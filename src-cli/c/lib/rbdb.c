#define _GNU_SOURCE
#include "rbdb.h"

#include <cdj/anlz.h>
#include <cdj/pdb.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fsutil.h"
#include "rbsettings.h"
#include "sha256.h"

#pragma GCC diagnostic ignored "-Wformat-truncation"

static char *dup0(const char *s)
{
    return strdup(s != NULL ? s : "");
}

/* ---- loading ---- */

static void *grow(void *p, size_t *cap, size_t n, size_t esz)
{
    if (n < *cap) {
        return p;
    }
    {
        size_t nc = *cap ? *cap * 2 : 64;
        void *np = realloc(p, nc * esz);

        if (np == NULL) {
            return NULL;
        }
        *cap = nc;
        return np;
    }
}

struct loader {
    struct tc_rbdb *db;
    size_t cap_tracks, cap_playlists, cap_entries;
    size_t cap_named[5];
    int oom;
};

static int cb_track(const pdb_track_t *t, void *ud)
{
    struct loader *l = ud;
    struct tc_rbdb_track *r;
    void *g = grow(l->db->tracks, &l->cap_tracks, l->db->ntracks, sizeof *l->db->tracks);

    if (g == NULL) {
        l->oom = 1;
        return 1;
    }
    l->db->tracks = g;
    r = &l->db->tracks[l->db->ntracks++];
    memset(r, 0, sizeof *r);
    r->id = t->id;
    r->sample_rate = t->sample_rate;
    r->file_size = t->file_size;
    r->artwork_id = t->artwork_id;
    r->label_id = t->label_id;
    r->bitrate = t->bitrate;
    r->track_number = t->track_number;
    r->bpm_x100 = t->bpm_x100;
    r->genre_id = t->genre_id;
    r->album_id = t->album_id;
    r->artist_id = t->artist_id;
    r->disc_number = t->disc_number;
    r->year = t->year;
    r->duration_secs = t->duration_secs;
    r->rating = t->rating;
    r->title = dup0(t->title);
    r->filename = dup0(t->filename);
    r->file_path = dup0(t->file_path);
    r->analyze_path = dup0(t->analyze_path);
    if (r->title == NULL || r->filename == NULL || r->file_path == NULL || r->analyze_path == NULL) {
        l->oom = 1;
        return 1;
    }
    return 0;
}

struct named_ctx {
    struct loader *l;
    struct tc_rbdb_named **list;
    size_t *n;
    size_t *cap;
};

static int add_named(struct named_ctx *c, uint32_t id, uint32_t artist, const char *name)
{
    void *g = grow(*c->list, c->cap, *c->n, sizeof **c->list);
    struct tc_rbdb_named *r;

    if (g == NULL) {
        c->l->oom = 1;
        return 1;
    }
    *c->list = g;
    r = &(*c->list)[(*c->n)++];
    r->id = id;
    r->artist_id = artist;
    r->name = dup0(name);
    if (r->name == NULL) {
        c->l->oom = 1;
        return 1;
    }
    return 0;
}

static int cb_named(const pdb_named_t *n, void *ud)
{
    struct named_ctx *c = ud;

    return add_named(c, n->id, 0, n->name);
}

static int cb_album(const pdb_album_t *a, void *ud)
{
    struct named_ctx *c = ud;

    return add_named(c, a->id, a->artist_id, a->name);
}

static int cb_playlist(const pdb_playlist_t *p, void *ud)
{
    struct loader *l = ud;
    struct tc_rbdb_playlist *r;
    void *g = grow(l->db->playlists, &l->cap_playlists, l->db->nplaylists, sizeof *l->db->playlists);

    if (g == NULL) {
        l->oom = 1;
        return 1;
    }
    l->db->playlists = g;
    r = &l->db->playlists[l->db->nplaylists++];
    r->id = p->id;
    r->parent_id = p->parent_id;
    r->sort_order = p->sort_order;
    r->is_folder = p->is_folder;
    r->name = dup0(p->name);
    if (r->name == NULL) {
        l->oom = 1;
        return 1;
    }
    return 0;
}

static int cb_entry(const pdb_playlist_entry_t *e, void *ud)
{
    struct loader *l = ud;
    void *g = grow(l->db->entries, &l->cap_entries, l->db->nentries, sizeof *l->db->entries);

    if (g == NULL) {
        l->oom = 1;
        return 1;
    }
    l->db->entries = g;
    l->db->entries[l->db->nentries].entry_index = e->entry_index;
    l->db->entries[l->db->nentries].track_id = e->track_id;
    l->db->entries[l->db->nentries].playlist_id = e->playlist_id;
    l->db->nentries++;
    return 0;
}

void tc_rbdb_free(struct tc_rbdb *db)
{
    size_t i;
    struct tc_rbdb_named *lists[5] = { db->artists, db->genres, db->labels, db->artwork, db->albums };
    size_t counts[5] = { db->nartists, db->ngenres, db->nlabels, db->nartwork, db->nalbums };
    int k;

    for (i = 0; i < db->ntracks; i++) {
        free(db->tracks[i].title);
        free(db->tracks[i].filename);
        free(db->tracks[i].file_path);
        free(db->tracks[i].analyze_path);
    }
    free(db->tracks);
    for (k = 0; k < 5; k++) {
        for (i = 0; i < counts[k]; i++) {
            free(lists[k][i].name);
        }
        free(lists[k]);
    }
    for (i = 0; i < db->nplaylists; i++) {
        free(db->playlists[i].name);
    }
    free(db->playlists);
    free(db->entries);
    memset(db, 0, sizeof *db);
}

int tc_rbdb_load(const uint8_t *data, size_t len, struct tc_rbdb *db, char *err, size_t errsz)
{
    struct loader l;
    struct named_ctx c;

    memset(db, 0, sizeof *db);
    memset(&l, 0, sizeof l);
    l.db = db;
    c.l = &l;
    if (len > UINT32_MAX || pdb_parse(data, (uint32_t)len, cb_track, &l) != 0 || l.oom) {
        snprintf(err, errsz, l.oom ? "out of memory" : "the track table cannot be read");
        tc_rbdb_free(db);
        return -1;
    }
    /* the other tables are optional: a table that cannot be read leaves its list empty, the comparison then reports it */
#define NAMED(L, N, FN, PARSER) \
    c.list = &db->L; c.n = &db->N; c.cap = &l.cap_named[FN]; \
    PARSER(data, (uint32_t)len, cb_named, &c);
    NAMED(artists, nartists, 0, pdb_parse_artists)
    NAMED(genres, ngenres, 1, pdb_parse_genres)
    NAMED(labels, nlabels, 2, pdb_parse_labels)
    NAMED(artwork, nartwork, 3, pdb_parse_artwork)
#undef NAMED
    c.list = &db->albums;
    c.n = &db->nalbums;
    c.cap = &l.cap_named[4];
    pdb_parse_albums(data, (uint32_t)len, cb_album, &c);
    pdb_parse_playlists(data, (uint32_t)len, cb_playlist, &l);
    pdb_parse_playlist_entries(data, (uint32_t)len, cb_entry, &l);
    if (l.oom) {
        snprintf(err, errsz, "out of memory");
        tc_rbdb_free(db);
        return -1;
    }
    return 0;
}

const struct tc_rbdb_named *tc_rbdb_find(const struct tc_rbdb_named *list, size_t n, uint32_t id)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (list[i].id == id) {
            return &list[i];
        }
    }
    return NULL;
}

/* ---- comparison with a plan ---- */

static size_t diff_count;

#define D(...) do { char m_[600]; snprintf(m_, sizeof m_, __VA_ARGS__); emit(ud, m_); n++; } while (0)

static const char *s(const char *p)
{
    return p != NULL ? p : "";
}

static int entry_cmp(const void *a, const void *b)
{
    const struct tc_rbdb_entry *x = a, *y = b;

    return x->entry_index < y->entry_index ? -1 : x->entry_index > y->entry_index;
}

size_t tc_rbdb_compare_plan(const struct tc_rbdb *db, const struct tc_export_plan *plan, const struct tc_built_pdb *built, tc_emit_fn emit, void *ud)
{
    size_t n = 0, i, k, m;

    diff_count = 0;
    if (db->ntracks != plan->ntracks) {
        D("track count %zu, plan has %zu", db->ntracks, plan->ntracks);
    }
    m = db->ntracks < plan->ntracks ? db->ntracks : plan->ntracks;
    for (i = 0; i < m; i++) {
        const struct tc_plan_track *p = &plan->tracks[i];
        const struct tc_rbdb_track *t = &db->tracks[i];

        if (t->id != i + 1) D("track %zu: id %u", i + 1, t->id);
        if (strcmp(t->title, s(p->title)) != 0) D("track %zu: title \"%s\" instead of \"%s\"", i + 1, t->title, s(p->title));
        if (strcmp(t->filename, s(p->file_name)) != 0) D("track %zu: file name \"%s\"", i + 1, t->filename);
        if (strcmp(t->file_path, s(p->file_path)) != 0) D("track %zu: file path \"%s\"", i + 1, t->file_path);
        if (i < (size_t)plan->ntracks && built->analyze_paths[i] != NULL && strcmp(t->analyze_path, built->analyze_paths[i]) != 0) {
            D("track %zu: analyze path \"%s\"", i + 1, t->analyze_path);
        }
        if (t->bpm_x100 != p->bpm_x100) D("track %zu: bpm", i + 1);
        if (t->duration_secs != (p->duration_secs & 0xFFFF)) D("track %zu: duration", i + 1);
        if (t->rating != p->rating) D("track %zu: rating", i + 1);
        if (t->year != (p->year & 0xFFFF)) D("track %zu: year", i + 1);
        if (t->file_size != p->file_size) D("track %zu: file size", i + 1);
        if (t->sample_rate != p->sample_rate) D("track %zu: sample rate", i + 1);
        if (t->bitrate != p->bitrate_kbps) D("track %zu: bitrate", i + 1);
        if (t->track_number != p->track_number) D("track %zu: track number", i + 1);
        if (t->disc_number != (p->disc_number & 0xFFFF)) D("track %zu: disc number", i + 1);
        {
            struct { const struct tc_rbdb_named *list; size_t cnt; uint32_t id; const char *want; const char *what; } c[3] = {
                { db->artists, db->nartists, t->artist_id, p->artist, "artist" },
                { db->genres, db->ngenres, t->genre_id, p->genre, "genre" },
                { db->labels, db->nlabels, t->label_id, p->label, "label" },
            };
            int j;

            for (j = 0; j < 3; j++) {
                const struct tc_rbdb_named *f;

                if (s(c[j].want)[0] == '\0') {
                    if (c[j].id != 0) D("track %zu: %s set but the plan has none", i + 1, c[j].what);
                } else if ((f = tc_rbdb_find(c[j].list, c[j].cnt, c[j].id)) == NULL) {
                    D("track %zu: %s id %u not found", i + 1, c[j].what, c[j].id);
                } else if (strcmp(f->name, c[j].want) != 0) {
                    D("track %zu: %s \"%s\" instead of \"%s\"", i + 1, c[j].what, f->name, c[j].want);
                }
            }
        }
        if (s(p->album)[0] == '\0') {
            if (t->album_id != 0) D("track %zu: album set but the plan has none", i + 1);
        } else {
            const struct tc_rbdb_named *a = tc_rbdb_find(db->albums, db->nalbums, t->album_id);

            if (a == NULL) {
                D("track %zu: album id not found", i + 1);
            } else {
                if (strcmp(a->name, p->album) != 0) D("track %zu: album \"%s\"", i + 1, a->name);
                if (a->artist_id != t->artist_id) D("track %zu: album belongs to another artist", i + 1);
            }
        }
        if (s(p->artwork_key)[0] == '\0') {
            if (t->artwork_id != 0) D("track %zu: artwork set but the plan has none", i + 1);
        } else {
            const struct tc_rbdb_named *a = tc_rbdb_find(db->artwork, db->nartwork, t->artwork_id);

            if (a == NULL) D("track %zu: artwork id not found", i + 1);
            else if (strcmp(a->name, s(p->artwork_path)) != 0) D("track %zu: artwork path \"%s\"", i + 1, a->name);
        }
    }
    if (db->nplaylists != plan->nplaylists) {
        D("playlist count %zu", db->nplaylists);
    }
    m = db->nplaylists < plan->nplaylists ? db->nplaylists : plan->nplaylists;
    for (i = 0; i < m; i++) {
        const struct tc_rbdb_playlist *pl = &db->playlists[i];
        struct tc_rbdb_entry *got = calloc(db->nentries ? db->nentries : 1, sizeof *got);
        size_t ng = 0;

        if (strcmp(pl->name, s(plan->playlists[i].name)) != 0) D("playlist %zu is called \"%s\"", i + 1, pl->name);
        if (pl->parent_id != 0 || pl->is_folder) D("playlist %zu is not at the top level", i + 1);
        if (got == NULL) {
            D("out of memory");
            continue;
        }
        for (k = 0; k < db->nentries; k++) {
            if (db->entries[k].playlist_id == pl->id) {
                got[ng++] = db->entries[k];
            }
        }
        qsort(got, ng, sizeof *got, entry_cmp);
        if (ng != plan->playlists[i].ntracks) {
            D("playlist \"%s\" has %zu entries", s(plan->playlists[i].name), ng);
        } else {
            for (k = 0; k < ng; k++) {
                if (got[k].track_id != plan->playlists[i].tracks[k] + 1) {
                    D("playlist \"%s\" entry %zu is track %u", s(plan->playlists[i].name), k + 1, got[k].track_id);
                }
            }
        }
        free(got);
    }
    if (db->nartwork != built->nartwork) {
        D("artwork count %zu", db->nartwork);
    }
    return n;
}
#undef D

/* ---- drive check ---- */

struct vctx {
    const char *root;
    tc_finding_fn emit;
    void *ud;
    struct tc_verify_stats *st;
};

static void find(struct vctx *c, const char *sev, const char *where, const char *code, const char *fmt, ...) __attribute__((format(printf, 5, 6)));

#include <stdarg.h>

static void find(struct vctx *c, const char *sev, const char *where, const char *code, const char *fmt, ...)
{
    char msg[600];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (strcmp(sev, "error") == 0) {
        c->st->errors++;
    } else {
        c->st->warnings++;
    }
    c->emit(c->ud, sev, where, code, msg);
}

static int is_file(const char *path)
{
    struct stat sb;

    return stat(path, &sb) == 0 && S_ISREG(sb.st_mode);
}

static int join(char *out, size_t sz, const char *a, const char *b)
{
    return snprintf(out, sz, "%s%s%s", a, (b[0] == '/' || a[0] == '\0') ? "" : "/", b) < (int)sz;
}

/* an ANLZ file: parses, not damaged, the length field agrees, and (when `want_path` is set) it names the right audio file */
static void check_anlz(struct vctx *c, const char *rel, const char *want_path, int required)
{
    char full[PATH_MAX];
    anlz_t *a = NULL;
    const char *path = NULL;

    if (!join(full, sizeof full, c->root, rel[0] == '/' ? rel + 1 : rel)) {
        return;
    }
    if (!is_file(full)) {
        find(c, required ? "error" : "warning", rel, "anlz-missing", "analysis file does not exist");
        return;
    }
    c->st->files_checked++;
    if (anlz_open_file(full, &a) != ANLZ_OK) {
        find(c, "error", rel, "anlz-unreadable", "analysis file does not parse");
        return;
    }
    if (anlz_is_damaged(a) || !anlz_file_len_ok(a)) {
        find(c, "error", rel, "anlz-damaged", "analysis file is damaged or its length field is wrong");
    } else if (anlz_path(a, &path) != ANLZ_OK) {
        find(c, "error", rel, "anlz-no-path", "analysis file has no PPTH tag");
    } else if (strcmp(path, want_path) != 0) {
        find(c, "error", rel, "anlz-path-mismatch", "analysis file names %s, the database says %s", path, want_path);
    }
    anlz_close(a);
}

static void check_settings(struct vctx *c, const char *rel)
{
    char full[PATH_MAX], err[300];
    uint8_t *b;
    size_t n;
    const char *why;

    if (!join(full, sizeof full, c->root, rel) || !is_file(full)) {
        return;
    }
    c->st->files_checked++;
    if (tc_settings_load(full, &b, &n, err, sizeof err) != 0) {
        why = strrchr(err, ':') ? strrchr(err, ':') + 2 : err;
        find(c, "error", rel, "settings-bad", "%s", why);
    } else {
        free(b);
    }
}

/* SHA256SUMS lines "hash  path" */
static void check_sums(struct vctx *c)
{
    char full[PATH_MAX], *data, *line, *save = NULL;
    size_t n;

    if (!join(full, sizeof full, c->root, "SHA256SUMS") || !is_file(full) || tc_read_file(full, &data, &n) != 0) {
        return;
    }
    for (line = strtok_r(data, "\n", &save); line != NULL; line = strtok_r(NULL, "\n", &save)) {
        char want[65] = "", got[65], p[PATH_MAX];
        const char *rel;

        if (strlen(line) < 66 || line[64] != ' ') {
            find(c, "warning", "SHA256SUMS", "sums-line", "cannot read the line: %.80s", line);
            continue;
        }
        memcpy(want, line, 64);
        want[64] = '\0';
        rel = line + 65;
        while (*rel == ' ' || *rel == '*') {
            rel++;
        }
        if (strstr(rel, "..") != NULL || !join(p, sizeof p, c->root, rel)) {
            find(c, "warning", "SHA256SUMS", "sums-path", "path not checked: %.80s", rel);
            continue;
        }
        c->st->files_checked++;
        if (tc_sha256_file(p, got) != 0) {
            find(c, "error", rel, "checksum-missing", "listed in SHA256SUMS but cannot be read");
        } else if (strcmp(got, want) != 0) {
            find(c, "error", rel, "checksum-mismatch", "SHA-256 differs from SHA256SUMS");
        }
    }
    free(data);
}

int tc_verify_drive(const char *root, tc_finding_fn emit, void *ud, struct tc_verify_stats *stats)
{
    struct vctx c = { root, emit, ud, stats };
    char pdb[PATH_MAX], err[300], full[PATH_MAX];
    char *bytes;
    size_t len, i, k;
    struct tc_rbdb db;
    uint32_t pid = 0;
    (void)pid;

    memset(stats, 0, sizeof *stats);
    if (!join(pdb, sizeof pdb, root, "PIONEER/rekordbox/export.pdb") || tc_read_file(pdb, &bytes, &len) != 0) {
        find(&c, "error", "PIONEER/rekordbox/export.pdb", "pdb-missing", "export.pdb cannot be read: %s", strerror(errno));
        return 1;
    }
    stats->files_checked++;
    {
        struct tc_pdb f;
        char perr[200];

        if (tc_pdb_parse((uint8_t *)bytes, len, &f, perr, sizeof perr) != 0) {
            find(&c, "error", "PIONEER/rekordbox/export.pdb", "pdb-structure", "%s", perr);
            free(bytes);
            return 1;
        }
        tc_pdb_free(&f);
    }
    if (tc_rbdb_load((uint8_t *)bytes, len, &db, err, sizeof err) != 0) {
        find(&c, "error", "PIONEER/rekordbox/export.pdb", "pdb-unreadable", "%s", err);
        free(bytes);
        return 1;
    }
    free(bytes);
    stats->tracks = db.ntracks;
    stats->playlists = db.nplaylists;
    for (i = 0; i < db.ntracks; i++) {
        const struct tc_rbdb_track *t = &db.tracks[i];
        char where[160];

        snprintf(where, sizeof where, "track %u", t->id);
        if (t->title[0] == '\0') {
            find(&c, "warning", where, "track-no-title", "the track has no title");
        }
        if (t->file_path[0] == '\0' || !join(full, sizeof full, root, t->file_path)) {
            find(&c, "error", where, "audio-path", "the track has no usable file path");
        } else if (!is_file(full)) {
            find(&c, "error", t->file_path, "audio-missing", "audio file of track %u (%s) does not exist", t->id, t->title);
        } else {
            struct stat sb;

            stats->files_checked++;
            if (stat(full, &sb) == 0 && (uint64_t)sb.st_size != t->file_size) {
                find(&c, "warning", t->file_path, "audio-size", "file size %llu differs from the database (%u)", (unsigned long long)sb.st_size, t->file_size);
            }
        }
        if (t->analyze_path[0] == '\0') {
            find(&c, "error", where, "anlz-path", "the track has no analysis path");
        } else {
            char ext[PATH_MAX];
            size_t l = strlen(t->analyze_path);

            check_anlz(&c, t->analyze_path, t->file_path, 1);
            if (l > 3 && l < sizeof ext) {
                snprintf(ext, sizeof ext, "%.*sEXT", (int)(l - 3), t->analyze_path);
                check_anlz(&c, ext, t->file_path, 0);
            }
        }
        if (t->artwork_id != 0) {
            const struct tc_rbdb_named *a = tc_rbdb_find(db.artwork, db.nartwork, t->artwork_id);

            if (a == NULL) {
                find(&c, "error", where, "artwork-row", "artwork id %u has no row", t->artwork_id);
            } else if (!join(full, sizeof full, root, a->name) || !is_file(full)) {
                find(&c, "error", a->name, "artwork-missing", "artwork file does not exist");
            }
        }
    }
    for (i = 0; i < db.nentries; i++) {
        const struct tc_rbdb_entry *e = &db.entries[i];
        int have_pl = 0;
        char where[96];

        for (k = 0; k < db.nplaylists && !have_pl; k++) {
            have_pl = db.playlists[k].id == e->playlist_id;
        }
        snprintf(where, sizeof where, "playlist %u", e->playlist_id);
        if (!have_pl) {
            find(&c, "error", where, "entry-playlist", "an entry belongs to a playlist that does not exist");
        }
        for (k = 0; k < db.ntracks && db.tracks[k].id != e->track_id; k++) {
        }
        if (k == db.ntracks) {
            find(&c, "error", where, "entry-track", "entry %u refers to track %u, which does not exist", e->entry_index, e->track_id);
        }
    }
    check_settings(&c, "PIONEER/DEVSETTING.DAT");
    check_settings(&c, "PIONEER/MYSETTING.DAT");
    check_sums(&c);
    tc_rbdb_free(&db);
    return stats->errors ? 1 : 0;
}
