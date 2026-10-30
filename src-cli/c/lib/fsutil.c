#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fsutil.h"

//SNIP_fsutil

int tc_safe_name(const char *name)
{
    const char *p;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }
    for (p = name; *p; p++) {
        if (*p == '/' || *p == '\n') {
            return 0;
        }
    }
    return 1;
}

int tc_read_file(const char *path, char **data, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf = NULL;
    size_t n = 0, cap = 0, got;

    if (f == NULL) {
        return -1;
    }
    for (;;) {
        if (n + 4096 + 1 > cap) {
            char *nb;
            cap = cap ? cap * 2 : 8192;
            nb = realloc(buf, cap);
            if (nb == NULL) {
                free(buf);
                fclose(f);
                errno = ENOMEM;
                return -1;
            }
            buf = nb;
        }
        got = fread(buf + n, 1, 4096, f);
        n += got;
        if (got < 4096) {
            break;
        }
    }
    if (ferror(f)) {
        int e = errno;
        free(buf);
        fclose(f);
        errno = e ? e : EIO;
        return -1;
    }
    fclose(f);
    if (buf == NULL) {
        buf = malloc(1);
        if (buf == NULL) {
            errno = ENOMEM;
            return -1;
        }
    }
    buf[n] = '\0';
    *data = buf;
    *len = n;
    return 0;
}

int tc_write_atomic(const char *path, const void *data, size_t len)
{
    char tmp[PATH_MAX];
    const char *slash = strrchr(path, '/');
    int fd, e;
    size_t done = 0;

    if (slash == NULL) {
        snprintf(tmp, sizeof tmp, ".%s.tmp.%d", path, (int)getpid());
    } else {
        snprintf(tmp, sizeof tmp, "%.*s/.%s.tmp.%d", (int)(slash - path), path, slash + 1, (int)getpid());
    }
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    while (done < len) {
        ssize_t w = write(fd, (const char *)data + done, len - done);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            e = errno;
            close(fd);
            unlink(tmp);
            errno = e;
            return -1;
        }
        done += (size_t)w;
    }
    if (close(fd) != 0 || rename(tmp, path) != 0) {
        e = errno;
        unlink(tmp);
        errno = e;
        return -1;
    }
    return 0;
}

/* dst = a + sep + b (sep only when b is not empty); -1 when it does not fit */
static int join(char *dst, size_t sz, const char *a, const char *sep, const char *b)
{
    size_t la = strlen(a), ls = b[0] ? strlen(sep) : 0, lb = strlen(b);

    if (la + ls + lb + 1 > sz) {
        return -1;
    }
    memcpy(dst, a, la);
    memcpy(dst + la, sep, ls);
    memcpy(dst + la + ls, b, lb + 1);
    return 0;
}

/* realpath of the existing part of path, with the rest appended */
static int resolve(const char *path, char *out, size_t outsz)
{
    char head[PATH_MAX], rest[PATH_MAX], real[PATH_MAX];
    char *slash;

    if (strlen(path) >= sizeof head) {
        return -1;
    }
    memcpy(head, path, strlen(path) + 1);
    rest[0] = '\0';
    while (realpath(head, real) == NULL) {
        char tail[PATH_MAX];
        struct stat st;
        if (errno != ENOENT && errno != ENOTDIR) {
            return -1;
        }
        /* a dangling link cannot be resolved: do not guess where it leads */
        if (lstat(head, &st) == 0 && S_ISLNK(st.st_mode)) {
            return -1;
        }
        slash = strrchr(head, '/');
        if (slash == NULL) {
            /* a relative name that does not exist: resolve from the current directory */
            if (join(tail, sizeof tail, head, "/", rest) != 0) {
                return -1;
            }
            memcpy(rest, tail, strlen(tail) + 1);
            head[0] = '.';
            head[1] = '\0';
            continue;
        }
        if (join(tail, sizeof tail, slash + 1, "/", rest) != 0) {
            return -1;
        }
        memcpy(rest, tail, strlen(tail) + 1);
        if (slash == head) {
            slash[1] = '\0';
        } else {
            *slash = '\0';
        }
    }
    return join(out, outsz, real, strcmp(real, "/") == 0 ? "" : "/", rest);
}

int tc_path_within(const char *root, const char *path)
{
    char r[PATH_MAX], p[PATH_MAX];
    size_t rl;
    char *seg;

    if (resolve(root, r, sizeof r) != 0 || resolve(path, p, sizeof p) != 0) {
        return 0;
    }
    /* ".." left in the not yet existing tail would escape */
    for (seg = strstr(p, ".."); seg != NULL; seg = strstr(seg + 1, "..")) {
        if ((seg == p || seg[-1] == '/') && (seg[2] == '\0' || seg[2] == '/')) {
            return 0;
        }
    }
    rl = strlen(r);
    if (strcmp(r, "/") == 0) {
        return strcmp(p, "/") != 0;
    }
    return strncmp(p, r, rl) == 0 && p[rl] == '/' && p[rl + 1] != '\0';
}

int tc_lock_acquire(const char *library, pid_t *holder)
{
    char path[PATH_MAX], buf[32];
    int attempt;

    snprintf(path, sizeof path, "%s/.trakcrate-lock", library);
    for (attempt = 0; attempt < 2; attempt++) {
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd >= 0) {
            int n = snprintf(buf, sizeof buf, "%d\n", (int)getpid());
            if (write(fd, buf, (size_t)n) != n) {
                int e = errno;
                close(fd);
                unlink(path);
                errno = e;
                return -1;
            }
            close(fd);
            return 0;
        }
        if (errno != EEXIST) {
            return -1;
        }
        {
            FILE *f = fopen(path, "r");
            long pid = 0;
            if (f != NULL) {
                if (fscanf(f, "%ld", &pid) != 1) {
                    pid = 0;
                }
                fclose(f);
            }
            if (pid > 0 && (kill((pid_t)pid, 0) == 0 || errno != ESRCH)) {
                *holder = (pid_t)pid;
                return 1;
            }
            /* no such process (or no pid in the file): stale */
            if (attempt == 0) {
                unlink(path);
                continue;
            }
            *holder = (pid_t)pid;
            return 1;
        }
    }
    return -1;
}

void tc_lock_release(const char *library)
{
    char path[PATH_MAX];
    FILE *f;
    long pid = 0;

    snprintf(path, sizeof path, "%s/.trakcrate-lock", library);
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    if (fscanf(f, "%ld", &pid) != 1) {
        pid = 0;
    }
    fclose(f);
    if (pid == (long)getpid()) {
        unlink(path);
    }
}

//SNIP_fsutil
