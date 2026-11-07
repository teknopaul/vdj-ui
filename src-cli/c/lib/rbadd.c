/*
 * tc_add_to_drive: the C port of addToDrive in src/trakcrate/exporter.cpp. The order of the steps, the refusals and their messages are the C++ ones.
 */
#define _GNU_SOURCE
#include "rbadd.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "fsops.h"
#include "fsutil.h"
#include "rbappend.h"
#include "rblimits.h"
#include "rbexport_internal.h"

#pragma GCC diagnostic ignored "-Wformat-truncation"

#define PDB_REL "PIONEER/rekordbox/export.pdb"

void tc_add_result_free(struct tc_add_result *res)
{
    size_t i;

    for (i = 0; i < res->ncreated; i++) {
        free(res->created[i]);
    }
    free(res->created);
    res->created = NULL;
    res->ncreated = 0;
}

/* ---- file operations ---- */

static int is_regular(const char *path)
{
    struct stat sb;

    return stat(path, &sb) == 0 && S_ISREG(sb.st_mode);
}

static int exists(const char *path)
{
    struct stat sb;

    return lstat(path, &sb) == 0;
}

static void sync_drive(const char *drive)
{
    int fd = open(drive, O_RDONLY | O_DIRECTORY);

    if (fd >= 0) {
        syncfs(fd);
        close(fd);
    }
}

/* Writes drive/rel from a file (src) or from memory: to "<name>.trakcrate-tmp", fsynced, then renamed over the final name, so an interrupted copy never
 * leaves a damaged file under its real name; a partial temporary is removed again. 0, 1 when cancelled, -1 with err. */
static int copy_safe(const char *drive, const char *rel, const char *src, const uint8_t *mem, size_t mem_len, volatile int *cancel, uint64_t *total,
                     char *err, size_t errsz)
{
    char final[PATH_MAX], tmp[PATH_MAX], dir[PATH_MAX], *slash;
    static uint8_t buf[1 << 20];
    int fd, in = -1, rc = 0;
    uint64_t written = 0;

    snprintf(final, sizeof final, "%s/%s", drive, rel);
    snprintf(tmp, sizeof tmp, "%s.trakcrate-tmp", final);
    snprintf(dir, sizeof dir, "%s", final);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    }
    if (tc_mkdir_p(dir) != 0) {
        snprintf(err, errsz, "cannot create folder %s: %s", dir, strerror(errno));
        return -1;
    }
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        snprintf(err, errsz, "cannot create %s: %s", tmp, strerror(errno));
        return -1;
    }
#define PUT(p, n) do { \
        size_t done_ = 0; \
        while (done_ < (n)) { \
            ssize_t w_ = write(fd, (const uint8_t *)(p) + done_, (n) - done_); \
            if (w_ < 0) { if (errno == EINTR) continue; snprintf(err, errsz, "cannot write %s: %s", tmp, strerror(errno)); rc = -1; break; } \
            done_ += (size_t)w_; \
        } \
        written += (n); \
    } while (0)
    if (src == NULL) {
        PUT(mem, mem_len);
    } else {
        in = open(src, O_RDONLY);
        if (in < 0) {
            snprintf(err, errsz, "cannot read %s", src);
            rc = -1;
        }
        while (rc == 0) {
            ssize_t r;

            if (cancel != NULL && *cancel) {
                rc = 1;
                break;
            }
            r = read(in, buf, sizeof buf);
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                snprintf(err, errsz, "read error on %s", src);
                rc = -1;
                break;
            }
            if (r == 0) {
                break;
            }
            {
                size_t rn = (size_t)r;

                PUT(buf, rn);
            }
        }
        if (in >= 0) {
            close(in);
        }
    }
#undef PUT
    if (rc == 0 && fsync(fd) != 0) {
        snprintf(err, errsz, "cannot sync %s: %s", tmp, strerror(errno));
        rc = -1;
    }
    close(fd);
    if (rc == 0 && rename(tmp, final) != 0) {
        snprintf(err, errsz, "cannot replace %s: %s", final, strerror(errno));
        rc = -1;
    }
    if (rc != 0) {
        unlink(tmp);
        return rc;
    }
    *total += written;
    return 0;
}

/* Makes room for a new export.pdb.bak: .bak.2 becomes .bak.3 (the old .bak.3 is dropped) and .bak becomes .bak.2. Renames only. */
static int rotate_backups(const char *pdb, char *err, size_t errsz)
{
    char a[PATH_MAX], b[PATH_MAX];
    int n;

    snprintf(a, sizeof a, "%s.bak.3", pdb);
    unlink(a);
    for (n = 2; n >= 1; n--) {
        if (n == 1) {
            snprintf(a, sizeof a, "%s.bak", pdb);
        } else {
            snprintf(a, sizeof a, "%s.bak.%d", pdb, n);
        }
        snprintf(b, sizeof b, "%s.bak.%d", pdb, n + 1);
        if (exists(a) && rename(a, b) != 0) {
            snprintf(err, errsz, "cannot rotate %s: %s", a, strerror(errno));
            return -1;
        }
    }
    return 0;
}

/* removes the files in res->created (only plain files below Contents/, PIONEER/Artwork/ and PIONEER/USBANLZ/), then the folders that became empty.
 * What could not be removed stays in res->created. */
static void delete_created(const char *drive, struct tc_add_result *res)
{
    size_t i, kept = 0;
    struct strlist dirs = { NULL, 0, 0 };

    for (i = 0; i < res->ncreated; i++) {
        const char *r = res->created[i];
        char full[PATH_MAX], d[PATH_MAX];
        struct stat sb;
        int inside = (strncmp(r, "Contents/", 9) == 0 || strncmp(r, "PIONEER/Artwork/", 16) == 0 || strncmp(r, "PIONEER/USBANLZ/", 16) == 0) &&
                     strstr(r, "..") == NULL && strcmp(r, "PIONEER/USBANLZ/USBMNG.DAT") != 0;

        snprintf(full, sizeof full, "%s/%s", drive, r);
        if (!inside || lstat(full, &sb) != 0 || !S_ISREG(sb.st_mode) || unlink(full) != 0) {
            res->created[kept++] = res->created[i];
            continue;
        }
        snprintf(d, sizeof d, "%s", r);
        free(res->created[i]);
        for (;;) {
            char *slash = strrchr(d, '/');

            if (slash == NULL) {
                break;
            }
            *slash = '\0';
            tcx_sl_add(&dirs, d);
        }
    }
    res->ncreated = kept;
    /* deepest folders first; rmdir on a folder with something in it just fails, which is what we want */
    for (i = dirs.n; i-- > 0;) {
        char full[PATH_MAX];
        const char *name = strrchr(dirs.v[i], '/');

        name = name != NULL ? name + 1 : dirs.v[i];
        if (strcmp(name, "Contents") == 0 || strcmp(name, "Artwork") == 0 || strcmp(name, "USBANLZ") == 0 || strcmp(dirs.v[i], "PIONEER") == 0) {
            continue;
        }
        snprintf(full, sizeof full, "%s/%s", drive, dirs.v[i]);
        rmdir(full);
    }
    tcx_sl_free(&dirs);
}

/* ---- the engine ---- */

#define FAIL(...) do { snprintf(res->error, sizeof res->error, __VA_ARGS__); rc = -1; goto out; } while (0)
#define STOPPED(step) do { \
        if (o->stop_at != NULL && strcmp(o->stop_at, (step)) == 0) { \
            snprintf(res->error, sizeof res->error, "stopped at %s", (step)); \
            rc = -1; \
            stopped = 1; \
            goto out; \
        } \
    } while (0)

static int created_add(struct tc_add_result *res, const char *rel)
{
    char **n = realloc(res->created, (res->ncreated + 1) * sizeof *n);

    if (n == NULL) {
        return -1;
    }
    res->created = n;
    res->created[res->ncreated] = strdup(rel);
    if (res->created[res->ncreated] == NULL) {
        return -1;
    }
    res->ncreated++;
    return 0;
}

struct addfile {
    char *rel, *src;
    const char *kind;
};

static int addfile_add(struct addfile **v, size_t *n, size_t *cap, const char *rel, const char *src, const char *kind)
{
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 64;
        struct addfile *nv = realloc(*v, nc * sizeof *nv);

        if (nv == NULL) {
            return -1;
        }
        *v = nv;
        *cap = nc;
    }
    (*v)[*n].rel = strdup(rel);
    (*v)[*n].src = strdup(src);
    (*v)[*n].kind = kind;
    if ((*v)[*n].rel == NULL || (*v)[*n].src == NULL) {
        free((*v)[*n].rel);
        free((*v)[*n].src);
        return -1;
    }
    (*n)++;
    return 0;
}

static int cancelled_now(const struct tc_add_opts *o)
{
    return o->cancel != NULL && *o->cancel;
}

int tc_add_to_drive(const struct tc_export_list *list, const struct tc_add_opts *o, struct tc_add_result *res)
{
    struct tc_export_opts eo;
    struct gathered g;
    struct tc_drive_db db;
    struct tc_append_result ar;
    struct tcx_seed seed;
    struct strlist anlz = { NULL, 0, 0 };
    uint8_t *old_bytes = NULL, *back = NULL;
    size_t old_len = 0, back_len = 0, i, k;
    char pdb[PATH_MAX], path[PATH_MAX], err[1024] = "", stage[PATH_MAX] = "", base[PATH_MAX];
    char **problems = NULL;
    size_t nproblems = 0;
    unsigned char *is_new = NULL;
    struct tc_plan_track *plan_tracks = NULL;
    struct tc_export_plan plan;
    int rc = 0, have_db = 0, have_ar = 0, have_g = 0, stopped = 0, no_rollback = 0, wrote_any = 0;
    struct stat sb;
    int total = 0, done = 0;
    uint64_t audio_bytes = 0, meta = 0, stage_estimate = 0;
    struct addfile *files = NULL;
    size_t nfiles = 0, fcap = 0;
    char newpath[PATH_MAX], bak[PATH_MAX];

    memset(res, 0, sizeof *res);
    memset(&g, 0, sizeof g);
    memset(&db, 0, sizeof db);
    memset(&ar, 0, sizeof ar);
    memset(&eo, 0, sizeof eo);
    eo.include_artwork = o->include_artwork;
    eo.include_pvbr = o->include_pvbr;
    eo.include_pwv4 = o->include_pwv4;
    eo.include_pwv5 = o->include_pwv5;
    eo.allow_unanalysed = o->allow_unanalysed;
    eo.raise_limits = o->raise_limits;
    eo.cue = o->cue;
    eo.staging_base = o->staging_base;
    eo.progress = o->progress;
    eo.warn = o->warn;
    eo.ud = o->ud;

    if (o->drive == NULL || o->drive[0] == '\0' || stat(o->drive, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
        FAIL("the pendrive is not available: %s", o->drive != NULL ? o->drive : "");
    }
    if (list == NULL) {
        FAIL("add exactly one playlist");
    }
    if (o->tpl == NULL || o->tpl_len == 0) {
        FAIL("no template export.pdb");
    }
    snprintf(path, sizeof path, "%s/PIONEER/rekordbox/exportLibrary.db", o->drive);
    if (exists(path)) {
        FAIL("this pendrive also has exportLibrary.db (the newer rekordbox format), which cannot be added to safely: nothing was changed");
    }
    snprintf(pdb, sizeof pdb, "%s/%s", o->drive, PDB_REL);
    if (!is_regular(pdb)) {
        FAIL("this pendrive has no PIONEER/rekordbox/export.pdb");
    }
    if (access(o->drive, W_OK) != 0 && !o->dry_run) {
        FAIL("the pendrive is read only");
    }

    /* the old database into memory, checked before anything is written */
    {
        char *data;

        if (tc_read_file(pdb, &data, &old_len) != 0) {
            FAIL("cannot read export.pdb: %s", strerror(errno));
        }
        old_bytes = (uint8_t *)data;
    }
    if (tc_drive_db_load(old_bytes, old_len, &db, &problems, &nproblems) != 0) {
        size_t n = (size_t)snprintf(res->error, sizeof res->error, "the database on the pendrive cannot be added to (nothing was changed):");

        for (i = 0; i < nproblems && i < 6 && n < sizeof res->error; i++) {
            n += (size_t)snprintf(res->error + n, sizeof res->error - n, "\n  %s", problems[i]);
        }
        rc = -1;
        have_db = 1;
        goto out;
    }
    have_db = 1;
    for (i = 0; i < db.nwarnings; i++) {
        char m[1100];

        snprintf(m, sizeof m, "database: %s", db.warnings[i]);
        if (o->warn != NULL) {
            o->warn(o->ud, m);
        }
    }
    {
        char *key = strdup(list->name);

        for (i = 0; key != NULL && key[i] != '\0'; i++) {
            key[i] = (char)((key[i] >= 'A' && key[i] <= 'Z') ? key[i] + 32 : key[i]);
        }
        for (i = 0; key != NULL && i < db.nplaylists; i++) {
            if (strcmp(db.playlist_lower[i], key) == 0) {
                free(key);
                FAIL("the pendrive already has a playlist called \"%s\": nothing was changed", list->name);
            }
        }
        free(key);
    }

    /* a copy of the old database outside the stick, kept for good */
    if (!o->dry_run) {
        time_t now = time(NULL);
        struct tm tm;
        char stamp[32], copy[PATH_MAX];
        const char *bd = o->backup_dir != NULL && o->backup_dir[0] ? o->backup_dir : "/var/tmp";
        int n;

        gmtime_r(&now, &tm);
        strftime(stamp, sizeof stamp, "%Y%m%dT%H%M%SZ", &tm);
        tc_mkdir_p(bd);
        snprintf(copy, sizeof copy, "%s/rekordbox-edit-backup-export.%s.pdb", bd, stamp);
        for (n = 2; exists(copy); n++) {
            snprintf(copy, sizeof copy, "%s/rekordbox-edit-backup-export.%s-%d.pdb", bd, stamp, n);
        }
        if (tc_write_atomic(copy, old_bytes, old_len) != 0) {
            FAIL("cannot make the backup copy %s: %s", copy, strerror(errno));
        }
        {
            char *data;

            if (tc_read_file(copy, &data, &back_len) != 0) {
                FAIL("cannot make the backup copy %s: %s", copy, strerror(errno));
            }
            back = (uint8_t *)data;
        }
        if (back_len != old_len || memcmp(back, old_bytes, old_len) != 0) {
            FAIL("cannot make the backup copy %s", copy);
        }
        snprintf(res->backup_copy, sizeof res->backup_copy, "%s", copy);
    }
    STOPPED("backup");

    /* the playlist against what the pendrive holds */
    seed.db = &db;
    seed.drive = o->drive;
    if (tcx_gather(list, 1, &eo, &seed, &g) != 0) {
        FAIL("out of memory");
    }
    have_g = 1;
    if (g.problems.n > 0) {
        size_t n = (size_t)snprintf(res->error, sizeof res->error, "cannot add:");

        for (i = 0; i < g.problems.n && i < 8 && n < sizeof res->error; i++) {
            n += (size_t)snprintf(res->error + n, sizeof res->error - n, "\n  %s", g.problems.v[i]);
        }
        if (g.problems.n > 8 && n < sizeof res->error) {
            snprintf(res->error + n, sizeof res->error - n, "\n  ... and %zu more", g.problems.n - 8);
        }
        rc = -1;
        goto out;
    }
    if (g.ntracks == 0) {
        FAIL("the playlist is empty");
    }
    {   /* the limits of the decks, counted on the whole database after the addition */
        const char **new_artist = calloc(g.ntracks, sizeof *new_artist);
        const char **alb_artist = calloc(g.ntracks, sizeof *alb_artist), **alb_name = calloc(g.ntracks, sizeof *alb_name);
        size_t n_tracks = 0, n_artists = 0, n_albums = 0;
        char msg[2048], *nl;

        if (new_artist == NULL || alb_artist == NULL || alb_name == NULL) {
            free(new_artist);
            free(alb_artist);
            free(alb_name);
            FAIL("out of memory");
        }
        for (i = 0; i < g.ntracks; i++) {
            const struct gtrack *t = &g.tracks[i];
            const struct tc_name_id *a;
            int dup = 0;

            if (t->on_stick == 1) {
                continue;
            }
            n_tracks++;
            a = t->artist[0] ? tc_name_find(db.artist, db.nartist, 0, t->artist) : NULL;
            if (t->artist[0] && a == NULL) {
                for (k = 0; k < n_artists && !dup; k++) {
                    dup = strcmp(new_artist[k], t->artist) == 0;
                }
                if (!dup) {
                    new_artist[n_artists++] = t->artist;
                }
            }
            if (t->album[0] && (a == NULL || tc_name_find(db.album, db.nalbum, a->id, t->album) == NULL)) {
                dup = 0;
                for (k = 0; k < n_albums && !dup; k++) {
                    dup = strcmp(alb_artist[k], t->artist) == 0 && strcmp(alb_name[k], t->album) == 0;
                }
                if (!dup) {
                    alb_artist[n_albums] = t->artist;
                    alb_name[n_albums++] = t->album;
                }
            }
        }
        free(new_artist);
        free(alb_artist);
        free(alb_name);
        if (tc_check_deck_limits(db.ntracks + n_tracks, db.nartist + n_artists, db.nalbum + n_albums, o->raise_limits, msg, sizeof msg, NULL, NULL) > 0) {
            nl = strchr(msg, '\n');
            if (nl != NULL) {
                *nl = '\0';
            }
            FAIL("cannot add:\n  %s", msg);
        }
    }
    plan_tracks = calloc(g.ntracks, sizeof *plan_tracks);
    if (plan_tracks == NULL) {
        FAIL("out of memory");
    }
    for (i = 0; i < g.ntracks; i++) {
        plan_tracks[i] = g.tracks[i].plan;
    }
    plan.tracks = plan_tracks;
    plan.ntracks = g.ntracks;
    plan.playlists = g.lists;
    plan.nplaylists = g.nlists;
    if (tc_append_playlist(&db, &plan, o->tpl, o->tpl_len, &ar, err, sizeof err) != 0) {
        FAIL("cannot add: %s", err);
    }
    have_ar = 1;
    {
        char **diffs = NULL;
        size_t nd = tc_verify_appended(old_bytes, old_len, ar.bytes, ar.len, &plan, &ar, &diffs);

        if (nd > 0) {
            snprintf(res->error, sizeof res->error, "the new database does not say what the plan says: %s", diffs[0]);
            tc_problems_free(diffs, nd);
            rc = -1;
            goto out;
        }
        tc_problems_free(diffs, nd);
    }
    for (i = 0; i < g.ntracks; i++) {
        if (ar.track_is_new[i]) {
            res->new_tracks++;
        } else if (ar.track_ids[i]) {
            res->existing_tracks++;
        }
    }
    STOPPED("planned");

    /* space on the stick for what is new, and the staging area for the analysis files and artwork */
    is_new = calloc(g.ntracks, 1);
    if (is_new == NULL) {
        FAIL("out of memory");
    }
    {
        struct gathered fresh;
        size_t nn = 0;

        memset(&fresh, 0, sizeof fresh);
        fresh.tracks = calloc(g.ntracks, sizeof *fresh.tracks);
        if (fresh.tracks == NULL) {
            FAIL("out of memory");
        }
        for (i = 0; i < g.ntracks; i++) {
            is_new[i] = g.tracks[i].on_stick != 1;
            if (is_new[i]) {
                fresh.tracks[nn++] = g.tracks[i];                   /* a shallow copy for the estimate: never freed through `fresh` */
            }
            if (g.tracks[i].on_stick == 0) {
                audio_bytes += g.tracks[i].audio_bytes;
            }
        }
        fresh.ntracks = nn;
        stage_estimate = tcx_estimate_metadata(&fresh);
        free(fresh.tracks);
    }
    meta = stage_estimate + ar.len;
    res->audio_bytes = audio_bytes;
    res->meta_bytes = meta;
    if (o->dry_run) {
        goto out;
    }
    {
        struct statvfs sv;

        if (statvfs(o->drive, &sv) == 0) {
            const uint64_t avail = (uint64_t)sv.f_bavail * sv.f_frsize, need = audio_bytes + meta + o->min_free_bytes;

            if (avail < need) {
                FAIL("not enough space on the pendrive: needs %llu MiB, %llu MiB free", (unsigned long long)(need >> 20), (unsigned long long)(avail >> 20));
            }
        }
    }
    tcx_pick_staging_base(&eo, base, sizeof base, &res->staged_on_tmpfs, &eo);
    snprintf(stage, sizeof stage, "%s/trakcrate-add.XXXXXX", base);
    if (mkdtemp(stage) == NULL) {
        stage[0] = '\0';
        FAIL("cannot create the staging area: %s", strerror(errno));
    }
    {
        struct statvfs sv;

        if (statvfs(stage, &sv) == 0 && (uint64_t)sv.f_bavail * sv.f_frsize < stage_estimate + (8u << 20)) {
            FAIL("not enough space in the staging area %s", base);
        }
    }
    tcx_progress(&eo, "stage", 0, (int)g.ntracks, "");
    if (tcx_stage_track_files(&g, &eo, ar.analyze_paths, (const char *)is_new, stage, &anlz, err, sizeof err) != 0) {
        FAIL("%s", err);
    }
    for (i = 0, k = 0; i < g.ntracks; i++) {
        if (is_new[i]) {
            if (k >= anlz.n || tcx_verify_staged_anlz(stage, anlz.v[k], g.tracks[i].file_path, err, sizeof err) != 0) {
                FAIL("%s", k >= anlz.n ? "an analysis file was not staged" : err);
            }
            k++;
        }
    }

    /* the files, audio first; nothing that exists is changed */
    for (i = 0; i < g.ntracks; i++) {
        if (g.tracks[i].on_stick == 0 && addfile_add(&files, &nfiles, &fcap, g.tracks[i].file_path + 1, g.tracks[i].audio, "audio") != 0) {
            FAIL("out of memory");
        }
    }
    for (i = 0; i < ar.nartwork; i++) {
        snprintf(path, sizeof path, "%s/%s", stage, ar.artwork_paths[i] + 1);
        if (addfile_add(&files, &nfiles, &fcap, ar.artwork_paths[i] + 1, path, "artwork") != 0) {
            FAIL("out of memory");
        }
    }
    for (i = 0; i < anlz.n; i++) {
        int e;

        for (e = 0; e < 2; e++) {
            char rel[PATH_MAX];

            snprintf(rel, sizeof rel, "%s", anlz.v[i]);
            if (e == 1) {
                snprintf(rel + strlen(rel) - 3, 4, "EXT");
            }
            snprintf(path, sizeof path, "%s/%s", stage, rel);
            if (addfile_add(&files, &nfiles, &fcap, rel, path, "analysis") != 0) {
                FAIL("out of memory");
            }
        }
    }
    total = (int)nfiles + 2;
    {
        const char *last_kind = "";

        wrote_any = 1;
        for (i = 0; i < nfiles; i++) {
            int c;

            if (strcmp(files[i].kind, last_kind) != 0) {
                last_kind = files[i].kind;
                STOPPED(last_kind);
            }
            if (cancelled_now(o)) {
                res->cancelled = 1;
                FAIL("cancelled");
            }
            tcx_progress(&eo, "copy", done, total, files[i].rel);
            c = copy_safe(o->drive, files[i].rel, files[i].src, NULL, 0, o->cancel, &res->bytes_written, err, sizeof err);
            if (c > 0) {
                res->cancelled = 1;
                FAIL("cancelled");
            }
            if (c < 0) {
                FAIL("%s", err);
            }
            if (created_add(res, files[i].rel) != 0) {
                FAIL("out of memory");
            }
            res->files_copied++;
            done++;
        }
    }
    sync_drive(o->drive);                               /* all the new files are on the stick before the database that names them */

    /* the new database next to the old one */
    STOPPED("database");
    snprintf(newpath, sizeof newpath, "%s.new", pdb);
    tcx_progress(&eo, "copy", done, total, PDB_REL ".new");
    if (copy_safe(o->drive, PDB_REL ".new", NULL, ar.bytes, ar.len, NULL, &res->bytes_written, err, sizeof err) != 0) {
        FAIL("%s", err);
    }
    done++;
    sync_drive(o->drive);

    /* the one change to existing data: two renames */
    if (cancelled_now(o)) {
        res->cancelled = 1;
        FAIL("cancelled");
    }
    STOPPED("switch");
    tcx_progress(&eo, "switch", done, total, "Switching database...");
    snprintf(bak, sizeof bak, "%s.bak", pdb);
    if (rotate_backups(pdb, err, sizeof err) != 0) {
        FAIL("cannot rotate the old backups: %s", err);
    }
    if (rename(pdb, bak) != 0) {
        FAIL("cannot rename export.pdb to export.pdb.bak: %s", strerror(errno));
    }
    STOPPED("between-renames");
    if (rename(newpath, pdb) != 0) {
        const int why = errno;

        if (rename(bak, pdb) != 0) {
            no_rollback = 1;
            FAIL("cannot put the new export.pdb in place (%s) and cannot restore the old one: rename %s to export.pdb", strerror(why), bak);
        }
        FAIL("cannot put the new export.pdb in place: %s", strerror(why));
    }
    sync_drive(o->drive);
    res->switched = 1;
    snprintf(res->backup_on_drive, sizeof res->backup_on_drive, "%s.bak", PDB_REL);
    done++;
    tcx_progress(&eo, "switch", done, total, "");
    {
        char *data;
        size_t dl;

        if (tc_read_file(pdb, &data, &dl) != 0) {
            FAIL("the database on the pendrive does not read back as written: %s", strerror(errno));
        }
        if (dl != ar.len || memcmp(data, ar.bytes, dl) != 0) {
            free(data);
            FAIL("the database on the pendrive does not read back as written");
        }
        free(data);
    }
    STOPPED("done");
out:
    if (rc != 0 && !stopped && !res->switched && !no_rollback && wrote_any) {      /* an error or cancel before the switch: take what was added off again */
        snprintf(path, sizeof path, "%s.new", pdb);
        unlink(path);
        snprintf(path, sizeof path, "%s.new.trakcrate-tmp", pdb);
        unlink(path);
        delete_created(o->drive, res);
    }
    if (stage[0]) {
        tc_rm_rf(stage);
    }
    for (i = 0; i < nfiles; i++) {
        free(files[i].rel);
        free(files[i].src);
    }
    free(files);
    free(is_new);
    free(plan_tracks);
    tc_problems_free(problems, nproblems);
    free(old_bytes);
    free(back);
    tcx_sl_free(&anlz);
    if (have_ar) {
        tc_append_result_free(&ar);
    }
    if (have_g) {
        tcx_gathered_free(&g);
    }
    if (have_db) {
        tc_drive_db_free(&db);
    }
    return rc;
}
