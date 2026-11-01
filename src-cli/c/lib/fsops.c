#define _GNU_SOURCE
#include "fsops.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static int has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);

    return n > e && strcasecmp(name + n - e, ext) == 0;
}

int tc_find_audio(const char *dir, char *out, size_t outsz)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char best[NAME_MAX + 1] = "";

    if (d == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        if ((has_ext(e->d_name, ".mp3") || has_ext(e->d_name, ".wav") || has_ext(e->d_name, ".flac")) && (best[0] == '\0' || strcmp(e->d_name, best) < 0)) {
            snprintf(best, sizeof best, "%s", e->d_name);
        }
    }
    closedir(d);
    if (best[0] == '\0') {
        return 0;
    }
    return snprintf(out, outsz, "%s/%s", dir, best) < (int)outsz;
}

int tc_mkdir_p(const char *path)
{
    char buf[PATH_MAX], *p;
    struct stat sb;

    if (strlen(path) >= sizeof buf) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(buf, path);
    for (p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(buf, 0755) != 0 && !(errno == EEXIST && stat(buf, &sb) == 0 && S_ISDIR(sb.st_mode))) {
        return -1;
    }
    return 0;
}

int tc_rm_rf(const char *path)
{
    struct stat sb;
    int rc = 0;

    if (lstat(path, &sb) != 0) {
        return errno == ENOENT ? 0 : -1;
    }
    if (S_ISDIR(sb.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *e;

        if (d == NULL) {
            return -1;
        }
        while ((e = readdir(d)) != NULL) {
            char child[PATH_MAX];

            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                continue;
            }
            if (snprintf(child, sizeof child, "%s/%s", path, e->d_name) >= (int)sizeof child || tc_rm_rf(child) != 0) {
                rc = -1;
            }
        }
        closedir(d);
        return rmdir(path) == 0 ? rc : -1;
    }
    return unlink(path);
}

int tc_dir_empty(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;
    int empty = 1;

    if (d == NULL) {
        return 1;
    }
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            empty = 0;
            break;
        }
    }
    closedir(d);
    return empty;
}
