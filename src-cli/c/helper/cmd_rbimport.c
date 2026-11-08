/*
 * The two halves of import-rekordbox that bash cannot do (doc/trakcrate/import-from-rekordbox.md). Read only on the drive.
 *
 *   rbdrive DRIVE            the database as TSV lines, in the order of the file:
 *                              T  id  file_path  analyze_path      a track
 *                              P  id  name                         a playlist (not a folder), name = "Folder - Playlist"
 *                              E  playlist_id  track_id            an entry, in play order
 *   rbtrack DRIVE ID DIR [--no-beatgrid] [--no-cues] [--no-waveform] [--no-artwork] [--no-anlz]
 *                            DIR is a new track directory holding track.<ext>, meta.txt (the tags of the file) and source.txt:
 *                            the rekordbox values are written over them, the beat grid, cuemarks, waveform, artwork and the original
 *                            ANLZ files are added, and the origins in source.txt say "rekordbox".
 *                            stdout: grid cues waveform artwork anlz (0 or 1 each); warnings on stderr
 *
 * Real data wins over anything we generate; the files come from libcdj's readers.
 */
#define _GNU_SOURCE
#include <cdj/anlz.h>
#include <cdj/pdb.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../lib/beatgrid.h"
#include "../lib/cuemarks.h"
#include "../lib/fsops.h"
#include "../lib/fsutil.h"
#include "../lib/kv.h"
#include "../lib/status.h"
#include "../lib/wave.h"

#pragma GCC diagnostic ignored "-Wformat-truncation"

struct named {
    uint32_t id;
    char *name;
};

struct names {
    struct named *v;
    size_t n, cap;
};

static int names_cb(const pdb_named_t *r, void *ud)
{
    struct names *l = ud;

    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 32;
        struct named *nv = realloc(l->v, nc * sizeof *nv);

        if (nv == NULL) {
            return 1;
        }
        l->v = nv;
        l->cap = nc;
    }
    l->v[l->n].id = r->id;
    l->v[l->n].name = strdup(r->name ? r->name : "");
    l->n++;
    return 0;
}

static const char *name_of(const struct names *l, uint32_t id)
{
    size_t i;

    for (i = 0; i < l->n; i++) {
        if (l->v[i].id == id) {
            return l->v[i].name;
        }
    }
    return "";
}

static void names_free(struct names *l)
{
    size_t i;

    for (i = 0; i < l->n; i++) {
        free(l->v[i].name);
    }
    free(l->v);
}

/* ---- rbdrive ---- */

struct pl {
    pdb_playlist_t p;
    char *name;
};

struct pls {
    struct pl *v;
    size_t n, cap;
};

static int pl_cb(const pdb_playlist_t *r, void *ud)
{
    struct pls *l = ud;

    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 16;
        struct pl *nv = realloc(l->v, nc * sizeof *nv);

        if (nv == NULL) {
            return 1;
        }
        l->v = nv;
        l->cap = nc;
    }
    l->v[l->n].p = *r;
    l->v[l->n].p.name = NULL;
    l->v[l->n].name = strdup(r->name ? r->name : "");
    l->n++;
    return 0;
}

struct entries {
    pdb_playlist_entry_t *v;
    size_t n, cap;
};

static int entry_cb(const pdb_playlist_entry_t *r, void *ud)
{
    struct entries *l = ud;

    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 64;
        pdb_playlist_entry_t *nv = realloc(l->v, nc * sizeof *nv);

        if (nv == NULL) {
            return 1;
        }
        l->v = nv;
        l->cap = nc;
    }
    l->v[l->n++] = *r;
    return 0;
}

static int entry_cmp(const void *a, const void *b)
{
    const pdb_playlist_entry_t *x = a, *y = b;

    return x->entry_index < y->entry_index ? -1 : x->entry_index > y->entry_index;
}

static void clean_tsv(const char *s, char *out, size_t sz)
{
    size_t i = 0;

    for (; *s && i + 1 < sz; s++) {
        out[i++] = (*s == '\t' || *s == '\n' || *s == '\r') ? ' ' : *s;
    }
    out[i] = '\0';
}

static int emit_track(const pdb_track_t *t, void *ud)
{
    char fp[PATH_MAX], ap[PATH_MAX];

    (void)ud;
    clean_tsv(t->file_path ? t->file_path : "", fp, sizeof fp);
    clean_tsv(t->analyze_path ? t->analyze_path : "", ap, sizeof ap);
    printf("T\t%u\t%s\t%s\n", t->id, fp, ap);
    return 0;
}

static uint8_t *read_pdb(const char *drive, uint32_t *len)
{
    char path[PATH_MAX];
    uint8_t *data;

    snprintf(path, sizeof path, "%s/PIONEER/rekordbox/export.pdb", drive);
    data = pdb_read_file(path, len);
    if (data == NULL) {
        fprintf(stderr, "trakcrate-helper: not a rekordbox drive (cannot read %s)\n", path);
    }
    return data;
}

int cmd_rbdrive(int argc, char **argv)
{
    uint32_t len = 0;
    uint8_t *data;
    struct pls pls = { NULL, 0, 0 };
    struct entries es = { NULL, 0, 0 };
    size_t i, k;

    if (argc != 2) {
        fprintf(stderr, "usage: trakcrate-helper rbdrive DRIVE\n");
        return TC_USAGE;
    }
    data = read_pdb(argv[1], &len);
    if (data == NULL) {
        return TC_FAIL;
    }
    if (pdb_parse(data, len, emit_track, NULL) != 0) {
        fprintf(stderr, "trakcrate-helper: export.pdb could not be read\n");
        free(data);
        return TC_FAIL;
    }
    pdb_parse_playlists(data, len, pl_cb, &pls);
    pdb_parse_playlist_entries(data, len, entry_cb, &es);
    qsort(es.v, es.n, sizeof *es.v, entry_cmp);
    for (i = 0; i < pls.n; i++) {
        char name[1024] = "", part[512];
        size_t at = i;
        int depth;
        char *chain[16];
        int nparts = 0;

        if (pls.v[i].p.is_folder) {
            continue;
        }
        for (depth = 0; depth < 16; depth++) {
            size_t p;

            chain[nparts++] = pls.v[at].name;
            if (pls.v[at].p.parent_id == 0) {
                break;
            }
            for (p = 0; p < pls.n && pls.v[p].p.id != pls.v[at].p.parent_id; p++) {
            }
            if (p == pls.n) {
                break;
            }
            at = p;
        }
        while (nparts-- > 0) {
            clean_tsv(chain[nparts], part, sizeof part);
            snprintf(name + strlen(name), sizeof name - strlen(name), "%s%s", name[0] ? " - " : "", part);
        }
        printf("P\t%u\t%s\n", pls.v[i].p.id, name);
    }
    for (i = 0; i < pls.n; i++) {
        if (pls.v[i].p.is_folder) {
            continue;
        }
        for (k = 0; k < es.n; k++) {
            if (es.v[k].playlist_id == pls.v[i].p.id) {
                printf("E\t%u\t%u\n", es.v[k].playlist_id, es.v[k].track_id);
            }
        }
    }
    for (i = 0; i < pls.n; i++) {
        free(pls.v[i].name);
    }
    free(pls.v);
    free(es.v);
    free(data);
    return TC_OK;
}

/* ---- rbtrack ---- */

struct want_track {
    pdb_track_t t;
    char *title, *comment, *date_added, *release_date, *mix_name, *analyze_path, *file_path;
    int found;
};

static int find_cb(const pdb_track_t *t, void *ud)
{
    struct want_track *w = ud;

    if (t->id != w->t.id) {
        return 0;
    }
    w->t = *t;
    w->title = strdup(t->title ? t->title : "");
    w->comment = strdup(t->comment ? t->comment : "");
    w->date_added = strdup(t->date_added ? t->date_added : "");
    w->release_date = strdup(t->release_date ? t->release_date : "");
    w->mix_name = strdup(t->mix_name ? t->mix_name : "");
    w->analyze_path = strdup(t->analyze_path ? t->analyze_path : "");
    w->file_path = strdup(t->file_path ? t->file_path : "");
    w->found = 1;
    return 1;
}

struct album_find {
    uint32_t id;
    char name[512];
};

static int album_cb(const pdb_album_t *a, void *ud)
{
    struct album_find *f = ud;

    if (a->id == f->id) {
        snprintf(f->name, sizeof f->name, "%s", a->name ? a->name : "");
        return 1;
    }
    return 0;
}

static int set_if(tc_kvdoc *d, const char *key, const char *v)
{
    return tc_kv_set(d, key, v);
}

int cmd_rbtrack(int argc, char **argv)
{
    int do_grid = 1, do_cues = 1, do_wave = 1, do_art = 1, do_anlz = 1, a, rc = TC_FAIL;
    const char *drive = NULL, *dir = NULL;
    uint32_t id = 0, len = 0;
    uint8_t *data = NULL;
    struct want_track w;
    struct names artists = { NULL, 0, 0 }, genres = { NULL, 0, 0 }, labels = { NULL, 0, 0 }, keys = { NULL, 0, 0 }, colors = { NULL, 0, 0 }, art = { NULL, 0, 0 };
    struct album_find af;
    char path[PATH_MAX], buf[64];
    tc_kvdoc *meta = NULL, *source = NULL, *grid_doc = NULL;
    int got_grid = 0, got_cues = 0, got_wave = 0, got_art = 0, got_anlz = 0;
    anlz_track_t *at = NULL;
    struct tc_beatgrid g;

    memset(&w, 0, sizeof w);
    memset(&g, 0, sizeof g);
    for (a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--no-beatgrid") == 0) {
            do_grid = 0;
        } else if (strcmp(argv[a], "--no-cues") == 0) {
            do_cues = 0;
        } else if (strcmp(argv[a], "--no-waveform") == 0) {
            do_wave = 0;
        } else if (strcmp(argv[a], "--no-artwork") == 0) {
            do_art = 0;
        } else if (strcmp(argv[a], "--no-anlz") == 0) {
            do_anlz = 0;
        } else if (drive == NULL) {
            drive = argv[a];
        } else if (id == 0 && dir == NULL) {
            id = (uint32_t)strtoul(argv[a], NULL, 10);
        } else if (dir == NULL) {
            dir = argv[a];
        } else {
            drive = NULL;
            break;
        }
    }
    if (drive == NULL || dir == NULL || id == 0) {
        fprintf(stderr, "usage: trakcrate-helper rbtrack DRIVE ID DIR [--no-beatgrid] [--no-cues] [--no-waveform] [--no-artwork] [--no-anlz]\n");
        return TC_USAGE;
    }
    data = read_pdb(drive, &len);
    if (data == NULL) {
        return TC_FAIL;
    }
    w.t.id = id;
    pdb_parse(data, len, find_cb, &w);
    if (!w.found) {
        fprintf(stderr, "trakcrate-helper rbtrack: no track %u in the database\n", id);
        goto out;
    }
    pdb_parse_artists(data, len, names_cb, &artists);
    pdb_parse_genres(data, len, names_cb, &genres);
    pdb_parse_labels(data, len, names_cb, &labels);
    pdb_parse_keys(data, len, names_cb, &keys);
    pdb_parse_colors(data, len, names_cb, &colors);
    pdb_parse_artwork(data, len, names_cb, &art);
    af.id = w.t.album_id;
    af.name[0] = '\0';
    pdb_parse_albums(data, len, album_cb, &af);

    snprintf(path, sizeof path, "%s/meta.txt", dir);
    if (tc_kv_load(path, &meta, NULL) != 0) {
        fprintf(stderr, "trakcrate-helper rbtrack: cannot read %s\n", path);
        goto out;
    }
    snprintf(path, sizeof path, "%s/source.txt", dir);
    if (tc_kv_load(path, &source, NULL) != 0) {
        goto out;
    }
    set_if(meta, "title", w.title[0] ? w.title : tc_kv_get(meta, "title") ? tc_kv_get(meta, "title") : "");
    set_if(meta, "artist", name_of(&artists, w.t.artist_id));
    set_if(meta, "album", af.name);
    set_if(meta, "genre", name_of(&genres, w.t.genre_id));
    set_if(meta, "label", name_of(&labels, w.t.label_id));
    set_if(meta, "key", name_of(&keys, w.t.key_id));
    if (w.t.remixer_id) {
        set_if(meta, "remixer", name_of(&artists, w.t.remixer_id));
    }
    if (w.mix_name[0]) {
        set_if(meta, "mix_name", w.mix_name);
    }
    if (w.t.year) {
        tc_kv_set_int(meta, "year", w.t.year);
    }
    if (w.t.track_number) {
        tc_kv_set_int(meta, "track_number", w.t.track_number);
    }
    if (w.t.bpm_x100) {
        snprintf(buf, sizeof buf, "%.2f", w.t.bpm_x100 / 100.0);
        set_if(meta, "bpm_tag", buf);
    }
    tc_kv_set_int(meta, "rating", w.t.rating > 5 ? 5 : w.t.rating);
    if (w.t.color_id) {
        set_if(meta, "color", name_of(&colors, w.t.color_id));
    }
    set_if(meta, "comment", w.comment);
    if (w.date_added[0]) {
        set_if(meta, "date_added", w.date_added);
    }
    if (w.release_date[0]) {
        set_if(meta, "release_date", w.release_date);
    }
    if (w.t.duration_secs) {
        tc_kv_set_int(meta, "duration_ms", (long long)w.t.duration_secs * 1000);
    }
    tc_kv_set_int(meta, "rekordbox_track_id", id);
    set_if(source, "origin.meta", "rekordbox");
    tc_kv_set_int(source, "pdb_track_id", id);

    snprintf(path, sizeof path, "%s/rb-unused", dir);
    if (w.analyze_path[0] && anlz_track_open(drive, w.analyze_path, &at) == ANLZ_OK && at != NULL) {
        anlz_beatgrid_t bg;

        if (do_grid && anlz_track_beatgrid(at, &bg) == ANLZ_OK && bg.count > 0) {
            anlz_beat_t first, b;
            uint32_t i;
            int variable = 0;

            anlz_beat_at(&bg, 0, &first);
            g.bpm = first.bpm_x100 / 100.0;
            g.first_beat_us = (long long)first.time_signed_ms * 1000 + first.sub_ms_us;
            g.confidence = -1;
            snprintf(g.analyser, sizeof g.analyser, "rekordbox");
            for (i = 1; i < bg.count && !variable; i++) {
                if (anlz_beat_at(&bg, i, &b) && b.bpm_x100 != first.bpm_x100) {
                    variable = 1;
                }
            }
            g.variable_tempo = variable;
            tc_kv_load("/dev/null", &grid_doc, NULL);
            tc_kv_add_comment(grid_doc, "trakcrate beatgrid (from rekordbox)");
            tc_beatgrid_to(grid_doc, &g);
            if (variable) {
                fprintf(stderr, "warning: %s: variable tempo grid, trakcrate keeps the first tempo (the original ANLZ files are kept in anlz/)\n", w.title);
            }
            set_if(source, "origin.beatgrid", "rekordbox");
            got_grid = 1;
        }
        if (do_cues) {
            anlz_cue_list_t list;

            memset(&list, 0, sizeof list);
            if (anlz_track_cues(at, ANLZ_CUES_ALL, &list) == ANLZ_OK && list.count > 0) {
                struct tc_cuemarks cm;
                uint32_t i;
                double period = g.bpm > 0 ? 60000.0 / g.bpm : 0;
                char *text;
                size_t tl;

                memset(&cm, 0, sizeof cm);
                cm.header = strdup("# imported from rekordbox\n");
                cm.marks = calloc(list.count, sizeof *cm.marks);
                cm.n = list.count;
                for (i = 0; i < list.count; i++) {
                    const anlz_cue_t *c = &list.cues[i];
                    struct tc_cuemark *m = &cm.marks[i];
                    char lab[300], col[16], ext[48];

                    m->ms = c->time_ms;
                    m->beat = period > 0 ? (c->time_ms - g.first_beat_us / 1000.0) / period : NAN;
                    snprintf(lab, sizeof lab, "%s", c->comment);
                    if (c->hot_cue >= 1 && c->hot_cue <= 3) {
                        m->hot = (char)('A' + c->hot_cue - 1);
                    } else if (c->hot_cue > 3 && lab[0] == '\0') {
                        snprintf(lab, sizeof lab, "hot %c", 'A' + c->hot_cue - 1);
                    }
                    col[0] = '\0';
                    if (c->has_rgb) {
                        snprintf(col, sizeof col, "#%02x%02x%02x", c->red, c->green, c->blue);
                    }
                    ext[0] = '\0';
                    if (c->is_loop) {
                        snprintf(ext, sizeof ext, "loop_end_ms=%u", c->loop_time_ms);
                    }
                    m->label = strdup(lab);
                    m->colour = strdup(col);
                    m->extra = strdup(ext);
                }
                text = tc_cuemarks_serialise(&cm, &tl);
                snprintf(path, sizeof path, "%s/cuemarks.txt", dir);
                if (text != NULL && tc_write_atomic(path, text, tl) == 0) {
                    set_if(source, "origin.cues", "rekordbox");
                    got_cues = 1;
                }
                free(text);
                tc_cuemarks_free(&cm);
            }
            anlz_cue_list_free(&list);
        }
        if (do_wave) {
            struct tc_waveform wf;
            anlz_waveform_t aw, a3, a5;

            memset(&wf, 0, sizeof wf);
            if (anlz_track_waveform(at, ANLZ_WAVE_PREVIEW, &aw) == ANLZ_OK && aw.count == TC_WAVE_PREVIEW_COLUMNS && aw.entry_bytes == 1 &&
                anlz_track_waveform(at, ANLZ_WAVE_DETAIL, &a3) == ANLZ_OK && a3.count > 0 && a3.entry_bytes == 1 &&
                anlz_track_waveform(at, ANLZ_WAVE_COLOR_DETAIL, &a5) == ANLZ_OK && a5.count >= a3.count && a5.entry_bytes == 2) {
                uint32_t i;
                size_t sl;
                uint8_t *bytes;

                memcpy(wf.pwav, aw.data, sizeof wf.pwav);
                if (anlz_track_waveform(at, ANLZ_WAVE_TINY, &aw) == ANLZ_OK && aw.count == TC_WAVE_TINY_COLUMNS) {
                    memcpy(wf.pwv2, aw.data, sizeof wf.pwv2);
                }
                wf.columns = a3.count;
                wf.pwv5_extra = a5.count - a3.count;    /* kept as rekordbox wrote it (a few tracks have one more PWV5 column) */
                wf.pwv3 = malloc(wf.columns);
                wf.pwv5 = malloc(a5.count * sizeof *wf.pwv5);
                if (wf.pwv3 != NULL && wf.pwv5 != NULL) {
                    memcpy(wf.pwv3, a3.data, wf.columns);
                    for (i = 0; i < a5.count; i++) {
                        wf.pwv5[i] = (uint16_t)(a5.data[2 * i] << 8 | a5.data[2 * i + 1]);
                    }
                    bytes = tc_waveform_serialise(&wf, &sl);
                    snprintf(path, sizeof path, "%s/waveform.bin", dir);
                    if (bytes != NULL && tc_write_atomic(path, bytes, sl) == 0) {
                        set_if(source, "origin.waveform", "rekordbox");
                        got_wave = 1;
                    }
                    free(bytes);
                }
                tc_waveform_free(&wf);
            }
        }
        if (do_anlz) {
            static const struct { anlz_file_t which; const char *name; } files[] = { { ANLZ_FILE_DAT, "ANLZ0000.DAT" }, { ANLZ_FILE_EXT, "ANLZ0000.EXT" },
                                                                                   { ANLZ_FILE_2EX, "ANLZ0000.2EX" } };
            size_t f;
            char sub[PATH_MAX];

            snprintf(sub, sizeof sub, "%s/anlz", dir);
            for (f = 0; f < 3; f++) {
                char src[PATH_MAX], *bytes;
                size_t bl;

                if (anlz_resolve_path(drive, w.analyze_path, files[f].which, src, sizeof src) != 0 || tc_read_file(src, &bytes, &bl) != 0) {
                    continue;
                }
                if (tc_mkdir_p(sub) == 0) {
                    snprintf(path, sizeof path, "%s/%s", sub, files[f].name);
                    if (tc_write_atomic(path, bytes, bl) == 0) {
                        got_anlz = 1;
                    }
                }
                free(bytes);
            }
        }
    } else if (w.analyze_path[0]) {
        fprintf(stderr, "warning: %s: analysis files not readable (%s)\n", w.title, w.analyze_path);
    }
    if (do_art && w.t.artwork_id) {
        const char *rel = name_of(&art, w.t.artwork_id);
        char src[PATH_MAX], *bytes;
        size_t bl;

        if (rel[0] == '/' && strstr(rel, "..") == NULL) {
            snprintf(src, sizeof src, "%s%s", drive, rel);
            if (tc_read_file(src, &bytes, &bl) == 0) {
                snprintf(path, sizeof path, "%s/artwork.jpg", dir);
                if (tc_write_atomic(path, bytes, bl) == 0) {
                    set_if(source, "origin.artwork", "rekordbox");
                    got_art = 1;
                }
                free(bytes);
            }
        }
    }
    if (got_grid) {
        snprintf(path, sizeof path, "%s/beatgrid.txt", dir);
        if (tc_kv_save(grid_doc, path) != 0) {
            goto out;
        }
    }
    snprintf(path, sizeof path, "%s/meta.txt", dir);
    if (tc_kv_save(meta, path) != 0) {
        goto out;
    }
    snprintf(path, sizeof path, "%s/source.txt", dir);
    if (tc_kv_save(source, path) != 0) {
        goto out;
    }
    printf("%d\t%d\t%d\t%d\t%d\n", got_grid, got_cues, got_wave, got_art, got_anlz);
    rc = TC_OK;
out:
    if (at != NULL) {
        anlz_track_close(at);
    }
    tc_kv_free(meta);
    tc_kv_free(source);
    tc_kv_free(grid_doc);
    names_free(&artists);
    names_free(&genres);
    names_free(&labels);
    names_free(&keys);
    names_free(&colors);
    names_free(&art);
    free(w.title);
    free(w.comment);
    free(w.date_added);
    free(w.release_date);
    free(w.mix_name);
    free(w.analyze_path);
    free(w.file_path);
    free(data);
    return rc;
}
