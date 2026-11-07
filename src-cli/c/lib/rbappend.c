/*
 * Adding a playlist to an existing export.pdb: the C port of src/trakcrate/pdb_append.cpp. Keep the two in step; src-cli/test/test_append_oracle.sh
 * requires byte identical databases from both.
 */
#define _GNU_SOURCE
#include "rbappend.h"

#include <cdj/pdb.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"

#define K_PAGE TC_PDB_PAGE_SIZE
#define K_HEAD TC_PDB_PAGE_HEADER
#define K_PER TC_PDB_ROWS_PER_GROUP
#define K_MAX_BYTES ((size_t)256 << 20)
#define K_MAX_PROBLEMS 40
#define K_TABLE_COUNT 20
#define K_EMPTY_ENTRY 0x03ffffffu

/* ---- small helpers ---- */

static void *grow(void *v, size_t *cap, size_t n, size_t elem)
{
    size_t nc;
    void *nv;

    if (n < *cap) {
        return v;
    }
    nc = *cap ? *cap * 2 : 16;
    nv = realloc(v, nc * elem);
    if (nv == NULL) {
        return NULL;
    }
    *cap = nc;
    return nv;
}

static char *lower_dup(const char *s)
{
    char *r = strdup(s);
    size_t i;

    for (i = 0; r != NULL && r[i] != '\0'; i++) {
        r[i] = (char)tolower((unsigned char)r[i]);
    }
    return r;
}

static uint32_t rd32(const uint8_t *b, size_t at)
{
    return tc_get_le32(b + at);
}

static unsigned rd16(const uint8_t *b, size_t at)
{
    return tc_get_le16(b + at);
}

void tc_problems_free(char **problems, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        free(problems[i]);
    }
    free(problems);
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

    return x < y ? -1 : x > y;
}

/* ---- the checker ---- */

struct checker {
    char **problems;
    size_t n, cap;
    int failed, want, oom;
};

static void problem(struct checker *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void problem(struct checker *c, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    c->failed = 1;
    if (!c->want || c->n >= K_MAX_PROBLEMS) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    c->problems = grow(c->problems, &c->cap, c->n, sizeof *c->problems);
    if (c->problems == NULL) {
        c->oom = 1;
        return;
    }
    c->problems[c->n] = strdup(buf);
    if (c->problems[c->n] != NULL) {
        c->n++;
    }
}

/* ids of the present rows of a table, sorted, to find duplicates */
struct idset {
    uint32_t *v;
    size_t n, cap;
};

/* records the id; 0 when it is a present row's id seen before. Max over every row, present or deleted. */
static int idset_add(struct idset *s, uint32_t id, int present, uint32_t *max_id)
{
    size_t lo = 0, hi = s->n;

    if (id > *max_id) {
        *max_id = id;
    }
    if (!present) {
        return 1;
    }
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;

        if (s->v[mid] < id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < s->n && s->v[lo] == id) {
        return 0;
    }
    s->v = grow(s->v, &s->cap, s->n, sizeof *s->v);
    if (s->v == NULL) {
        s->n = 0;
        return 1;
    }
    memmove(s->v + lo + 1, s->v + lo, (s->n - lo) * sizeof *s->v);
    s->v[lo] = id;
    s->n++;
    return 1;
}

static int idset_has(const struct idset *s, uint32_t id)
{
    return s->n > 0 && bsearch(&id, s->v, s->n, sizeof *s->v, cmp_u32) != NULL;
}

static void idset_free(struct idset *s)
{
    free(s->v);
    memset(s, 0, sizeof *s);
}

/* The data pages of a table in chain order. The chain starts at an index page, stays inside the file, has no loop and ends at the table's empty candidate
 * page (a blank page, or a page number past the end of the file); the table entry's last page is the last data page. */
static void walk_table(const struct tc_pdb *f, const struct tc_pdb_table *t, struct checker *c, uint32_t **data, size_t *ndata)
{
    size_t n = f->npages, cap = 0;
    uint32_t cur, last_data;
    unsigned char *seen;

    *data = NULL;
    *ndata = 0;
    if (t->first_page == 0 || t->first_page >= n) {
        problem(c, "table %u: the first page %u is outside the file", t->type, t->first_page);
        return;
    }
    if (f->pages[t->first_page].structured || !(f->pages[t->first_page].flags & 0x40)) {
        problem(c, "table %u: the first page %u is not an index page", t->type, t->first_page);
        return;
    }
    if (t->empty_candidate == 0) {
        problem(c, "table %u: no empty candidate page", t->type);
        return;
    }
    if (t->empty_candidate < n) {
        const struct tc_pdb_page *e = &f->pages[t->empty_candidate];

        if (e->structured || e->flags != 0 || e->index != 0) {
            problem(c, "table %u: the empty candidate page %u is not blank", t->type, t->empty_candidate);
        }
    }
    seen = calloc(n, 1);
    if (seen == NULL) {
        c->oom = 1;
        return;
    }
    seen[t->first_page] = 1;
    cur = t->first_page;
    for (;;) {
        uint32_t next = f->pages[cur].next;
        const struct tc_pdb_page *p;

        if (next == t->empty_candidate) {
            break;
        }
        if (next == 0 || next >= n) {
            problem(c, "table %u: the chain ends at page %u before the empty candidate page %u", t->type, cur, t->empty_candidate);
            free(seen);
            return;
        }
        if (seen[next]) {
            problem(c, "table %u: the chain loops at page %u", t->type, next);
            free(seen);
            return;
        }
        seen[next] = 1;
        p = &f->pages[next];
        if (!p->structured) {
            problem(c, "table %u: page %u of the chain is not a data page the model understands", t->type, next);
            free(seen);
            return;
        }
        if (p->type != t->type) {
            problem(c, "table %u: page %u has the type of table %u", t->type, next, p->type);
        }
        if (p->index != next) {
            problem(c, "table %u: page %u says it is page %u", t->type, next, p->index);
        }
        *data = grow(*data, &cap, *ndata, sizeof **data);
        if (*data == NULL) {
            c->oom = 1;
            free(seen);
            return;
        }
        (*data)[(*ndata)++] = next;
        cur = next;
    }
    free(seen);
    last_data = *ndata == 0 ? t->first_page : (*data)[*ndata - 1];
    if (t->last_page != last_data) {
        problem(c, "table %u: the table says its last page is %u but the chain ends at %u", t->type, t->last_page, last_data);
    }
}

/* ---- the database model ---- */

static void warn_add(struct tc_drive_db *db, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void warn_add(struct tc_drive_db *db, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    char **nw;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    nw = realloc(db->warnings, (db->nwarnings + 1) * sizeof *nw);
    if (nw == NULL) {
        return;
    }
    db->warnings = nw;
    db->warnings[db->nwarnings] = strdup(buf);
    if (db->warnings[db->nwarnings] != NULL) {
        db->nwarnings++;
    }
}

static void free_names(struct tc_name_id *v, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        free(v[i].name);
    }
    free(v);
}

static void free_strs(char **v, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        free(v[i]);
    }
    free(v);
}

void tc_drive_db_free(struct tc_drive_db *db)
{
    size_t i;

    tc_pdb_free(&db->file);
    free_names(db->artist, db->nartist);
    free_names(db->genre, db->ngenre);
    free_names(db->label, db->nlabel);
    free_names(db->artwork, db->nartwork);
    free_names(db->album, db->nalbum);
    for (i = 0; i < db->ntracks; i++) {
        free(db->tracks[i].title);
        free(db->tracks[i].file_path);
        free(db->tracks[i].analyze_path);
    }
    free(db->tracks);
    free_strs(db->track_lower, db->ntracks);
    free_strs(db->analyze_paths, db->nanalyze);
    free_strs(db->playlist_lower, db->nplaylists);
    free(db->playlist_id);
    free_strs(db->warnings, db->nwarnings);
    memset(db, 0, sizeof *db);
}

/* (artist, name) order */
static int cmp_name_id(const void *a, const void *b)
{
    const struct tc_name_id *x = a, *y = b;
    int r;

    if (x->artist != y->artist) {
        return x->artist < y->artist ? -1 : 1;
    }
    r = strcmp(x->name, y->name);
    if (r != 0) {
        return r;
    }
    return x->id < y->id ? -1 : x->id > y->id;
}

/* sorts by (artist, name, id) and keeps the lowest id of each (artist, name) */
static void dedupe_names(struct tc_name_id *v, size_t *n)
{
    size_t i, o = 0;

    if (*n == 0) {
        return;
    }
    qsort(v, *n, sizeof *v, cmp_name_id);
    for (i = 0; i < *n; i++) {
        if (o > 0 && v[o - 1].artist == v[i].artist && strcmp(v[o - 1].name, v[i].name) == 0) {
            free(v[i].name);
            continue;
        }
        v[o++] = v[i];
    }
    *n = o;
}

const struct tc_name_id *tc_name_find(const struct tc_name_id *v, size_t n, uint32_t artist, const char *name)
{
    struct tc_name_id key;

    key.name = (char *)name;
    key.artist = artist;
    key.id = 0;
    /* ids are ignored: the lowest id of a pair is the only one kept */
    {
        size_t lo = 0, hi = n;

        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            int r = cmp_name_id(&v[mid], &key);

            if (v[mid].artist == artist && strcmp(v[mid].name, name) == 0) {
                return &v[mid];
            }
            if (r < 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
    }
    return NULL;
}

static int name_add(struct tc_name_id **v, size_t *n, size_t *cap, uint32_t artist, uint32_t id, char *name)
{
    struct tc_name_id *g = grow(*v, cap, *n, sizeof **v);

    if (g == NULL) {
        free(name);
        return -1;
    }
    *v = g;
    g[*n].name = name;
    g[*n].artist = artist;
    g[*n].id = id;
    (*n)++;
    return 0;
}

struct entry3 {
    uint32_t index, track, playlist;
};

int tc_drive_db_load(const uint8_t *bytes, size_t len, struct tc_drive_db *out, char ***problems, size_t *nproblems)
{
    struct checker c;
    const struct tc_pdb *f;
    uint32_t **data = NULL;
    size_t *ndata = NULL;
    size_t t, i, ntracks_cap = 0, nan_cap = 0, npl_cap = 0;
    size_t acap = 0, gcap = 0, lcap = 0, wcap = 0, alcap = 0, nentries = 0, entries_cap = 0;
    struct idset track_ids, artist_ids, album_ids, genre_ids, label_ids, artwork_ids, playlist_ids;
    struct entry3 *entries = NULL;
    uint32_t max_candidate = 0;
    char perr[300];
    int ok = 0;

    memset(&c, 0, sizeof c);
    c.want = problems != NULL;
    memset(out, 0, sizeof *out);
    memset(&track_ids, 0, sizeof track_ids);
    memset(&artist_ids, 0, sizeof artist_ids);
    memset(&album_ids, 0, sizeof album_ids);
    memset(&genre_ids, 0, sizeof genre_ids);
    memset(&label_ids, 0, sizeof label_ids);
    memset(&artwork_ids, 0, sizeof artwork_ids);
    memset(&playlist_ids, 0, sizeof playlist_ids);
    if (problems != NULL) {
        *problems = NULL;
        *nproblems = 0;
    }
    if (len > K_MAX_BYTES) {
        problem(&c, "export.pdb is %zu bytes, more than the %zu byte limit", len, K_MAX_BYTES);
        goto done;
    }
    if (tc_pdb_parse(bytes, len, &out->file, perr, sizeof perr) != 0) {
        problem(&c, "not an export.pdb this program can read: %s", perr);
        goto done;
    }
    f = &out->file;
    if (f->ntables != K_TABLE_COUNT) {
        problem(&c, "the database has %zu tables, not %d", f->ntables, K_TABLE_COUNT);
        goto done;
    }
    data = calloc(f->ntables, sizeof *data);
    ndata = calloc(f->ntables, sizeof *ndata);
    if (data == NULL || ndata == NULL) {
        c.oom = 1;
        goto done;
    }
    for (t = 0; t < f->ntables; t++) {
        const struct tc_pdb_table *tb = &f->tables[t];
        size_t u;

        for (u = 0; u < t; u++) {
            if (f->tables[u].type == tb->type) {
                problem(&c, "table type %u appears twice in the header", tb->type);
            }
        }
        if (tb->type >= TC_DRIVE_MAX_TABLE_TYPE) {
            problem(&c, "table type %u is not one this program knows", tb->type);
            continue;
        }
        walk_table(f, tb, &c, &data[t], &ndata[t]);
        if (tb->empty_candidate > max_candidate) {
            max_candidate = tb->empty_candidate;
        }
    }
    if (f->next_unused_page < f->npages) {
        problem(&c, "next unused page %u is inside the file (%zu pages)", f->next_unused_page, f->npages);
    }
    if (f->next_unused_page <= max_candidate) {
        problem(&c, "next unused page %u is not past the empty candidate pages (%u)", f->next_unused_page, max_candidate);
    }
    if (c.failed) {
        goto done;                                      /* the rows cannot be trusted when the chains are not */
    }
    for (t = 0; t < f->ntables; t++) {
        for (i = 0; i < ndata[t]; i++) {
            const struct tc_pdb_page *p = &f->pages[data[t][i]];

            if (p->u10 > f->sequence) {
                warn_add(out, "page %u has write counter %u, past the header's sequence %u", data[t][i], p->u10, f->sequence);
            }
        }
    }

    for (t = 0; t < f->ntables; t++) {
        const uint32_t type = f->tables[t].type;

        for (i = 0; i < ndata[t]; i++) {
            const uint32_t pg = data[t][i];
            const struct tc_pdb_page *page = &f->pages[pg];
            size_t slot;

            for (slot = 0; slot < page->nrows; slot++) {
                const struct tc_pdb_row *r = &page->rows[slot];
                const uint8_t *b = r->bytes;
                const size_t bl = r->len;
                char at[96];

                snprintf(at, sizeof at, "table %u, page %u, row %zu: ", type, pg, slot);
                if (c.oom) {
                    goto done;
                }
                switch (type) {
                case 0: {                                                   /* tracks */
                    struct tc_drive_track tr;
                    char *file_name = NULL;
                    int sok;
                    size_t k;

                    if (bl < (size_t)(TC_TRACK_FIXED + TC_TRACK_SLOTS * 2)) {
                        problem(&c, "%sa track row of %zu bytes is too short", at, bl);
                        break;
                    }
                    if (rd16(b, 0) != 0x24) {
                        problem(&c, "%sunknown track row subtype %u", at, rd16(b, 0));
                        break;
                    }
                    memset(&tr, 0, sizeof tr);
                    tr.id = rd32(b, 72);
                    tr.file_size = rd32(b, 16);
                    sok = tc_devsql_decode(b, bl, rd16(b, TC_TRACK_FIXED + 2 * 14), &tr.analyze_path, NULL) == 0 &&
                          tc_devsql_decode(b, bl, rd16(b, TC_TRACK_FIXED + 2 * 17), &tr.title, NULL) == 0 &&
                          tc_devsql_decode(b, bl, rd16(b, TC_TRACK_FIXED + 2 * 19), &file_name, NULL) == 0 &&
                          tc_devsql_decode(b, bl, rd16(b, TC_TRACK_FIXED + 2 * 20), &tr.file_path, NULL) == 0;
                    free(file_name);
                    if (!sok) {
                        problem(&c, "%sthe strings of a track row cannot be read", at);
                        free(tr.analyze_path);
                        free(tr.title);
                        free(tr.file_path);
                        break;
                    }
                    if (!idset_add(&track_ids, tr.id, r->present, &out->max_id[0])) {
                        problem(&c, "%strack id %u appears twice", at, tr.id);
                    }
                    out->analyze_paths = grow(out->analyze_paths, &nan_cap, out->nanalyze, sizeof *out->analyze_paths);
                    if (out->analyze_paths == NULL) {
                        c.oom = 1;
                        goto done;
                    }
                    out->analyze_paths[out->nanalyze] = strdup(tr.analyze_path);
                    if (out->analyze_paths[out->nanalyze] != NULL) {
                        out->nanalyze++;
                    }
                    if (r->present) {
                        char *expected = tc_analyze_path_for(tr.file_path, NULL, 0);

                        if (expected != NULL) {
                            const char *e1 = strrchr(expected, '/'), *e2 = strrchr(tr.analyze_path, '/');
                            size_t l1 = e1 != NULL ? (size_t)(e1 - expected) : strlen(expected);
                            size_t l2 = e2 != NULL ? (size_t)(e2 - tr.analyze_path) : strlen(tr.analyze_path);

                            if (l1 != l2 || strncmp(expected, tr.analyze_path, l1) != 0) {
                                warn_add(out, "track %u: the analysis folder %s is not the one rekordbox's rule gives for %s", tr.id, tr.analyze_path,
                                         tr.file_path);
                            }
                            free(expected);
                        }
                        k = out->ntracks;
                        out->tracks = grow(out->tracks, &ntracks_cap, k, sizeof *out->tracks);
                        if (out->tracks == NULL) {
                            c.oom = 1;
                            goto done;
                        }
                        {
                            char **tl = realloc(out->track_lower, (k + 1) * sizeof *tl);

                            if (tl == NULL) {
                                c.oom = 1;
                                goto done;
                            }
                            out->track_lower = tl;
                            tl[k] = lower_dup(tr.file_path);
                        }
                        out->tracks[k] = tr;
                        out->ntracks++;
                    } else {
                        free(tr.analyze_path);
                        free(tr.title);
                        free(tr.file_path);
                    }
                    break;
                }
                case 2: {                                                   /* artists */
                    char *name = NULL;
                    uint32_t id;

                    if (bl < 11 || rd16(b, 0) != 0x60 || tc_devsql_decode(b, bl, b[9], &name, NULL) != 0) {
                        problem(&c, "%san artist row cannot be read", at);
                        break;
                    }
                    id = rd32(b, 4);
                    if (!idset_add(&artist_ids, id, r->present, &out->max_id[2])) {
                        problem(&c, "%sartist id %u appears twice", at, id);
                    }
                    if (r->present) {
                        if (name_add(&out->artist, &out->nartist, &acap, 0, id, name) != 0) {
                            c.oom = 1;
                        }
                    } else {
                        free(name);
                    }
                    break;
                }
                case 3: {                                                   /* albums */
                    char *name = NULL;
                    uint32_t id, artist;

                    if (bl < 23 || rd16(b, 0) != 0x80 || tc_devsql_decode(b, bl, b[21], &name, NULL) != 0) {
                        problem(&c, "%san album row cannot be read", at);
                        break;
                    }
                    id = rd32(b, 12);
                    artist = rd32(b, 8);
                    if (!idset_add(&album_ids, id, r->present, &out->max_id[3])) {
                        problem(&c, "%salbum id %u appears twice", at, id);
                    }
                    if (r->present) {
                        if (name_add(&out->album, &out->nalbum, &alcap, artist, id, name) != 0) {
                            c.oom = 1;
                        }
                    } else {
                        free(name);
                    }
                    break;
                }
                case 1:
                case 4:
                case 13: {                                                  /* genres, labels, artwork: an id and a string */
                    char *name = NULL;
                    uint32_t id;
                    struct idset *ids = type == 1 ? &genre_ids : type == 4 ? &label_ids : &artwork_ids;

                    if (bl < 5 || tc_devsql_decode(b, bl, 4, &name, NULL) != 0) {
                        problem(&c, "%sa row cannot be read", at);
                        break;
                    }
                    id = rd32(b, 0);
                    if (!idset_add(ids, id, r->present, &out->max_id[type])) {
                        problem(&c, "%sid %u appears twice", at, id);
                    }
                    if (r->present) {
                        int rc2 = type == 1 ? name_add(&out->genre, &out->ngenre, &gcap, 0, id, name)
                                : type == 4 ? name_add(&out->label, &out->nlabel, &lcap, 0, id, name)
                                            : name_add(&out->artwork, &out->nartwork, &wcap, 0, id, name);

                        if (rc2 != 0) {
                            c.oom = 1;
                        }
                        if (type == 13) {
                            out->artwork_count++;
                        }
                    } else {
                        free(name);
                    }
                    break;
                }
                case 7: {                                                   /* playlist tree */
                    char *name = NULL;
                    uint32_t parent, sort, id, folder;

                    if (bl < 21 || tc_devsql_decode(b, bl, 20, &name, NULL) != 0) {
                        problem(&c, "%sa playlist row cannot be read", at);
                        break;
                    }
                    parent = rd32(b, 0);
                    sort = rd32(b, 8);
                    id = rd32(b, 12);
                    folder = rd32(b, 16);
                    if (!idset_add(&playlist_ids, id, r->present, &out->max_id[7])) {
                        problem(&c, "%splaylist id %u appears twice", at, id);
                    }
                    if (r->present) {
                        char *lo = lower_dup(name);
                        char **pl;
                        uint32_t *pi;

                        if (lo == NULL) {
                            c.oom = 1;
                            free(name);
                            break;
                        }
                        pl = grow(out->playlist_lower, &npl_cap, out->nplaylists, sizeof *pl);
                        out->playlist_lower = pl != NULL ? pl : out->playlist_lower;
                        pi = realloc(out->playlist_id, (out->nplaylists + 1) * sizeof *pi);
                        if (pl == NULL || pi == NULL) {
                            c.oom = 1;
                            free(lo);
                            free(name);
                            break;
                        }
                        out->playlist_id = pi;
                        out->playlist_lower[out->nplaylists] = lo;
                        out->playlist_id[out->nplaylists] = id;
                        out->nplaylists++;
                        if (parent == 0 && sort > out->max_root_sort_order) {
                            out->max_root_sort_order = sort;
                        }
                        if (folder != 0) {
                            warn_add(out, "playlist \"%s\" is a folder: folders are left as they are", name);
                        }
                    }
                    free(name);
                    break;
                }
                case 8: {                                                   /* playlist entries */
                    if (bl < 12) {
                        problem(&c, "%sa playlist entry row is too short", at);
                        break;
                    }
                    if (r->present) {
                        entries = grow(entries, &entries_cap, nentries, sizeof *entries);
                        if (entries == NULL) {
                            c.oom = 1;
                            goto done;
                        }
                        entries[nentries].index = rd32(b, 0);
                        entries[nentries].track = rd32(b, 4);
                        entries[nentries].playlist = rd32(b, 8);
                        nentries++;
                    }
                    if (rd32(b, 0) > out->max_id[8]) {
                        out->max_id[8] = rd32(b, 0);
                    }
                    break;
                }
                default:
                    break;                                                  /* keys, colours, history ...: only their chains matter */
                }
            }
        }
    }
    for (i = 0; i < nentries; i++) {
        if (!idset_has(&track_ids, entries[i].track)) {
            problem(&c, "a playlist entry refers to track %u, which is not in the track table", entries[i].track);
        }
        if (!idset_has(&playlist_ids, entries[i].playlist)) {
            problem(&c, "a playlist entry refers to playlist %u, which is not in the playlist table", entries[i].playlist);
        }
    }
    dedupe_names(out->artist, &out->nartist);
    dedupe_names(out->album, &out->nalbum);
    dedupe_names(out->genre, &out->ngenre);
    dedupe_names(out->label, &out->nlabel);
    dedupe_names(out->artwork, &out->nartwork);
    ok = !c.failed && !c.oom;
done:
    if (c.oom) {
        c.failed = 1;
        ok = 0;
        if (problems != NULL && c.n == 0) {
            problem(&c, "out of memory");
        }
    }
    if (data != NULL) {
        for (t = 0; t < (out->file.ntables); t++) {
            free(data[t]);
        }
    }
    free(data);
    free(ndata);
    free(entries);
    idset_free(&track_ids);
    idset_free(&artist_ids);
    idset_free(&album_ids);
    idset_free(&genre_ids);
    idset_free(&label_ids);
    idset_free(&artwork_ids);
    idset_free(&playlist_ids);
    if (problems != NULL) {
        *problems = c.problems;
        *nproblems = c.n;
    } else {
        tc_problems_free(c.problems, c.n);
    }
    return ok ? 0 : -1;
}

char *tc_analyze_path_for(const char *file_path, char *const *used, size_t nused)
{
    const uint32_t slot = tc_analyze_slot(file_path);
    const unsigned part = tc_analyze_partition(slot);
    char buf[96];
    unsigned n;

    for (n = 0;; n++) {
        size_t k;
        int taken = 0;

        snprintf(buf, sizeof buf, "/PIONEER/USBANLZ/P%03X/%08X/ANLZ%04X.DAT", part, slot, n);
        for (k = 0; k < nused && !taken; k++) {
            taken = strcmp(used[k], buf) == 0;
        }
        if (!taken) {
            break;
        }
    }
    return strdup(buf);
}

/* ---- adding rows ---- */

static int has_index_shift(uint32_t type)
{
    return type == 0 || type == 2 || type == 3;          /* tracks, artists, albums: bytes 2..3 of the row are slot * 0x20 */
}

struct editor {
    struct tc_pdb *f;
    uint32_t seq;                                       /* the last write counter handed out */
    uint32_t *touched;                                  /* sorted, unique */
    size_t ntouched, touched_cap;
    int rows_added, pages_added;
};

/* true when the page was not in the set */
static int touched_insert(struct editor *e, uint32_t no)
{
    size_t lo = 0, hi = e->ntouched;

    while (lo < hi) {
        size_t mid = (lo + hi) / 2;

        if (e->touched[mid] < no) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < e->ntouched && e->touched[lo] == no) {
        return 0;
    }
    e->touched = grow(e->touched, &e->touched_cap, e->ntouched, sizeof *e->touched);
    if (e->touched == NULL) {
        e->ntouched = 0;
        return 0;
    }
    memmove(e->touched + lo + 1, e->touched + lo, (e->ntouched - lo) * sizeof *e->touched);
    e->touched[lo] = no;
    e->ntouched++;
    return 1;
}

static int fits(const struct tc_pdb_page *p, size_t row_bytes)
{
    const size_t slots = p->nrows + 1;
    const size_t heap = tc_pdb_heap_bytes(p) + row_bytes;

    return slots <= 0x1FFF && heap <= 0xFFFF && K_HEAD + heap + tc_pdb_index_span(slots) <= K_PAGE;
}

/* Puts the new row into the next slot of a structured page and marks it as the row written last. Takes over `row` (malloc'd). -1 when out of memory. */
static int place(struct editor *e, struct tc_pdb_page *p, uint32_t no, uint8_t *row, size_t rowlen, uint32_t type, int new_page)
{
    const size_t slot = p->nrows;
    const int fresh = p->u20 == 1 && (unsigned)p->num_large + 1u == slot;      /* the page follows the fresh pattern: keep following it */
    const size_t group = slot / K_PER;
    struct tc_pdb_row *nr;

    if (has_index_shift(type)) {
        tc_set_le16(row + 2, (unsigned)((slot * 0x20) & 0xFFFF));
    }
    nr = realloc(p->rows, (slot + 1) * sizeof *nr);
    if (nr == NULL) {
        free(row);
        return -1;
    }
    p->rows = nr;
    p->rows[slot].bytes = row;
    p->rows[slot].len = rowlen;
    p->rows[slot].present = 1;
    p->nrows = slot + 1;
    if (p->ngroups <= group) {
        uint16_t *ng = realloc(p->group_flags, (group + 1) * sizeof *ng);
        size_t g;

        if (ng == NULL) {
            return -1;
        }
        for (g = p->ngroups; g <= group; g++) {
            ng[g] = 0;
        }
        p->group_flags = ng;
        p->ngroups = group + 1;
    }
    p->group_flags[group] = (uint16_t)(p->group_flags[group] | (1u << (slot % K_PER)));   /* rekordbox: the flag word covers every slot of the page */
    if (fresh) {
        p->num_large = (uint16_t)slot;
    }
    if (new_page) {
        if (touched_insert(e, no)) {
            p->u10 = ++e->seq;
        }
    } else {
        touched_insert(e, no);
    }
    e->rows_added++;
    return 0;
}

/* appends a row to the table of `type`. Takes over `row`. 0, or -1 with err */
static int add_row(struct editor *e, uint32_t type, uint8_t *row, size_t rowlen, char *err, size_t errsz)
{
    struct tc_pdb *f = e->f;
    struct tc_pdb_table *t = NULL;
    size_t i;
    uint32_t no, new_candidate;
    struct tc_pdb_page np;

    for (i = 0; i < f->ntables; i++) {
        if (f->tables[i].type == type) {
            t = &f->tables[i];
        }
    }
    if (t == NULL) {
        snprintf(err, errsz, "the database has no table %u", type);
        free(row);
        return -1;
    }
    if (K_HEAD + rowlen + 8 > K_PAGE) {
        snprintf(err, errsz, "a row of table %u is too large for a page", type);
        free(row);
        return -1;
    }
    if (t->last_page != t->first_page) {
        struct tc_pdb_page *last = &f->pages[t->last_page];

        if (fits(last, rowlen)) {
            if (place(e, last, t->last_page, row, rowlen, type, 0) != 0) {
                snprintf(err, errsz, "out of memory");
                return -1;
            }
            return 0;
        }
    }
    /* a new page: it takes the table's empty candidate number, and the new empty candidate is the next unused page */
    no = t->empty_candidate;
    new_candidate = f->next_unused_page;
    if (f->npages <= no) {
        struct tc_pdb_page *np2 = realloc(f->pages, ((size_t)no + 1) * sizeof *np2);

        if (np2 == NULL) {
            free(row);
            snprintf(err, errsz, "out of memory");
            return -1;
        }
        f->pages = np2;
        while (f->npages <= no) {
            struct tc_pdb_page *blank = &f->pages[f->npages];

            memset(blank, 0, sizeof *blank);
            blank->raw = calloc(1, K_PAGE);
            if (blank->raw == NULL) {
                free(row);
                snprintf(err, errsz, "out of memory");
                return -1;
            }
            f->npages++;
        }
    }
    memset(&np, 0, sizeof np);
    np.index = no;
    np.type = type;
    np.next = new_candidate;
    np.flags = 0x24;
    np.u20 = 1;
    np.num_large = 0;
    np.structured = 1;
    np.group_flags = calloc(1, sizeof *np.group_flags);
    np.ngroups = 1;
    if (np.group_flags == NULL) {
        free(row);
        snprintf(err, errsz, "out of memory");
        return -1;
    }
    tc_pdb_page_free(&f->pages[no]);
    f->pages[no] = np;
    if (place(e, &f->pages[no], no, row, rowlen, type, 1) != 0) {
        snprintf(err, errsz, "out of memory");
        return -1;
    }
    {
        struct tc_pdb_page *prev = &f->pages[t->last_page];

        if (!prev->structured && prev->raw != NULL && rd32(prev->raw, 0x2c) == K_EMPTY_ENTRY) {      /* the table was empty: its index page names the first data page */
            tc_set_le32(prev->raw + 0x2c, no);
        }
    }
    t->last_page = no;
    t->empty_candidate = new_candidate;
    f->next_unused_page = new_candidate + 1;
    e->pages_added++;
    return 0;
}

/* ---- appendPlaylist ---- */

struct pending_row {
    uint32_t type;
    uint8_t *bytes;
    size_t len;
};

struct made_name {
    uint32_t type;
    char *name;                         /* owned */
};

struct made_album {
    uint32_t id, artist;
    char *name;                         /* owned */
};

struct build {
    uint32_t next[TC_DRIVE_MAX_TABLE_TYPE];             /* table type -> the last id handed out */
    struct tc_name_id *fresh[TC_DRIVE_MAX_TABLE_TYPE];  /* names made so far, per table type (the albums in fresh[3], keyed by artist) */
    size_t nfresh[TC_DRIVE_MAX_TABLE_TYPE], fresh_cap[TC_DRIVE_MAX_TABLE_TYPE];
    struct made_name *names;
    size_t nnames, names_cap;
    struct made_album *albums;
    size_t nalbums, albums_cap;
    struct pending_row *rows;                           /* in the order they are added */
    size_t nrows, rows_cap;
    struct pending_row *trows;                          /* the track rows, made first and added after the names and albums */
    size_t ntrows, trows_cap;
    int oom;
};

static void build_free(struct build *b)
{
    size_t i;

    for (i = 0; i < TC_DRIVE_MAX_TABLE_TYPE; i++) {
        free_names(b->fresh[i], b->nfresh[i]);
    }
    for (i = 0; i < b->nnames; i++) {
        free(b->names[i].name);
    }
    free(b->names);
    for (i = 0; i < b->nalbums; i++) {
        free(b->albums[i].name);
    }
    free(b->albums);
    for (i = 0; i < b->nrows; i++) {
        free(b->rows[i].bytes);
    }
    free(b->rows);
    for (i = 0; i < b->ntrows; i++) {
        free(b->trows[i].bytes);
    }
    free(b->trows);
}

static const struct tc_name_id *find_fresh(const struct build *b, uint32_t type, uint32_t artist, const char *name)
{
    size_t i;

    for (i = 0; i < b->nfresh[type]; i++) {
        if (b->fresh[type][i].artist == artist && strcmp(b->fresh[type][i].name, name) == 0) {
            return &b->fresh[type][i];
        }
    }
    return NULL;
}

/* the id of the artist, genre, label or artwork called `name`: the drive's, one made earlier in this addition, or a new one. 0 for an empty name. */
static uint32_t id_for(struct build *b, const struct tc_name_id *have, size_t nhave, uint32_t type, const char *name)
{
    const struct tc_name_id *f;
    char *copy;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    f = tc_name_find(have, nhave, 0, name);
    if (f != NULL) {
        return f->id;
    }
    f = find_fresh(b, type, 0, name);
    if (f != NULL) {
        return f->id;
    }
    copy = strdup(name);
    if (copy == NULL || name_add(&b->fresh[type], &b->nfresh[type], &b->fresh_cap[type], 0, ++b->next[type], strdup(name)) != 0) {
        free(copy);
        b->oom = 1;
        return 0;
    }
    b->names = grow(b->names, &b->names_cap, b->nnames, sizeof *b->names);
    if (b->names == NULL) {
        free(copy);
        b->oom = 1;
        return 0;
    }
    b->names[b->nnames].type = type;
    b->names[b->nnames].name = copy;
    b->nnames++;
    return b->next[type];
}

static int push_row_to(struct build *b, struct pending_row **list, size_t *n, size_t *cap, uint32_t type, uint8_t *bytes, size_t len)
{
    if (bytes == NULL) {
        b->oom = 1;
        return -1;
    }
    *list = grow(*list, cap, *n, sizeof **list);
    if (*list == NULL) {
        free(bytes);
        b->oom = 1;
        return -1;
    }
    (*list)[*n].type = type;
    (*list)[*n].bytes = bytes;
    (*list)[*n].len = len;
    (*n)++;
    return 0;
}

static int push_row(struct build *b, uint32_t type, uint8_t *bytes, size_t len)
{
    return push_row_to(b, &b->rows, &b->nrows, &b->rows_cap, type, bytes, len);
}

void tc_append_result_free(struct tc_append_result *r)
{
    size_t i;

    free(r->bytes);
    free(r->track_ids);
    free(r->track_is_new);
    if (r->analyze_paths != NULL) {
        for (i = 0; i < r->ntracks; i++) {
            free(r->analyze_paths[i]);
        }
    }
    free(r->analyze_paths);
    if (r->artwork_paths != NULL) {
        for (i = 0; i < r->nartwork; i++) {
            free(r->artwork_paths[i]);
        }
    }
    free(r->artwork_paths);
    memset(r, 0, sizeof *r);
}

static void tail_set(char *err, size_t errsz, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void tail_set(char *err, size_t errsz, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(err, errsz, fmt, ap);
    va_end(ap);
}

int tc_append_playlist(struct tc_drive_db *db, const struct tc_export_plan *plan, const uint8_t *tpl_bytes, size_t tpl_len,
                       struct tc_append_result *out, char *err, size_t errsz)
{
    struct build b;
    struct tc_pdb tpl_file, work;
    struct tc_track_template tpl;
    struct editor ed;
    struct tc_drive_db fresh;
    char **used = NULL, **problems = NULL;
    size_t nused = 0, used_cap = 0, nproblems = 0;
    unsigned char *wanted = NULL;
    size_t i, k, n;
    int have_tpl_file = 0, have_tpl = 0, have_work = 0, rc = -1;
    uint32_t sort, t_id;
    uint8_t *bytes = NULL;
    size_t blen = 0;
    char perr[300];

    memset(&b, 0, sizeof b);
    memset(&ed, 0, sizeof ed);
    memset(out, 0, sizeof *out);
    memset(&fresh, 0, sizeof fresh);
    if (plan->nplaylists == 0) {
        tail_set(err, errsz, "no playlist to add");
        return -1;
    }
    for (i = 0; i < plan->nplaylists; i++) {
        const struct tc_plan_playlist *pl = &plan->playlists[i];
        char *key;
        size_t j;

        if (pl->ntracks == 0) {
            tail_set(err, errsz, "the playlist \"%s\" has no tracks", pl->name);
            return -1;
        }
        key = lower_dup(pl->name);
        if (key == NULL) {
            tail_set(err, errsz, "out of memory");
            return -1;
        }
        for (j = 0; j < db->nplaylists; j++) {
            if (strcmp(db->playlist_lower[j], key) == 0) {
                tail_set(err, errsz, "the pendrive already has a playlist called \"%s\"", pl->name);
                free(key);
                return -1;
            }
        }
        for (j = 0; j < i; j++) {
            char *other = lower_dup(plan->playlists[j].name);
            int same = other != NULL && strcmp(other, key) == 0;

            free(other);
            if (same) {
                tail_set(err, errsz, "the playlist \"%s\" is given twice", pl->name);
                free(key);
                return -1;
            }
        }
        free(key);
        for (j = 0; j < pl->ntracks; j++) {
            if (pl->tracks[j] >= plan->ntracks) {
                tail_set(err, errsz, "playlist \"%s\" refers to a track that does not exist", pl->name);
                return -1;
            }
        }
    }
    if (tc_pdb_parse(tpl_bytes, tpl_len, &tpl_file, perr, sizeof perr) != 0) {
        tail_set(err, errsz, "the template database has no usable track row");
        return -1;
    }
    have_tpl_file = 1;
    if (!tc_track_template_load(&tpl_file, &tpl)) {
        tail_set(err, errsz, "the template database has no usable track row");
        goto out;
    }
    have_tpl = 1;

    /* ids (nothing is changed until every row is laid out) */
    {
        static const uint32_t types[] = { 0, 1, 2, 3, 4, 7, 13 };

        for (i = 0; i < sizeof types / sizeof *types; i++) {
            b.next[types[i]] = db->max_id[types[i]];
        }
    }
    out->ntracks = plan->ntracks;
    out->track_ids = calloc(plan->ntracks ? plan->ntracks : 1, sizeof *out->track_ids);
    out->track_is_new = calloc(plan->ntracks ? plan->ntracks : 1, sizeof *out->track_is_new);
    out->analyze_paths = calloc(plan->ntracks ? plan->ntracks : 1, sizeof *out->analyze_paths);
    wanted = calloc(plan->ntracks ? plan->ntracks : 1, 1);
    if (out->track_ids == NULL || out->track_is_new == NULL || out->analyze_paths == NULL || wanted == NULL) {
        goto oom;
    }
    for (i = 0; i < db->nanalyze; i++) {
        used = grow(used, &used_cap, nused, sizeof *used);
        if (used == NULL) {
            goto oom;
        }
        used[nused++] = db->analyze_paths[i];           /* borrowed */
    }
    for (i = 0; i < plan->nplaylists; i++) {
        for (k = 0; k < plan->playlists[i].ntracks; k++) {
            wanted[plan->playlists[i].tracks[k]] = 1;
        }
    }
    for (i = 0; i < plan->ntracks; i++) {
        const struct tc_plan_track *t = &plan->tracks[i];
        uint32_t genre, label, artist, album = 0, art = 0;
        char *lp, *analyze;
        size_t ex;
        uint8_t *row;
        size_t rlen = 0;

        if (!wanted[i]) {
            continue;
        }
        lp = lower_dup(t->file_path ? t->file_path : "");
        if (lp == NULL) {
            goto oom;
        }
        for (ex = 0; ex < db->ntracks && strcmp(db->track_lower[ex], lp) != 0; ex++) {
        }
        free(lp);
        if (ex < db->ntracks) {
            out->track_ids[i] = db->tracks[ex].id;
            out->analyze_paths[i] = strdup(db->tracks[ex].analyze_path);
            if (out->analyze_paths[i] == NULL) {
                goto oom;
            }
            continue;
        }
        genre = id_for(&b, db->genre, db->ngenre, 1, t->genre);
        label = id_for(&b, db->label, db->nlabel, 4, t->label);
        artist = id_for(&b, db->artist, db->nartist, 2, t->artist);
        if (t->album != NULL && t->album[0] != '\0') {
            const struct tc_name_id *have = tc_name_find(db->album, db->nalbum, artist, t->album);
            const struct tc_name_id *mine = find_fresh(&b, 3, artist, t->album);

            if (have != NULL) {
                album = have->id;
            } else if (mine != NULL) {
                album = mine->id;
            } else {
                album = ++b.next[3];
                if (name_add(&b.fresh[3], &b.nfresh[3], &b.fresh_cap[3], artist, album, strdup(t->album)) != 0) {
                    goto oom;
                }
                b.albums = grow(b.albums, &b.albums_cap, b.nalbums, sizeof *b.albums);
                if (b.albums == NULL) {
                    goto oom;
                }
                b.albums[b.nalbums].id = album;
                b.albums[b.nalbums].artist = artist;
                b.albums[b.nalbums].name = strdup(t->album);
                if (b.albums[b.nalbums].name == NULL) {
                    goto oom;
                }
                b.nalbums++;
            }
        }
        if (t->artwork_key != NULL && t->artwork_key[0] && t->artwork_path != NULL && t->artwork_path[0]) {
            art = id_for(&b, db->artwork, db->nartwork, 13, t->artwork_path);
        }
        if (b.oom) {
            goto oom;
        }
        if (t->analyze_path != NULL && t->analyze_path[0]) {
            analyze = strdup(t->analyze_path);
        } else {
            analyze = tc_analyze_path_for(t->file_path ? t->file_path : "", used, nused);
        }
        if (analyze == NULL) {
            goto oom;
        }
        used = grow(used, &used_cap, nused, sizeof *used);
        if (used == NULL) {
            free(analyze);
            goto oom;
        }
        used[nused++] = analyze;                         /* borrowed from out->analyze_paths below */
        t_id = ++b.next[0];
        out->track_ids[i] = t_id;
        out->track_is_new[i] = 1;
        out->analyze_paths[i] = analyze;
        row = tc_row_track(&tpl, t, t_id, genre, album, artist, label, art, analyze, &rlen);
        if (push_row_to(&b, &b.trows, &b.ntrows, &b.trows_cap, 0, row, rlen) != 0) {
            goto oom;
        }
    }
    /* the order the rows are added in: names (as they were made), albums, tracks, then the playlists */
    {
        size_t a;

        for (a = 0; a < b.nnames; a++) {
            const uint32_t type = b.names[a].type;
            const char *name = b.names[a].name;
            const struct tc_name_id *f = find_fresh(&b, type, 0, name);
            size_t rl = 0;
            uint8_t *r = type == 2 ? tc_row_artist(f->id, name, &rl) : tc_row_name(f->id, name, &rl);

            if (push_row(&b, type, r, rl) != 0) {
                goto oom;
            }
            if (type == 13) {
                char **ap = realloc(out->artwork_paths, (out->nartwork + 1) * sizeof *ap);

                if (ap == NULL) {
                    goto oom;
                }
                out->artwork_paths = ap;
                out->artwork_paths[out->nartwork] = strdup(name);
                if (out->artwork_paths[out->nartwork] == NULL) {
                    goto oom;
                }
                out->nartwork++;
            }
        }
        for (a = 0; a < b.nalbums; a++) {
            size_t rl = 0;
            uint8_t *r = tc_row_album(b.albums[a].id, b.albums[a].artist, b.albums[a].name, &rl);

            if (push_row(&b, 3, r, rl) != 0) {
                goto oom;
            }
        }
        for (a = 0; a < b.ntrows; a++) {
            if (push_row(&b, 0, b.trows[a].bytes, b.trows[a].len) != 0) {
                goto oom;
            }
            b.trows[a].bytes = NULL;                    /* now owned by rows */
        }
    }
    sort = db->nplaylists == 0 ? 0 : db->max_root_sort_order + 1;
    for (i = 0; i < plan->nplaylists; i++) {
        const struct tc_plan_playlist *pl = &plan->playlists[i];
        const uint32_t pid = ++b.next[7];
        size_t rl = 0;

        if (out->playlist_id == 0) {
            out->playlist_id = pid;
        }
        uint8_t *r = tc_row_playlist(pid, sort++, pl->name, &rl);        /* the length is only known once the row is made */

        if (push_row(&b, 7, r, rl) != 0) {
            goto oom;
        }
        for (k = 0; k < pl->ntracks; k++) {
            rl = 0;
            r = tc_row_entry((uint32_t)(k + 1), out->track_ids[pl->tracks[k]], pid, &rl);
            if (push_row(&b, 8, r, rl) != 0) {
                goto oom;
            }
        }
    }

    /* lay the rows out in a copy of the file */
    if (tc_pdb_clone(&db->file, &work) != 0) {
        goto oom;
    }
    have_work = 1;
    ed.f = &work;
    ed.seq = work.sequence;
    for (i = 1; i < work.npages; i++) {
        if (work.pages[i].u10 > ed.seq) {
            ed.seq = work.pages[i].u10;
        }
    }
    for (n = 0; n < b.nrows; n++) {
        uint8_t *r = b.rows[n].bytes;

        b.rows[n].bytes = NULL;                         /* add_row takes it over, also on failure */
        if (add_row(&ed, b.rows[n].type, r, b.rows[n].len, err, errsz) != 0) {
            goto out;
        }
    }
    work.sequence = ed.seq + 1;                         /* rekordbox leaves the header sequence a little past the last page counter */
    bytes = tc_pdb_serialise(&work, &blen, perr, sizeof perr);
    if (bytes == NULL) {
        tail_set(err, errsz, "%s", perr);
        goto out;
    }
    if (tc_drive_db_load(bytes, blen, &fresh, &problems, &nproblems) != 0) {
        tail_set(err, errsz, "the new database is not valid: %s", nproblems > 0 ? problems[0] : "unknown reason");
        tc_drive_db_free(&fresh);
        goto out;
    }
    out->bytes = bytes;
    out->len = blen;
    bytes = NULL;
    out->rows_added = ed.rows_added;
    out->pages_added = ed.pages_added;
    tc_drive_db_free(db);
    *db = fresh;
    rc = 0;
    goto out;
oom:
    tail_set(err, errsz, "out of memory");
out:
    if (rc != 0) {
        tc_append_result_free(out);
    }
    free(bytes);
    tc_problems_free(problems, nproblems);
    free(used);
    free(wanted);
    free(ed.touched);
    if (have_work) {
        tc_pdb_free(&work);
    }
    if (have_tpl) {
        tc_track_template_free(&tpl);
    }
    if (have_tpl_file) {
        tc_pdb_free(&tpl_file);
    }
    build_free(&b);
    return rc;
}

/* ---- verifyAppended ---- */

struct vtrack {
    uint32_t id;
    char *title, *path;
};

struct vtracks {
    struct vtrack *v;
    size_t n, cap;
};

static int cb_vtrack(const pdb_track_t *t, void *ud)
{
    struct vtracks *l = ud;

    l->v = grow(l->v, &l->cap, l->n, sizeof *l->v);
    if (l->v == NULL) {
        return 1;
    }
    l->v[l->n].id = t->id;
    l->v[l->n].title = strdup(t->title ? t->title : "");
    l->v[l->n].path = strdup(t->file_path ? t->file_path : "");
    l->n++;
    return 0;
}

struct vlist {
    char **v;
    size_t n, cap;
};

static void bad(struct vlist *l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void bad(struct vlist *l, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (l->n >= K_MAX_PROBLEMS) {
        return;
    }
    l->v = grow(l->v, &l->cap, l->n, sizeof *l->v);
    if (l->v == NULL) {
        return;
    }
    l->v[l->n] = strdup(buf);
    if (l->v[l->n] != NULL) {
        l->n++;
    }
}

struct ent2 {
    uint32_t index, track;
};

static int cmp_ent2(const void *a, const void *b)
{
    const struct ent2 *x = a, *y = b;

    if (x->index != y->index) {
        return x->index < y->index ? -1 : 1;
    }
    return x->track < y->track ? -1 : x->track > y->track;
}

size_t tc_verify_appended(const uint8_t *before, size_t before_len, const uint8_t *after, size_t after_len, const struct tc_export_plan *plan,
                          const struct tc_append_result *result, char ***diffs)
{
    struct vlist l = { NULL, 0, 0 };
    struct tc_pdb b, a;
    struct tc_drive_db db;
    struct vtracks seen = { NULL, 0, 0 };
    char perr[300];
    char **problems = NULL;
    size_t nproblems = 0, n, i, k;
    int have_a = 0, have_b = 0, have_db = 0;

    memset(&db, 0, sizeof db);
    if (tc_pdb_parse(before, before_len, &b, perr, sizeof perr) != 0) {
        bad(&l, "the old database cannot be read: %s", perr);
        goto done;
    }
    have_b = 1;
    if (tc_pdb_parse(after, after_len, &a, perr, sizeof perr) != 0) {
        bad(&l, "the new database cannot be read: %s", perr);
        goto done;
    }
    have_a = 1;
    if (a.npages < b.npages) {
        bad(&l, "the new database is shorter than the old one");
    }
    if (a.ntables != b.ntables) {
        bad(&l, "the number of tables changed");
        goto done;
    }
    if (a.next_unused_page < b.next_unused_page) {
        bad(&l, "next unused page went backwards");
    }
    if (a.sequence < b.sequence) {
        bad(&l, "the sequence went backwards");
    }
    /* nothing that was there changed */
    for (n = 1; n < b.npages && n < a.npages; n++) {
        const struct tc_pdb_page *x = &b.pages[n], *y = &a.pages[n];

        if (x->structured != y->structured && !(!x->structured && x->flags == 0)) {
            bad(&l, "page %zu changed kind", n);
            continue;
        }
        if (x->structured) {
            if (y->nrows < x->nrows) {
                bad(&l, "page %zu lost rows", n);
                continue;
            }
            for (i = 0; i < x->nrows; i++) {
                if (x->rows[i].len != y->rows[i].len || memcmp(x->rows[i].bytes, y->rows[i].bytes, x->rows[i].len) != 0 ||
                    x->rows[i].present != y->rows[i].present) {
                    bad(&l, "page %zu row %zu changed", n, i);
                }
            }
            if (y->nrows == x->nrows && y->next != x->next) {
                bad(&l, "page %zu changed its next page", n);
            }
            if (y->nrows > x->nrows && y->flags != x->flags) {
                bad(&l, "page %zu changed its flags", n);
            }
        } else if (x->flags & 0x40) {                                    /* an index page: only the first data page of an empty table may be filled in */
            if (x->raw != NULL && y->raw != NULL) {
                uint8_t xr[K_PAGE];

                memcpy(xr, x->raw, K_PAGE);
                if (rd32(xr, 0x2c) == K_EMPTY_ENTRY) {
                    tc_set_le32(xr + 0x2c, rd32(y->raw, 0x2c));
                }
                if (memcmp(xr, y->raw, K_PAGE) != 0) {
                    bad(&l, "index page %zu changed", n);
                }
            }
        } else if (x->flags == 0 && !y->structured && x->raw != NULL && y->raw != NULL && memcmp(x->raw, y->raw, K_PAGE) != 0) {
            bad(&l, "blank page %zu changed", n);
        }
    }
    /* tables: chains, counters */
    if (tc_drive_db_load(after, after_len, &db, &problems, &nproblems) != 0) {
        for (i = 0; i < nproblems; i++) {
            bad(&l, "the new database fails the checks: %s", problems[i]);
        }
        have_db = 1;
        goto done;
    }
    have_db = 1;
    /* the new rows through libcdj's reader */
    pdb_parse(after, (uint32_t)after_len, cb_vtrack, &seen);
    for (i = 0; i < plan->ntracks; i++) {
        const struct vtrack *s = NULL;
        char *lo1, *lo2;

        if (result->ntracks <= i || result->track_ids[i] == 0) {
            continue;
        }
        for (k = 0; k < seen.n; k++) {
            if (seen.v[k].id == result->track_ids[i]) {
                s = &seen.v[k];
                break;
            }
        }
        if (s == NULL) {
            bad(&l, "track \"%s\" is not in the new database", plan->tracks[i].title ? plan->tracks[i].title : "");
            continue;
        }
        lo1 = lower_dup(s->path);
        lo2 = lower_dup(plan->tracks[i].file_path ? plan->tracks[i].file_path : "");
        if (lo1 != NULL && lo2 != NULL && strcmp(lo1, lo2) != 0) {
            bad(&l, "track \"%s\" has the path %s, expected %s", plan->tracks[i].title ? plan->tracks[i].title : "", s->path,
                plan->tracks[i].file_path ? plan->tracks[i].file_path : "");
        }
        free(lo1);
        free(lo2);
        if (result->track_is_new[i] && strcmp(s->title, plan->tracks[i].title ? plan->tracks[i].title : "") != 0) {
            bad(&l, "track %u has the title \"%s\"", result->track_ids[i], s->title);
        }
    }
    /* playlist entries in order */
    {
        size_t nrows = 0;
        const struct tc_pdb_row **rows = tc_pdb_table_rows(&a, 8, &nrows);

        for (i = 0; i < plan->nplaylists; i++) {
            const struct tc_plan_playlist *pl = &plan->playlists[i];
            char *key = lower_dup(pl->name);
            struct ent2 *es = calloc(nrows ? nrows : 1, sizeof *es);
            size_t nes = 0, j;
            uint32_t pid = 0;
            int found = 0;

            for (j = 0; key != NULL && j < db.nplaylists; j++) {
                if (strcmp(db.playlist_lower[j], key) == 0) {
                    pid = db.playlist_id[j];
                    found = 1;
                    break;
                }
            }
            free(key);
            if (!found || es == NULL) {
                if (!found) {
                    bad(&l, "playlist \"%s\" is not in the new database", pl->name);
                }
                free(es);
                continue;
            }
            for (j = 0; j < nrows; j++) {
                if (rows[j]->len >= 12 && rd32(rows[j]->bytes, 8) == pid) {
                    es[nes].index = rd32(rows[j]->bytes, 0);
                    es[nes].track = rd32(rows[j]->bytes, 4);
                    nes++;
                }
            }
            qsort(es, nes, sizeof *es, cmp_ent2);
            if (nes != pl->ntracks) {
                bad(&l, "playlist \"%s\" has %zu entries, expected %zu", pl->name, nes, pl->ntracks);
            } else {
                for (j = 0; j < nes; j++) {
                    if (es[j].index != j + 1 || es[j].track != result->track_ids[pl->tracks[j]]) {
                        bad(&l, "playlist \"%s\" entry %zu is wrong", pl->name, j + 1);
                    }
                }
            }
            free(es);
        }
        free(rows);
    }
    /* counters agree with the contents, flags follow the deleted rows */
    for (n = 1; n < a.npages; n++) {
        const struct tc_pdb_page *p = &a.pages[n];

        if (!p->structured) {
            continue;
        }
        if (p->flags != (tc_pdb_present_rows(p) == p->nrows ? 0x24 : 0x34)) {
            bad(&l, "page %zu has flags %u that do not fit its rows", n, p->flags);
        }
    }
done:
    for (i = 0; i < seen.n; i++) {
        free(seen.v[i].title);
        free(seen.v[i].path);
    }
    free(seen.v);
    tc_problems_free(problems, nproblems);
    if (have_db) {
        tc_drive_db_free(&db);
    }
    if (have_a) {
        tc_pdb_free(&a);
    }
    if (have_b) {
        tc_pdb_free(&b);
    }
    n = l.n;
    if (diffs != NULL) {
        *diffs = l.v;
    } else {
        tc_problems_free(l.v, l.n);
    }
    return n;
}
