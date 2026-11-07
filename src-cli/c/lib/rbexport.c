#define _GNU_SOURCE
#include "rbexport.h"
#include "rbexport_internal.h"

#include <cdj/anlz.h>
#include <ctype.h>
#include <dirent.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "artwork.h"
#include "beatgrid.h"
#include "cuemarks.h"
#include "fsops.h"
#include "fsutil.h"
#include "kv.h"
#include "rbanlz.h"
#include "rbdb.h"
#include "rblimits.h"
#include "rbpdb.h"
#include "rbsettings.h"
#include "sha256.h"
#include "wave.h"

/* Paths are built into PATH_MAX buffers; one that does not fit is cut and the open that follows fails, so the file never goes astray. */
#pragma GCC diagnostic ignored "-Wformat-truncation"

void tc_fat_safe_name(const char *name, char *out, size_t outsz)
{
    static const char *reserved[] = { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                      "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
    char s[PATH_MAX], stem[PATH_MAX], *b;
    size_t n = 0, i, stem_len;

    for (i = 0; name[i] != '\0' && n + 2 < sizeof s; i++) {
        unsigned char c = (unsigned char)name[i];

        s[n++] = (c < 0x20 || c == 0x7f || strchr("<>:\"/\\|?*", c) != NULL) ? '_' : (char)c;
    }
    s[n] = '\0';
    while (n > 0 && (s[n - 1] == '.' || s[n - 1] == ' ')) {
        s[--n] = '\0';
    }
    b = s;
    while (*b == ' ') {
        b++;
    }
    memmove(s, b, strlen(b) + 1);
    if (s[0] == '\0') {
        strcpy(s, "_");
    }
    stem_len = strcspn(s, ".");
    memcpy(stem, s, stem_len);
    stem[stem_len] = '\0';
    for (i = 0; stem[i]; i++) {
        stem[i] = (char)toupper((unsigned char)stem[i]);
    }
    for (i = 0; i < sizeof reserved / sizeof *reserved; i++) {
        if (strcmp(stem, reserved[i]) == 0) {
            memmove(s + stem_len + 1, s + stem_len, strlen(s + stem_len) + 1);
            s[stem_len] = '_';
            break;
        }
    }
    n = strlen(s);
    if (n > 200) {
        size_t cut = 200;

        while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) {
            cut--;
        }
        s[cut] = '\0';
    }
    snprintf(out, outsz, "%s", s);
}

void tc_rb_folder_name(const char *name, const char *fallback, char *out, size_t outsz)
{
    char s[PATH_MAX];
    size_t n = 0, i = 0, points = 0;

    while (name[i] != '\0' && points < 48 && n + 5 < sizeof s) {
        unsigned char c = (unsigned char)name[i];

        if (c < 0x20 || c == 0x7f || strchr("<>:\"/\\|?*", c) != NULL) {
            s[n++] = '_';
            i++;
        } else {
            s[n++] = (char)c;
            i++;
            while (name[i] != '\0' && (((unsigned char)name[i]) & 0xC0) == 0x80 && n + 5 < sizeof s) {
                s[n++] = name[i++];
            }
        }
        points++;
    }
    s[n] = '\0';
    while (n > 0 && s[n - 1] == ' ') {
        s[--n] = '\0';
    }
    if (n > 0 && s[n - 1] == '.') {
        s[n - 1] = '_';
    }
    if (n == 0) {
        snprintf(out, outsz, "%s", fallback);
        return;
    }
    tc_fat_safe_name(s, out, outsz);
}

/* ---- small helpers ---- */

int tcx_sl_add(struct strlist *l, const char *s)
{
    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 16;
        char **nv = realloc(l->v, nc * sizeof *nv);

        if (nv == NULL) {
            return -1;
        }
        l->v = nv;
        l->cap = nc;
    }
    l->v[l->n] = strdup(s);
    if (l->v[l->n] == NULL) {
        return -1;
    }
    l->n++;
    return 0;
}

void tcx_sl_free(struct strlist *l)
{
    size_t i;

    for (i = 0; i < l->n; i++) {
        free(l->v[i]);
    }
    free(l->v);
    memset(l, 0, sizeof *l);
}

int tcx_sl_addf(struct strlist *l, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return tcx_sl_add(l, buf);
}

void tcx_today(char *out, size_t sz)
{
    time_t t = time(NULL);
    struct tm tm;

    gmtime_r(&t, &tm);
    strftime(out, sz, "%Y-%m-%d", &tm);
}

void tcx_lower_into(const char *s, char *out, size_t sz)
{
    size_t i;

    for (i = 0; s[i] != '\0' && i + 1 < sz; i++) {
        out[i] = (char)tolower((unsigned char)s[i]);
    }
    out[i] = '\0';
}

static const char *kv_or(const tc_kvdoc *d, const char *key, const char *def)
{
    const char *v = tc_kv_get(d, key);

    return v != NULL ? v : def;
}

/* ---- the gathered tracks ---- */

static void gtrack_free(struct gtrack *t)
{
    free(t->dir);
    tc_kv_free(t->meta);
    tc_kv_free(t->source);
    tc_kv_free(t->grid_doc);
    tc_cuemarks_free(&t->cues);
    tc_waveform_free(&t->wave);
    free(t->art);
    free(t->title);
    free(t->artist);
    free(t->album);
    free(t->genre);
    free(t->comment);
    free(t->file_name);
    free(t->file_path);
    free(t->audio_hash);
}

void tcx_gathered_free(struct gathered *g)
{
    size_t i;

    for (i = 0; i < g->ntracks; i++) {
        gtrack_free(&g->tracks[i]);
    }
    free(g->tracks);
    for (i = 0; i < g->nlists; i++) {
        free(g->list_tracks[i]);
    }
    free(g->list_tracks);
    free(g->lists);
    tcx_sl_free(&g->problems);
}

static char *xdup(const char *s)
{
    return strdup(s != NULL ? s : "");
}

/* Same size and the same first and last megabyte: the two files are taken to be the same recording (reading all of a big file twice from a slow
 * stick for every track already on it would cost more than the copy it saves). */
static int same_audio(const char *a, const char *b)
{
    static uint8_t p[1 << 20], q[1 << 20];
    struct stat sa, sb;
    FILE *x, *y;
    const uint64_t chunk = (uint64_t)1 << 20;
    uint64_t n, at[2];
    int i, same = 1;

    if (stat(a, &sa) != 0 || stat(b, &sb) != 0 || sa.st_size != sb.st_size) {
        return 0;
    }
    n = (uint64_t)sa.st_size;
    x = fopen(a, "rb");
    y = fopen(b, "rb");
    if (x == NULL || y == NULL) {
        if (x != NULL) {
            fclose(x);
        }
        if (y != NULL) {
            fclose(y);
        }
        return 0;
    }
    at[0] = 0;
    at[1] = n > chunk ? n - chunk : 0;
    for (i = 0; i < 2 && same; i++) {
        size_t want = (size_t)(n < chunk ? n : chunk), r1, r2;

        if (fseeko(x, (off_t)at[i], SEEK_SET) != 0 || fseeko(y, (off_t)at[i], SEEK_SET) != 0) {
            same = 0;
            break;
        }
        r1 = fread(p, 1, want, x);
        r2 = fread(q, 1, want, y);
        same = r1 == r2 && memcmp(p, q, r1) == 0;
    }
    fclose(x);
    fclose(y);
    return same;
}

/* 0 = the path is free, 1 = the drive has a row for it and the file is this audio, 2 = the file is there and is this audio but has no row, 3 = something
 * else is there (another recording, or a row whose file is missing): choose another name. */
static int classify_on_drive(const struct tcx_seed *s, const char *full, const char *audio, uint64_t bytes)
{
    char on_disk[PATH_MAX], lower[PATH_MAX];
    struct stat sb;
    size_t k;
    int there;

    snprintf(on_disk, sizeof on_disk, "%s%s", s->drive, full);
    there = stat(on_disk, &sb) == 0 && S_ISREG(sb.st_mode);
    tcx_lower_into(full, lower, sizeof lower);
    for (k = 0; k < s->db->ntracks; k++) {
        if (strcmp(s->db->track_lower[k], lower) == 0) {
            return (there && s->db->tracks[k].file_size == (uint32_t)bytes && same_audio(audio, on_disk)) ? 1 : 3;
        }
    }
    if (there) {
        return same_audio(audio, on_disk) ? 2 : 3;
    }
    return 0;
}

/* FAT folders are case insensitive and the first spelling of an artist or album folder is reused: looks at one earlier path
 * (/Contents/<artist>/<album>/<file>) and adopts its spelling of the folders where they match ours, if none was adopted before. */
static void adopt_spelling(const char *file_path, char *adir, int *have_artist, char *aldir, int *have_album)
{
    char prev[PATH_MAX], *sa, *sb;

    if (strncmp(file_path, "/Contents/", 10) != 0) {
        return;
    }
    snprintf(prev, sizeof prev, "%s", file_path + 10);
    sa = strchr(prev, '/');
    if (sa == NULL) {
        return;
    }
    *sa = '\0';
    sb = strchr(sa + 1, '/');
    if (sb == NULL || strcasecmp(prev, adir) != 0) {
        return;
    }
    *sb = '\0';
    if (!*have_artist) {
        snprintf(adir, PATH_MAX, "%s", prev);
        *have_artist = 1;
    }
    if (!*have_album && strcasecmp(sa + 1, aldir) == 0) {
        snprintf(aldir, PATH_MAX, "%s", sa + 1);
        *have_album = 1;
    }
}

/* loads one track directory; 0 ok, 1 skipped with a problem recorded, -1 out of memory */
static int load_track(struct gathered *g, const char *dir, const char *list_name, const struct tc_export_opts *o, const struct tcx_seed *seed, struct gtrack *t)
{
    char path[PATH_MAX], rel[PATH_MAX], ext[16], lower[PATH_MAX], stem[NAME_MAX + 1], folder[NAME_MAX + 1], full[PATH_MAX], tmp[PATH_MAX];
    char *text;
    size_t len;
    struct stat sb;
    const char *m, *slash, *imported;
    double bpm_tag;
    long long rating;
    int existed;
    uint8_t *wbytes;
    size_t wlen, i;
    int j;

    memset(t, 0, sizeof *t);
    t->dir = strdup(dir);
    if (t->dir == NULL) {
        return -1;
    }
    if (stat(dir, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
        tcx_sl_addf(&g->problems, "%s: not a track folder: %s", list_name, dir);
        return 1;
    }
    if (!tc_find_audio(dir, t->audio, sizeof t->audio)) {
        tcx_sl_addf(&g->problems, "no audio file in %s", dir);
        return 1;
    }
    if (stat(t->audio, &sb) != 0) {
        tcx_sl_addf(&g->problems, "%s: cannot read the audio file", dir);
        return 1;
    }
    t->audio_bytes = (uint64_t)sb.st_size;
    snprintf(path, sizeof path, "%s/meta.txt", dir);
    if (tc_kv_load(path, &t->meta, &existed) != 0) {
        tcx_sl_addf(&g->problems, "%s: cannot read meta.txt: %s", list_name, strerror(errno));
        return 1;
    }
    snprintf(path, sizeof path, "%s/source.txt", dir);
    if (tc_kv_load(path, &t->source, NULL) != 0) {
        tcx_sl_addf(&g->problems, "%s: cannot read source.txt: %s", list_name, strerror(errno));
        return 1;
    }
    snprintf(path, sizeof path, "%s/beatgrid.txt", dir);
    if (tc_kv_load(path, &t->grid_doc, NULL) != 0) {
        tcx_sl_addf(&g->problems, "%s: cannot read beatgrid.txt: %s", list_name, strerror(errno));
        return 1;
    }
    snprintf(path, sizeof path, "%s/cuemarks.txt", dir);
    if (tc_read_file(path, &text, &len) == 0) {
        int rc = tc_cuemarks_parse(text, &t->cues);

        free(text);
        if (rc != 0) {
            return -1;
        }
    } else if (errno != ENOENT) {
        tcx_sl_addf(&g->problems, "%s: cannot read cuemarks.txt: %s", list_name, strerror(errno));
        return 1;
    }

    /* a trailing slash in the argument must not give an empty stem */
    {
        size_t sl = strlen(dir);

        while (sl > 1 && dir[sl - 1] == '/') {
            sl--;
        }
        snprintf(tmp, sizeof tmp, "%.*s", (int)sl, dir);
        slash = strrchr(tmp, '/');
        snprintf(stem, sizeof stem, "%s", slash != NULL ? slash + 1 : tmp);
        if (slash != NULL) {
            char up[PATH_MAX];

            snprintf(up, sizeof up, "%.*s", (int)(slash - tmp), tmp);
            slash = strrchr(up, '/');
            snprintf(folder, sizeof folder, "%s", slash != NULL ? slash + 1 : up);
        } else {
            folder[0] = '\0';
        }
    }

    m = tc_kv_get(t->meta, "title");
    t->title = xdup(m != NULL ? m : stem);
    t->artist = xdup(kv_or(t->meta, "artist", ""));
    t->album = xdup(kv_or(t->meta, "album", ""));
    t->genre = xdup(kv_or(t->meta, "genre", ""));
    t->comment = xdup(kv_or(t->meta, "comment", ""));
    t->audio_hash = xdup(kv_or(t->meta, "audio_sha256", ""));
    if (!t->title || !t->artist || !t->album || !t->genre || !t->comment || !t->audio_hash) {
        return -1;
    }
    t->plan.file_size = (uint32_t)t->audio_bytes;
    t->plan.sample_rate = (uint32_t)tc_kv_get_int(t->meta, "sample_rate", 44100);
    t->plan.bitrate_kbps = (uint32_t)(tc_kv_get_int(t->meta, "bitrate", 0) / 1000);
    t->plan.sample_depth = (uint32_t)tc_kv_get_int(t->meta, "bit_depth", 16);
    t->plan.year = (uint32_t)tc_kv_get_int(t->meta, "year", 0);
    t->plan.track_number = (uint32_t)tc_kv_get_int(t->meta, "track_number", 0);
    rating = tc_kv_get_int(t->meta, "rating", 0);
    t->plan.rating = (uint8_t)(rating < 0 ? 0 : rating > 5 ? 5 : rating);
    t->duration_ms = (double)tc_kv_get_int(t->meta, "duration_ms", 0);
    t->plan.duration_secs = (uint32_t)(t->duration_ms / 1000.0 + 0.5);
    imported = kv_or(t->source, "imported_at", "");
    if (strlen(imported) >= 10) {
        snprintf(t->date, sizeof t->date, "%.10s", imported);
    } else {
        tcx_today(t->date, sizeof t->date);
    }
    tc_beatgrid_from(t->grid_doc, &t->grid);
    bpm_tag = tc_kv_get_double(t->meta, "bpm_tag", 0);
    t->plan.bpm_x100 = t->grid.bpm <= 0 ? (uint32_t)(bpm_tag * 100 + 0.5) : (uint32_t)(t->grid.bpm * 100 + 0.5);

    /* the path on the drive: unique case insensitively (FAT) */
    {
        const char *dot = strrchr(t->audio, '.');
        char adir[PATH_MAX], aldir[PATH_MAX], fat_stem[PATH_MAX];
        size_t k;

        snprintf(ext, sizeof ext, "%s", dot != NULL ? dot : "");
        tcx_lower_into(ext, ext, sizeof ext);
        /* rekordbox's layout, Contents/<artist>/<album>/<file>, from the tags; FAT folders are case insensitive and the first spelling is reused */
        tc_rb_folder_name(t->artist, "UnknownArtist", adir, sizeof adir);
        tc_rb_folder_name(t->album, "UnknownAlbum", aldir, sizeof aldir);
        {
            int have_artist = 0, have_album = 0;

            for (k = 0; seed != NULL && k < seed->db->ntracks && !have_album; k++) {
                adopt_spelling(seed->db->tracks[k].file_path, adir, &have_artist, aldir, &have_album);
            }
            for (k = 0; k < g->ntracks && !have_album; k++) {
                adopt_spelling(g->tracks[k].file_path, adir, &have_artist, aldir, &have_album);
            }
        }
        tc_fat_safe_name(stem, fat_stem, sizeof fat_stem);
        snprintf(rel, sizeof rel, "/Contents/%s/%s/%s", adir, aldir, fat_stem);
        snprintf(full, sizeof full, "%s%s", rel, ext);
        for (j = 2;; j++) {
            int used = 0;

            tcx_lower_into(full, lower, sizeof lower);
            for (i = 0; i < g->ntracks && !used; i++) {
                char other[PATH_MAX];

                tcx_lower_into(g->tracks[i].file_path, other, sizeof other);
                used = strcmp(other, lower) == 0;
            }
            if (!used && seed != NULL) {
                t->on_stick = classify_on_drive(seed, full, t->audio, t->audio_bytes);
                used = t->on_stick == 3;
            }
            if (!used) {
                break;
            }
            snprintf(full, sizeof full, "%s (%d)%s", rel, j, ext);
        }
        t->file_path = strdup(full);
        t->file_name = strdup(strrchr(full, '/') + 1);
        if (t->file_path == NULL || t->file_name == NULL) {
            return -1;
        }
    }

    snprintf(path, sizeof path, "%s/waveform.bin", dir);
    if (tc_read_file(path, &text, &len) == 0) {
        wbytes = (uint8_t *)text;
        wlen = len;
        t->have_wave = tc_waveform_parse(wbytes, wlen, &t->wave) == 0;
        free(text);
    }
    if (!t->have_wave && !o->allow_unanalysed && t->on_stick != 1) {
        tcx_sl_addf(&g->problems, "not analysed: %s (%s)", stem, dir);
    }

    for (i = 0; i < t->cues.n; i++) {
        const struct tc_cuemark *c = &t->cues.marks[i];

        if (c->hot < 'A' || c->hot > 'C') {
            continue;
        }
        t->hot[t->nhot].hot_cue = c->hot - 'A' + 1;
        t->hot[t->nhot].time_ms = (uint32_t)((c->ms > 0 ? c->ms : 0) + 0.5);
        t->hot[t->nhot].marker = 1000;
        t->nhot++;
        if (t->nhot == 3) {
            break;
        }
    }
    for (i = 0; i + 1 < t->nhot; i++) {                     /* sorted by hot cue number */
        size_t k;

        for (k = i + 1; k < t->nhot; k++) {
            if (t->hot[k].hot_cue < t->hot[i].hot_cue) {
                struct tc_anlz_cue x = t->hot[i];

                t->hot[i] = t->hot[k];
                t->hot[k] = x;
            }
        }
    }
    if (o->include_artwork && t->on_stick != 1) {
        static const char *names[] = { "artwork.jpg", "artwork.jpeg", "artwork.png" };

        for (i = 0; i < 3; i++) {
            snprintf(path, sizeof path, "%s/%s", dir, names[i]);
            if (tc_read_file(path, &text, &len) == 0) {
                t->art = (uint8_t *)text;
                t->art_len = len;
                tc_sha256_hex(t->art, t->art_len, t->art_key);
                break;
            }
        }
    }
    return 0;
}

static int need_list_slot(struct gathered *g)
{
    struct tc_plan_playlist *nl = realloc(g->lists, (g->nlists + 1) * sizeof *nl);
    size_t **nt;

    if (nl == NULL) {
        return -1;
    }
    g->lists = nl;
    nt = realloc(g->list_tracks, (g->nlists + 1) * sizeof *nt);
    if (nt == NULL) {
        return -1;
    }
    g->list_tracks = nt;
    return 0;
}

int tcx_gather(const struct tc_export_list *lists, size_t nlists, const struct tc_export_opts *o, const struct tcx_seed *seed, struct gathered *g)
{
    size_t li, di, i;

    memset(g, 0, sizeof *g);
    if (nlists == 0) {
        tcx_sl_add(&g->problems, "no playlist chosen");
    }
    for (li = 0; li < nlists; li++) {
        const struct tc_export_list *src = &lists[li];
        size_t *idx = calloc(src->ndirs ? src->ndirs : 1, sizeof *idx);
        size_t cnt = 0;

        if (idx == NULL || need_list_slot(g) != 0) {
            free(idx);
            return -1;
        }
        for (di = 0; di < src->ndirs; di++) {
            char canon[PATH_MAX];
            size_t found = (size_t)-1;

            if (realpath(src->dirs[di], canon) == NULL) {
                snprintf(canon, sizeof canon, "%s", src->dirs[di]);
            }
            for (i = 0; i < g->ntracks; i++) {
                if (strcmp(g->tracks[i].canonical, canon) == 0) {
                    found = i;
                    break;
                }
            }
            if (found == (size_t)-1) {
                struct gtrack t;
                int rc;

                if (g->ntracks == g->cap) {
                    size_t nc = g->cap ? g->cap * 2 : 32;
                    struct gtrack *nt = realloc(g->tracks, nc * sizeof *nt);

                    if (nt == NULL) {
                        free(idx);
                        return -1;
                    }
                    g->tracks = nt;
                    g->cap = nc;
                }
                rc = load_track(g, src->dirs[di], src->name, o, seed, &t);
                if (rc < 0) {
                    gtrack_free(&t);
                    free(idx);
                    return -1;
                }
                if (rc > 0) {
                    gtrack_free(&t);
                    continue;
                }
                snprintf(t.canonical, sizeof t.canonical, "%s", canon);
                g->tracks[g->ntracks] = t;
                found = g->ntracks++;
            }
            idx[cnt++] = found;
        }
        g->lists[g->nlists].name = src->name;
        g->lists[g->nlists].tracks = idx;
        g->lists[g->nlists].ntracks = cnt;
        g->list_tracks[g->nlists] = idx;
        g->nlists++;
    }
    /* the limits of the decks, reported first so that they are never cut off the list of problems (adding to a drive counts the whole database instead) */
    if (seed == NULL) {
        const char **ar = calloc(g->ntracks ? g->ntracks : 1, sizeof *ar), **al = calloc(g->ntracks ? g->ntracks : 1, sizeof *al);
        size_t na = 0, nb = 0;
        char msg[2048], *line, *nl;
        int count;

        if (ar == NULL || al == NULL) {
            free(ar);
            free(al);
            return -1;
        }
        for (i = 0; i < g->ntracks; i++) {
            ar[i] = g->tracks[i].artist;
            al[i] = g->tracks[i].album;
        }
        if (tc_count_artists_albums(ar, al, g->ntracks, &na, &nb) != 0) {
            free(ar);
            free(al);
            return -1;
        }
        free(ar);
        free(al);
        count = tc_check_deck_limits(g->ntracks, na, nb, o->raise_limits, msg, sizeof msg, NULL, NULL);
        if (count > 0) {
            size_t k;
            char *keep[4];
            size_t nkeep = 0;

            for (line = msg; line != NULL && nkeep < 4; line = nl != NULL ? nl + 1 : NULL) {
                nl = strchr(line, '\n');
                if (nl != NULL) {
                    *nl = '\0';
                }
                keep[nkeep++] = line;
            }
            for (k = nkeep; k-- > 0;) {                      /* each goes to the front, so the list ends up in order */
                if (tcx_sl_add(&g->problems, keep[k]) != 0) {
                    return -1;
                }
                {
                    char *last = g->problems.v[g->problems.n - 1];

                    memmove(g->problems.v + 1, g->problems.v, (g->problems.n - 1) * sizeof *g->problems.v);
                    g->problems.v[0] = last;
                }
            }
        }
    }
    /* artwork ids in the order the database builder gives them: first seen */
    {
        char (*keys)[65] = calloc(g->ntracks ? g->ntracks : 1, sizeof *keys);
        size_t nk = 0, k;
        const uint32_t art_base = seed != NULL ? seed->db->max_id[13] : 0;      /* adding: the numbering continues after the drive's own artwork */

        if (keys == NULL) {
            return -1;
        }
        for (i = 0; i < g->ntracks; i++) {
            struct gtrack *t = &g->tracks[i];

            if (t->art_key[0]) {
                for (k = 0; k < nk && strcmp(keys[k], t->art_key) != 0; k++) {
                }
                if (k == nk) {
                    snprintf(keys[nk++], 65, "%s", t->art_key);
                }
                tc_artwork_path((uint32_t)(k + 1) + art_base, t->art_path, sizeof t->art_path);
            }
            t->plan.title = t->title;
            t->plan.artist = t->artist;
            t->plan.album = t->album;
            t->plan.genre = t->genre;
            t->plan.comment = t->comment;
            t->plan.file_name = t->file_name;
            t->plan.file_path = t->file_path;
            t->plan.audio_hash = t->audio_hash[0] ? t->audio_hash : NULL;
            t->plan.date_added = t->date;
            t->plan.artwork_key = t->art_key[0] ? t->art_key : NULL;
            t->plan.artwork_path = t->art_key[0] ? t->art_path : NULL;
            t->plan.sample_depth = t->plan.sample_depth ? t->plan.sample_depth : 16;
        }
        free(keys);
    }
    return 0;
}

uint64_t tcx_estimate_metadata(const struct gathered *g)
{
    uint64_t n = 400000;                                    /* database, settings, support files, pages */
    size_t i;

    for (i = 0; i < g->ntracks; i++) {
        const struct gtrack *t = &g->tracks[i];
        uint64_t cols = t->have_wave ? t->wave.columns : (uint64_t)(t->duration_ms * 0.15 + 1);

        n += cols * 3 + 7200 + 2000 + 16384 + (t->grid.bpm <= 0 ? 0 : (uint64_t)(t->duration_ms / 400.0) * 10) + (t->art_len ? 40000 : 0);
    }
    return n;
}

/* ---- staging and copying ---- */

int tcx_write_staged(const char *stage, const char *rel, const void *data, size_t len, char *err, size_t errsz)
{
    char full[PATH_MAX], dir[PATH_MAX], *slash;

    snprintf(full, sizeof full, "%s/%s", stage, rel);
    snprintf(dir, sizeof dir, "%s", full);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    }
    if (tc_mkdir_p(dir) != 0 || tc_write_atomic(full, data, len) != 0) {
        snprintf(err, errsz, "cannot write %s: %s", full, strerror(errno));
        return -1;
    }
    return 0;
}

int tcx_pick_staging_base(const struct tc_export_opts *o, char *out, size_t outsz, int *tmpfs, const struct tc_export_opts *warn_to)
{
    const char *x;

    *tmpfs = 0;
    if (o->staging_base != NULL && o->staging_base[0]) {
        snprintf(out, outsz, "%s", o->staging_base);
        return 0;
    }
    if (access("/dev/shm", W_OK) == 0) {
        snprintf(out, outsz, "/dev/shm");
        *tmpfs = 1;
        return 0;
    }
    x = getenv("XDG_RUNTIME_DIR");
    if (x != NULL && x[0] && access(x, W_OK) == 0) {
        snprintf(out, outsz, "%s", x);
        *tmpfs = 1;
        return 0;
    }
    if (warn_to->warn != NULL) {
        warn_to->warn(warn_to->ud, "no tmpfs available: staging in the system temp directory");
    }
    x = getenv("TMPDIR");
    snprintf(out, outsz, "%s", x != NULL && x[0] ? x : "/tmp");
    return 0;
}

int tcx_hex_digest_file(const char *path, char hex[65])
{
    return tc_sha256_file(path, hex);
}

/* link mode: a symbolic link to the real file */
int tcx_link_one(const char *dest, struct outfile *f, char *err, size_t errsz)
{
    char out[PATH_MAX], dir[PATH_MAX], real[PATH_MAX], *slash;

    snprintf(out, sizeof out, "%s/%s", dest, f->rel);
    snprintf(dir, sizeof dir, "%s", out);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    }
    if (tc_mkdir_p(dir) != 0 || realpath(f->src, real) == NULL || symlink(real, out) != 0) {
        snprintf(err, errsz, "cannot link %s: %s", out, strerror(errno));
        return -1;
    }
    return 0;
}

int tcx_copy_one(const char *dest, struct outfile *f, uint64_t *total, char *err, size_t errsz)
{
    char out[PATH_MAX], dir[PATH_MAX], *slash;
    int fd, in = -1, ok = 1;
    struct tc_sha256 sha;
    uint8_t digest[32];
    static uint8_t buf[1 << 20];
    size_t written = 0;
    char hex[65];
    int i;

    snprintf(out, sizeof out, "%s/%s", dest, f->rel);
    snprintf(dir, sizeof dir, "%s", out);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    }
    if (tc_mkdir_p(dir) != 0) {
        snprintf(err, errsz, "cannot create %s: %s", dir, strerror(errno));
        return -1;
    }
    fd = open(out, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        snprintf(err, errsz, "cannot create %s: %s", out, strerror(errno));
        return -1;
    }
    tc_sha256_init(&sha);
#define PUT(p, n) do { \
        size_t done_ = 0; \
        tc_sha256_update(&sha, (p), (n)); \
        while (done_ < (n)) { \
            ssize_t w_ = write(fd, (const uint8_t *)(p) + done_, (n) - done_); \
            if (w_ < 0) { if (errno == EINTR) continue; snprintf(err, errsz, "cannot write %s: %s", out, strerror(errno)); ok = 0; break; } \
            done_ += (size_t)w_; \
        } \
        written += (n); \
    } while (0)
    if (f->src == NULL) {
        PUT(f->mem, f->mem_len);
    } else {
        in = open(f->src, O_RDONLY);
        if (in < 0) {
            snprintf(err, errsz, "cannot read %s: %s", f->src, strerror(errno));
            ok = 0;
        }
        while (ok) {
            ssize_t r = read(in, buf, sizeof buf);
            size_t rn;

            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                snprintf(err, errsz, "read error on %s: %s", f->src, strerror(errno));
                ok = 0;
                break;
            }
            if (r == 0) {
                break;
            }
            rn = (size_t)r;
            PUT(buf, rn);
        }
        if (in >= 0) {
            close(in);
        }
    }
    if (ok && fsync(fd) != 0) {
        snprintf(err, errsz, "cannot sync %s: %s", out, strerror(errno));
        ok = 0;
    }
    close(fd);
    if (!ok) {
        return -1;
    }
    tc_sha256_final(&sha, digest);
    for (i = 0; i < 32; i++) {
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    }
    if (f->hash_known && strcmp(f->hash, hex) != 0) {
        snprintf(err, errsz, "source changed while copying: %s", f->rel);
        return -1;
    }
    memcpy(f->hash, hex, 65);
    f->hash_known = 1;
    *total += written;
    return 0;
#undef PUT
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* the regular files below dir as paths relative to it */
static int collect_files(const char *base, const char *rel, struct strlist *out)
{
    char dir[PATH_MAX];
    DIR *d;
    struct dirent *e;

    snprintf(dir, sizeof dir, "%s%s%s", base, rel[0] ? "/" : "", rel);
    d = opendir(dir);
    if (d == NULL) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char r[PATH_MAX], full[PATH_MAX];
        struct stat sb;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        snprintf(full, sizeof full, "%s/%s", base, r);
        if (stat(full, &sb) != 0) {
            continue;
        }
        if (S_ISDIR(sb.st_mode)) {
            if (collect_files(base, r, out) != 0) {
                closedir(d);
                return -1;
            }
        } else if (S_ISREG(sb.st_mode) && tcx_sl_add(out, r) != 0) {
            closedir(d);
            return -1;
        }
    }
    closedir(d);
    return 0;
}

/* Builds the analysis files and the artwork of the tracks of g (only those with only[i] set when `only` is given) in the staging folder. The analysis
 * files go to analyze_paths[i] (one per track of g, as the database names them); the paths of the .DAT files staged are appended to *anlz_rel, in track
 * order. 0, or -1 with the complete message in errout. */
int tcx_stage_track_files(const struct gathered *g, const struct tc_export_opts *o, char *const *analyze_paths, const char *only, const char *stage,
                          struct strlist *anlz_rel, char *errout, size_t errsz)
{
    char e[1024] = "";
    size_t i;

#define SFAIL(...) do { snprintf(errout, errsz, __VA_ARGS__); return -1; } while (0)
    for (i = 0; i < g->ntracks; i++) {
        struct gtrack *t = &g->tracks[i];
        struct tc_anlz_track d;
        struct tc_waveform w = t->wave, rendered;
        struct tc_anlz_beat *beats = NULL;
        uint16_t *sub = NULL, *flat5 = NULL;
        uint8_t *flat3 = NULL, *pwv4 = NULL, flat_pwav[TC_WAVE_PREVIEW_COLUMNS] = { 0 }, flat_pwv2[TC_WAVE_TINY_COLUMNS] = { 0 };
        size_t nbeats = 0;
        uint8_t *dat = NULL, *ext = NULL;
        size_t dat_len = 0, ext_len = 0;
        int have_rendered = 0, ok;
        char path[PATH_MAX];

        if (only != NULL && !only[i]) {
            continue;
        }
        memset(&rendered, 0, sizeof rendered);
        tcx_progress(o, "stage", (int)i, (int)g->ntracks, t->file_path);
        if (!t->have_wave) {                                /* allow_unanalysed: a flat waveform of the right length */
            size_t cols = (size_t)(t->duration_ms * TC_WAVE_COLUMNS_PER_SECOND / 1000.0) + 1;
            char m[300];

            memset(&w, 0, sizeof w);
            flat3 = calloc(cols, 1);
            flat5 = calloc(cols, sizeof *flat5);
            if (flat3 == NULL || flat5 == NULL) {
                free(flat3);
                free(flat5);
                SFAIL("out of memory");
            }
            w.pwv3 = flat3;
            w.pwv5 = flat5;
            w.columns = cols;
            memcpy(w.pwav, flat_pwav, sizeof w.pwav);
            memcpy(w.pwv2, flat_pwv2, sizeof w.pwv2);
            snprintf(m, sizeof m, "not analysed, flat waveform and no beat grid: %s", t->title);
            if (o->warn != NULL) {
                o->warn(o->ud, m);
            }
        }
        /* PWV4 comes from the true waveform: its layout is only partly understood, so it gets no spikes */
        pwv4 = o->include_pwv4 ? tc_anlz_pwv4_from(w.pwv5, w.columns) : NULL;
        if (o->cue.enabled && t->cues.n > 0) {
            if (tc_cuerender(&w, &t->cues, t->duration_ms, &o->cue, &rendered) != 0) {
                free(flat3);
                free(flat5);
                free(pwv4);
                SFAIL("out of memory");
            }
            have_rendered = 1;
        }
        if (tc_anlz_beats_from_grid(&t->grid, t->duration_ms, &beats, &sub, &nbeats) != 0) {
            free(flat3);
            free(flat5);
            free(pwv4);
            if (have_rendered) {
                tc_waveform_free(&rendered);
            }
            SFAIL("out of memory");
        }
        memset(&d, 0, sizeof d);
        d.audio_path = t->file_path;
        d.include_pvbr = o->include_pvbr;
        d.include_pwv5 = o->include_pwv5;
        d.beats = beats;
        d.nbeats = nbeats;
        d.beat_sub_us = sub;
        d.pwav = have_rendered ? rendered.pwav : w.pwav;
        d.pwv2 = have_rendered ? rendered.pwv2 : w.pwv2;
        d.pwv3 = have_rendered ? rendered.pwv3 : w.pwv3;
        d.pwv5 = have_rendered ? rendered.pwv5 : w.pwv5;
        d.columns = w.columns;
        d.pwv5_extra = w.pwv5_extra;
        d.pwv4 = pwv4;
        d.hot_cues = t->hot;
        d.nhot = t->nhot;
        ok = tc_anlz_build(&d, &dat, &dat_len, &ext, &ext_len, e, sizeof e) == 0;
        free(beats);
        free(sub);
        free(flat3);
        free(flat5);
        free(pwv4);
        if (have_rendered) {
            tc_waveform_free(&rendered);
        }
        if (!ok) {
            SFAIL("analysis files for %s: %s", t->title, e);
        }
        /* PIONEER/USBANLZ/Pxxx/yyyyyyyy/ANLZ0000.DAT */
        snprintf(path, sizeof path, "%s", analyze_paths[i] + 1);
        if (tcx_write_staged(stage, path, dat, dat_len, e, sizeof e) != 0) {
            free(dat);
            free(ext);
            SFAIL("staging: %s", e);
        }
        tcx_sl_add(anlz_rel, path);
        snprintf(path + strlen(path) - 3, 4, "EXT");
        if (tcx_write_staged(stage, path, ext, ext_len, e, sizeof e) != 0) {
            free(dat);
            free(ext);
            SFAIL("staging: %s", e);
        }
        free(dat);
        free(ext);
    }
    if (o->include_artwork) {
        struct strlist done_keys = { NULL, 0, 0 };

        for (i = 0; i < g->ntracks; i++) {
            struct gtrack *t = &g->tracks[i];
            uint8_t *jpeg;
            size_t jl, k;

            if ((only != NULL && !only[i]) || !t->art_key[0]) {
                continue;
            }
            for (k = 0; k < done_keys.n && strcmp(done_keys.v[k], t->art_key) != 0; k++) {
            }
            if (k < done_keys.n) {
                continue;
            }
            tcx_sl_add(&done_keys, t->art_key);
            if (tc_artwork_jpeg(t->art, t->art_len, TC_ARTWORK_SIZE, &jpeg, &jl, e, sizeof e) != 0) {
                tcx_sl_free(&done_keys);
                SFAIL("artwork of %s: %s", t->title, e);
            }
            if (tcx_write_staged(stage, t->art_path + 1, jpeg, jl, e, sizeof e) != 0) {
                free(jpeg);
                tcx_sl_free(&done_keys);
                SFAIL("staging: %s", e);
            }
            free(jpeg);
        }
        tcx_sl_free(&done_keys);
    }
    return 0;
#undef SFAIL
}

/* checks the staged .DAT and .EXT of one track: they parse, are whole and name the track's path. 0, or -1 with the message in err */
int tcx_verify_staged_anlz(const char *stage, const char *dat_rel, const char *file_path, char *err, size_t errsz)
{
    int e;

    for (e = 0; e < 2; e++) {
        char rel[PATH_MAX], full[PATH_MAX];
        anlz_t *a = NULL;
        const char *path = NULL;
        int good;

        snprintf(rel, sizeof rel, "%s", dat_rel);
        if (e == 1) {
            snprintf(rel + strlen(rel) - 3, 4, "EXT");
        }
        snprintf(full, sizeof full, "%s/%s", stage, rel);
        if (anlz_open_file(full, &a) != ANLZ_OK) {
            snprintf(err, errsz, "staged analysis file does not parse: %s", rel);
            return -1;
        }
        good = !anlz_is_damaged(a) && anlz_file_len_ok(a) && anlz_path(a, &path) == ANLZ_OK && strcmp(file_path, path) == 0;
        anlz_close(a);
        if (!good) {
            snprintf(err, errsz, "staged analysis file is wrong: %s", rel);
            return -1;
        }
    }
    return 0;
}

struct emit_ctx {
    char *msg;
    size_t cap;
};

static void emit_first(void *ud, const char *m)
{
    struct emit_ctx *c = ud;

    if (c->msg[0] == '\0') {
        snprintf(c->msg, c->cap, "%s", m);
    }
}

void tcx_progress(const struct tc_export_opts *o, const char *phase, int done, int total, const char *cur)
{
    if (o->progress != NULL) {
        o->progress(o->ud, phase, done, total, cur);
    }
}

#define FAIL(...) do { snprintf(res->error, sizeof res->error, __VA_ARGS__); rc = -1; goto out; } while (0)

int tc_export(const struct tc_export_list *lists, size_t nlists, const struct tc_export_opts *o, struct tc_export_result *res)
{
    struct gathered g;
    struct strlist staged = { NULL, 0, 0 }, anlz_rel = { NULL, 0, 0 };
    struct outfile *files = NULL;
    size_t nfiles = 0, i, cap = 0;
    char stage[PATH_MAX] = "", base[PATH_MAX], err[1024] = "", pdb_hash[65];
    struct tc_export_plan plan;
    struct tc_built_pdb built;
    struct outfile pdb_file, sums_file, manifest_file;
    char *sums = NULL, *manifest = NULL;
    const int sums_wanted = o->write_manifest && !o->link_audio;
    const int extra = o->write_manifest ? (sums_wanted ? 2 : 1) : 0;
    int rc = 0, dest_existed = 0, have_built = 0, wrote_any = 0, total = 0, done = 0;
    struct stat sb;
    struct strlist variant = { NULL, 0, 0 };
    struct tc_plan_track *plan_tracks = NULL;

    memset(res, 0, sizeof *res);
    memset(&built, 0, sizeof built);
    memset(&g, 0, sizeof g);
    memset(&pdb_file, 0, sizeof pdb_file);
    memset(&sums_file, 0, sizeof sums_file);
    memset(&manifest_file, 0, sizeof manifest_file);
    if (o->tpl == NULL || o->tpl_len == 0) {
        FAIL("no template export.pdb");
    }
    if (o->dest == NULL || o->dest[0] == '\0') {
        FAIL("no destination");
    }
    dest_existed = stat(o->dest, &sb) == 0;
    if (dest_existed) {
        if (!S_ISDIR(sb.st_mode)) {
            FAIL("the destination is not a folder: %s", o->dest);
        }
        {
            static const char *const mine[] = { "Contents", "PIONEER", "SHA256SUMS", "trakcrate-manifest.txt" };
            char names[256] = "";
            DIR *dd = opendir(o->dest);
            struct dirent *de;

            /* only the names this export writes matter; photos, .Trash-1000 and the like on a stick are left alone */
            while (dd != NULL && (de = readdir(dd)) != NULL) {
                size_t k;

                for (k = 0; k < sizeof mine / sizeof mine[0]; k++) {
                    if (strcasecmp(de->d_name, mine[k]) == 0 && strlen(names) + strlen(de->d_name) + 3 < sizeof names) {
                        strcat(names, names[0] ? ", " : "");
                        strcat(names, de->d_name);
                    }
                }
            }
            if (dd != NULL) {
                closedir(dd);
            }
            if (names[0]) {
                FAIL("the destination already has %s (an earlier export?), remove it or choose another folder: %s", names, o->dest);
            }
        }
    }
    if (tcx_gather(lists, nlists, o, NULL, &g) != 0) {
        FAIL("out of memory");
    }
    if (g.problems.n > 0) {
        size_t n = 0;

        n += (size_t)snprintf(res->error, sizeof res->error, "cannot export:");
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
        FAIL("the playlists are empty");
    }
    res->tracks = (int)g.ntracks;
    res->playlists = (int)g.nlists;
    for (i = 0; i < g.ntracks; i++) {
        res->audio_bytes += g.tracks[i].audio_bytes;
    }
    res->meta_bytes = tcx_estimate_metadata(&g);
    if (o->dry_run) {
        goto out;
    }

    /* space: the destination must hold audio and metadata */
    {
        char probe[PATH_MAX], *slash;
        struct statvfs sv;

        snprintf(probe, sizeof probe, "%s", o->dest);
        while (stat(probe, &sb) != 0 && (slash = strrchr(probe, '/')) != NULL) {
            if (slash == probe) {
                probe[1] = '\0';
                break;
            }
            *slash = '\0';
        }
        if (statvfs(probe[0] ? probe : ".", &sv) == 0) {
            uint64_t avail = (uint64_t)sv.f_bavail * sv.f_frsize, need = res->audio_bytes + res->meta_bytes + o->min_free_bytes;

            if (avail < need) {
                FAIL("not enough space on the destination: needs %llu MiB, %llu MiB free", (unsigned long long)(need >> 20),
                     (unsigned long long)(avail >> 20));
            }
        }
    }
    tcx_pick_staging_base(o, base, sizeof base, &res->staged_on_tmpfs, o);
    snprintf(stage, sizeof stage, "%s/trakcrate-export.XXXXXX", base);
    if (mkdtemp(stage) == NULL) {
        stage[0] = '\0';
        FAIL("cannot create the staging area: %s", strerror(errno));
    }
    {
        struct statvfs sv;

        if (statvfs(stage, &sv) == 0 && (uint64_t)sv.f_bavail * sv.f_frsize < res->meta_bytes + (8u << 20)) {
            FAIL("not enough space in the staging area %s", base);
        }
    }

    /* ---- stage: database, analysis files, artwork, settings */
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
    tcx_progress(o, "stage", 0, (int)g.ntracks, "export.pdb");
    if (tc_pdb_build(&plan, o->tpl, o->tpl_len, &built, err, sizeof err) != 0) {
        FAIL("database: %s", err);
    }
    have_built = 1;
    {
        struct tc_rbdb db;
        char first[600] = "";
        struct emit_ctx c = { first, sizeof first };
        size_t nd;

        if (tc_rbdb_load(built.bytes, built.len, &db, err, sizeof err) != 0) {
            FAIL("the database that was built cannot be read back: %s", err);
        }
        nd = tc_rbdb_compare_plan(&db, &plan, &built, emit_first, &c);
        tc_rbdb_free(&db);
        if (nd > 0) {
            FAIL("the database does not say what the plan says: %s", first);
        }
    }
    if (tcx_stage_track_files(&g, o, (char *const *)built.analyze_paths, NULL, stage, &anlz_rel, err, sizeof err) != 0) {
        snprintf(res->error, sizeof res->error, "%s", err);
        rc = -1;
        goto out;
    }
    if (o->include_settings) {
        size_t dl, ml;
        const uint8_t *dv = o->devsetting != NULL ? o->devsetting : tc_default_devsetting(&dl);
        const uint8_t *mv = o->mysetting != NULL ? o->mysetting : tc_default_mysetting(&ml);

        if (o->devsetting != NULL) {
            dl = o->dev_len;
        }
        if (o->mysetting != NULL) {
            ml = o->my_len;
        }
        if (tcx_write_staged(stage, "PIONEER/DEVSETTING.DAT", dv, dl, err, sizeof err) != 0 ||
            tcx_write_staged(stage, "PIONEER/MYSETTING.DAT", mv, ml, err, sizeof err) != 0) {
            FAIL("staging: %s", err);
        }
        {
            size_t l1, l2, l3;
            const uint8_t *dj = tc_default_djprofile(&l1), *rf = tc_default_rbfltr(&l2), *ee = tc_default_exportext(&l3);

            if (tcx_write_staged(stage, "PIONEER/djprofile.nxs", dj, l1, err, sizeof err) != 0 ||
                tcx_write_staged(stage, "PIONEER/rekordbox/RBFLTR.DAT", rf, l2, err, sizeof err) != 0 ||
                tcx_write_staged(stage, "PIONEER/rekordbox/exportExt.pdb", ee, l3, err, sizeof err) != 0) {
                FAIL("staging: %s", err);
            }
            {
                size_t l4;
                uint8_t *um = tc_empty_usbmng(&l4);
                int rc4;

                if (um == NULL) {
                    FAIL("out of memory");
                }
                rc4 = tcx_write_staged(stage, "PIONEER/USBANLZ/USBMNG.DAT", um, l4, err, sizeof err);
                free(um);
                if (rc4 != 0) {
                    FAIL("staging: %s", err);
                }
            }
        }
    }
    if (tcx_write_staged(stage, "PIONEER/rekordbox/export.pdb", built.bytes, built.len, err, sizeof err) != 0) {
        FAIL("staging: %s", err);
    }

    /* ---- verify the staged tree before anything touches the destination */
    for (i = 0; i < anlz_rel.n; i++) {
        if (tcx_verify_staged_anlz(stage, anlz_rel.v[i], g.tracks[i].file_path, err, sizeof err) != 0) {
            snprintf(res->error, sizeof res->error, "%s", err);
            rc = -1;
            goto out;
        }
    }
    if (o->include_settings) {
        const char *why = "";
        char full[PATH_MAX];
        uint8_t *b;
        size_t n;
        const char *names[2] = { "PIONEER/DEVSETTING.DAT", "PIONEER/MYSETTING.DAT" };
        int k;

        for (k = 0; k < 2; k++) {
            snprintf(full, sizeof full, "%s/%s", stage, names[k]);
            if (tc_settings_load(full, &b, &n, err, sizeof err) != 0) {
                FAIL("%s: %s", names[k], err);
            }
            free(b);
        }
        (void)why;
    }

    /* ---- the list of files to write, in order: audio, other metadata, checksums, export.pdb last */
#define ADD_FILE(f) do { \
        if (nfiles == cap) { size_t nc_ = cap ? cap * 2 : 64; struct outfile *nf_ = realloc(files, nc_ * sizeof *nf_); \
            if (nf_ == NULL) { FAIL("out of memory"); } files = nf_; cap = nc_; } \
        files[nfiles++] = (f); \
    } while (0)
    for (i = 0; i < g.ntracks; i++) {
        struct outfile f;

        memset(&f, 0, sizeof f);
        f.rel = strdup(g.tracks[i].file_path + 1);
        f.src = strdup(g.tracks[i].audio);
        f.audio = 1;
        if (f.rel == NULL || f.src == NULL) {
            free(f.rel);
            free(f.src);
            FAIL("out of memory");
        }
        ADD_FILE(f);
    }
    if (collect_files(stage, "", &staged) != 0) {
        FAIL("cannot list the staging area");
    }
    qsort(staged.v, staged.n, sizeof *staged.v, cmp_str);
    for (i = 0; i < staged.n; i++) {
        struct outfile f;
        char full[PATH_MAX];

        if (strcmp(staged.v[i], "PIONEER/rekordbox/export.pdb") == 0) {
            continue;
        }
        memset(&f, 0, sizeof f);
        f.rel = strdup(staged.v[i]);
        snprintf(full, sizeof full, "%s/%s", stage, staged.v[i]);
        f.src = strdup(full);
        if (f.rel == NULL || f.src == NULL) {
            free(f.rel);
            free(f.src);
            FAIL("out of memory");
        }
        if (tcx_hex_digest_file(full, f.hash) != 0) {
            free(f.rel);
            free(f.src);
            FAIL("cannot read %s", full);
        }
        f.hash_known = 1;
        ADD_FILE(f);
    }
    pdb_file.rel = strdup("PIONEER/rekordbox/export.pdb");
    {
        char full[PATH_MAX];

        snprintf(full, sizeof full, "%s/%s", stage, pdb_file.rel);
        pdb_file.src = strdup(full);
    }
    tc_sha256_hex(built.bytes, built.len, pdb_hash);
    memcpy(pdb_file.hash, pdb_hash, 65);
    pdb_file.hash_known = 1;
    if (pdb_file.rel == NULL || pdb_file.src == NULL) {
        FAIL("out of memory");
    }

    if (!o->include_settings) {
        tcx_sl_add(&variant, "no DEVSETTING.DAT / MYSETTING.DAT");
    }
    if (!o->include_artwork) {
        tcx_sl_add(&variant, "no artwork");
    }
    if (!o->include_pvbr) {
        tcx_sl_add(&variant, "no PVBR tag");
    }
    if (!o->include_pwv4) {
        tcx_sl_add(&variant, "no PWV4 tag");
    }
    if (!o->include_pwv5) {
        tcx_sl_add(&variant, "no PWV5 tag");
    }
    if (o->allow_unanalysed) {
        tcx_sl_add(&variant, "unanalysed tracks allowed");
    }
    total = (int)nfiles + 1 + extra;

    /* ---- write */
    if (!dest_existed && tc_mkdir_p(o->dest) != 0) {
        FAIL("cannot create %s: %s", o->dest, strerror(errno));
    }
    wrote_any = 1;
#define STEP(f) do { \
        tcx_progress(o, "copy", done, total, (f)->rel); \
        if (((o->link_audio && (f)->audio) ? tcx_link_one(o->dest, (f), err, sizeof err) : tcx_copy_one(o->dest, (f), &res->bytes_written, err, sizeof err)) != 0) { snprintf(res->error, sizeof res->error, "%s", err); rc = -1; goto out; } \
        done++; \
    } while (0)
    for (i = 0; i < nfiles; i++) {
        STEP(&files[i]);
    }
    if (o->write_manifest) {
        size_t sl = 0, scap = 0, mn;
        char date[16], head[512];

        for (i = 0; i <= nfiles; i++) {
            const struct outfile *f = i < nfiles ? &files[i] : &pdb_file;
            size_t need = sl + 64 + 2 + strlen(f->rel) + 2;

            if (need > scap) {
                char *ns = realloc(sums, need * 2);

                if (ns == NULL) {
                    FAIL("out of memory");
                }
                sums = ns;
                scap = need * 2;
            }
            sl += (size_t)snprintf(sums + sl, scap - sl, "%s  %s\n", f->hash, f->rel);
        }
        tcx_today(date, sizeof date);
        mn = strlen(date) + 4096;
        for (i = 0; i < g.nlists; i++) {
            mn += strlen(g.lists[i].name) + 4;
        }
        for (i = 0; i < variant.n; i++) {
            mn += strlen(variant.v[i]) + 8;
        }
        manifest = malloc(mn);
        if (manifest == NULL) {
            FAIL("out of memory");
        }
        snprintf(head, sizeof head, "trakcrate export\ndate: %s\ntracks: %d\nplaylists:", date, res->tracks);
        mn = (size_t)snprintf(manifest, mn, "%s", head);
        for (i = 0; i < g.nlists; i++) {
            mn += (size_t)sprintf(manifest + mn, " \"%s\"", g.lists[i].name);
        }
        mn += (size_t)sprintf(manifest + mn, "\nvariant: %s", variant.n == 0 ? "full" : "");
        for (i = 0; i < variant.n; i++) {
            mn += (size_t)sprintf(manifest + mn, "\n  - %s", variant.v[i]);
        }
        mn += (size_t)sprintf(manifest + mn,
                              "\nBytes that rekordbox files hold but trakcrate does not understand are copied from real drives or set to guessed values;\n"
                              "the list is doc/trakcrate/UNKNOWN_BYTES.md in the trakcrate sources.\n");
        sums_file.rel = strdup("SHA256SUMS");
        sums_file.mem = (uint8_t *)sums;
        sums_file.mem_len = sl;
        manifest_file.rel = strdup("trakcrate-manifest.txt");
        manifest_file.mem = (uint8_t *)manifest;
        manifest_file.mem_len = mn;
        if (sums_file.rel == NULL || manifest_file.rel == NULL) {
            FAIL("out of memory");
        }
        if (sums_wanted) {
            STEP(&sums_file);
        }
        STEP(&manifest_file);
    }
    STEP(&pdb_file);
#undef STEP
    {
        int fd = open(o->dest, O_RDONLY | O_DIRECTORY);

        if (fd >= 0) {
            syncfs(fd);
            close(fd);
        }
    }
    tcx_progress(o, "copy", done, total, "");
    res->files = (int)nfiles + 1 + extra;

    if (o->verify && !o->link_audio) {
        size_t k, count = nfiles + 1 + (o->write_manifest ? 2 : 0);

        for (k = 0; k < count; k++) {
            const struct outfile *f = k < nfiles ? &files[k] : k == nfiles ? &sums_file : k == nfiles + 1 && o->write_manifest ? &manifest_file : &pdb_file;
            char full[PATH_MAX], h[65];

            if (!o->write_manifest && k == nfiles) {
                f = &pdb_file;
            }
            tcx_progress(o, "verify", (int)k, (int)count, f->rel);
            snprintf(full, sizeof full, "%s/%s", o->dest, f->rel);
            if (tc_sha256_file(full, h) != 0 || strcmp(h, f->hash) != 0) {
                FAIL("verification failed for %s", f->rel);
            }
        }
        res->verified = 1;
    }

out:
    if (rc != 0 && wrote_any) {                             /* leave nothing half written behind: only what this export wrote */
        static const char *const mine[] = { "Contents", "PIONEER", "SHA256SUMS", "trakcrate-manifest.txt" };
        size_t k;

        for (k = 0; k < sizeof mine / sizeof mine[0]; k++) {
            char p[PATH_MAX];

            snprintf(p, sizeof p, "%s/%s", o->dest, mine[k]);
            tc_rm_rf(p);
        }
        if (!dest_existed) {
            rmdir(o->dest);
        }
    }
    if (stage[0]) {
        tc_rm_rf(stage);
    }
    for (i = 0; i < nfiles; i++) {
        free(files[i].rel);
        free(files[i].src);
    }
    free(files);
    free(pdb_file.rel);
    free(pdb_file.src);
    free(sums_file.rel);
    free(manifest_file.rel);
    free(sums);
    free(manifest);
    free(plan_tracks);
    if (have_built) {
        tc_built_pdb_free(&built);
    }
    tcx_sl_free(&staged);
    tcx_sl_free(&anlz_rel);
    tcx_sl_free(&variant);
    tcx_gathered_free(&g);
    return rc;
}
