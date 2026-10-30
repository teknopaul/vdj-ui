/*
 * pdb FILE [--table NAME] [--pages]     tables of an export.pdb as TSV, one row per line: table TAB columns ...
 *                                       tables: tracks artists albums genres labels artwork playlists entries; --pages: the page list
 * anlz FILE [--tag NAME] [--raw] [--beats] [--unknown]
 *                                       the tags of an ANLZ file (.DAT/.EXT/.2EX) and what libcdj decodes from them, TSV
 *                                       --raw: hex of the header words and body of --tag; --beats: every beat; --unknown: the words whose
 *                                       meaning is not known (doc/trakcrate/UNKNOWN_BYTES.md) with their values
 * Read only. Output is stable, so two dumps can be compared with diff.
 */
#define _GNU_SOURCE
#include <cdj/anlz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/buf.h"
#include "../lib/fsutil.h"
#include "../lib/rbdb.h"
#include "../lib/rbpdb.h"
#include "../lib/status.h"

static void tsv(const char *s)
{
    for (; *s; s++) {
        putchar((*s == '\t' || *s == '\n' || *s == '\r') ? ' ' : *s);
    }
}

static void cell(const char *s)
{
    putchar('\t');
    tsv(s);
}

int cmd_pdb(int argc, char **argv)
{
    const char *file = NULL, *only = NULL;
    int pages = 0, a;
    char *data, err[200];
    size_t len, i;

    for (a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--table") == 0 && a + 1 < argc) {
            only = argv[++a];
        } else if (strcmp(argv[a], "--pages") == 0) {
            pages = 1;
        } else if (argv[a][0] != '-' && file == NULL) {
            file = argv[a];
        } else {
            file = NULL;
            break;
        }
    }
    if (file == NULL) {
        fprintf(stderr, "usage: trakcrate-helper pdb FILE [--table NAME] [--pages]\n");
        return TC_USAGE;
    }
    if (tc_read_file(file, &data, &len) != 0) {
        fprintf(stderr, "trakcrate-helper pdb: cannot read %s\n", file);
        return TC_FAIL;
    }
    if (pages) {
        struct tc_pdb f;

        if (tc_pdb_parse((uint8_t *)data, len, &f, err, sizeof err) != 0) {
            fprintf(stderr, "trakcrate-helper pdb: %s\n", err);
            return TC_FAIL;
        }
        printf("header\tpages=%zu\tnext_unused=%u\tunknown10=%u\tsequence=%u\n", f.npages, f.next_unused_page, f.unknown10, f.sequence);
        for (i = 0; i < f.ntables; i++) {
            printf("table\t%u\tfirst=%u\tlast=%u\tempty_candidate=%u\n", f.tables[i].type, f.tables[i].first_page, f.tables[i].last_page, f.tables[i].empty_candidate);
        }
        for (i = 1; i < f.npages; i++) {
            const struct tc_pdb_page *p = &f.pages[i];
            size_t present = 0, r;

            for (r = 0; r < p->nrows; r++) {
                present += p->rows[r].present;
            }
            printf("page\t%zu\ttype=%u\tnext=%u\tflags=0x%02x\t%s\trows=%zu\tpresent=%zu\n", i, p->type, p->next, p->flags, p->structured ? "data" : "raw", p->nrows, present);
        }
        tc_pdb_free(&f);
    } else {
        struct tc_rbdb db;

        if (tc_rbdb_load((uint8_t *)data, len, &db, err, sizeof err) != 0) {
            fprintf(stderr, "trakcrate-helper pdb: %s\n", err);
            return TC_FAIL;
        }
#define WANT(n) (only == NULL || strcmp(only, n) == 0)
        if (WANT("tracks")) {
            for (i = 0; i < db.ntracks; i++) {
                const struct tc_rbdb_track *t = &db.tracks[i];
                const struct tc_rbdb_named *n;

                printf("tracks\t%u", t->id);
                cell(t->title);
                n = tc_rbdb_find(db.artists, db.nartists, t->artist_id);
                cell(n ? n->name : "");
                n = tc_rbdb_find(db.albums, db.nalbums, t->album_id);
                cell(n ? n->name : "");
                n = tc_rbdb_find(db.genres, db.ngenres, t->genre_id);
                cell(n ? n->name : "");
                n = tc_rbdb_find(db.labels, db.nlabels, t->label_id);
                cell(n ? n->name : "");
                printf("\t%u\t%u\t%u\t%u\t%u\t%u\t%u", t->bpm_x100, t->duration_secs, t->rating, t->year, t->track_number, t->sample_rate, t->file_size);
                cell(t->file_path);
                cell(t->analyze_path);
                putchar('\n');
            }
        }
        if (WANT("artists") || WANT("genres") || WANT("labels") || WANT("artwork") || WANT("albums")) {
            struct { const char *name; const struct tc_rbdb_named *v; size_t n; } l[5] = {
                { "artists", db.artists, db.nartists }, { "genres", db.genres, db.ngenres }, { "labels", db.labels, db.nlabels },
                { "artwork", db.artwork, db.nartwork }, { "albums", db.albums, db.nalbums } };
            int k;

            for (k = 0; k < 5; k++) {
                if (!WANT(l[k].name)) {
                    continue;
                }
                for (i = 0; i < l[k].n; i++) {
                    printf("%s\t%u", l[k].name, l[k].v[i].id);
                    if (k == 4) {
                        printf("\t%u", l[k].v[i].artist_id);
                    }
                    cell(l[k].v[i].name);
                    putchar('\n');
                }
            }
        }
        if (WANT("playlists")) {
            for (i = 0; i < db.nplaylists; i++) {
                printf("playlists\t%u\t%u\t%u\t%d", db.playlists[i].id, db.playlists[i].parent_id, db.playlists[i].sort_order, db.playlists[i].is_folder);
                cell(db.playlists[i].name);
                putchar('\n');
            }
        }
        if (WANT("entries")) {
            for (i = 0; i < db.nentries; i++) {
                printf("entries\t%u\t%u\t%u\n", db.entries[i].playlist_id, db.entries[i].entry_index, db.entries[i].track_id);
            }
        }
#undef WANT
        tc_rbdb_free(&db);
    }
    free(data);
    return TC_OK;
}

/* ---- anlz ---- */

static void hex(const uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        printf("%02x", p[i]);
    }
}

int cmd_anlz(int argc, char **argv)
{
    const char *file = NULL, *tag = NULL;
    int raw = 0, beats = 0, unknown = 0, a;
    char *data;
    size_t len, at;
    anlz_t *x = NULL;
    const char *path = NULL;

    for (a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--tag") == 0 && a + 1 < argc) {
            tag = argv[++a];
        } else if (strcmp(argv[a], "--raw") == 0) {
            raw = 1;
        } else if (strcmp(argv[a], "--beats") == 0) {
            beats = 1;
        } else if (strcmp(argv[a], "--unknown") == 0) {
            unknown = 1;
        } else if (argv[a][0] != '-' && file == NULL) {
            file = argv[a];
        } else {
            file = NULL;
            break;
        }
    }
    if (file == NULL) {
        fprintf(stderr, "usage: trakcrate-helper anlz FILE [--tag NAME] [--raw] [--beats] [--unknown]\n");
        return TC_USAGE;
    }
    if (tc_read_file(file, &data, &len) != 0) {
        fprintf(stderr, "trakcrate-helper anlz: cannot read %s\n", file);
        return TC_FAIL;
    }
    if (len < 28 || memcmp(data, "PMAI", 4) != 0 || anlz_open_mem((uint8_t *)data, (uint32_t)len, &x) != ANLZ_OK) {
        fprintf(stderr, "trakcrate-helper anlz: %s is not an ANLZ file\n", file);
        return TC_FAIL;
    }
    if (!tag && !unknown) {
        printf("file\tsize=%zu\tlength_field=%u\tdamaged=%d\n", len, tc_get_be32((uint8_t *)data + 8), anlz_is_damaged(x));
    }
    for (at = 28; at + 12 <= len;) {
        const uint8_t *t = (const uint8_t *)data + at;
        uint32_t hl = tc_get_be32(t + 4), tl = tc_get_be32(t + 8);
        char name[5];

        if (tl < hl || at + tl > len || hl < 12) {
            break;
        }
        memcpy(name, t, 4);
        name[4] = '\0';
        if (unknown) {
            if (strcmp(name, "PQTZ") == 0) {
                printf("PQTZ\theader+0x0e\tu16\t0x%04x\n", tc_get_be16(t + 14));
            } else if (strcmp(name, "PQT2") == 0 && hl >= 0x30) {
                printf("PQT2\theader+0x2c\tu32\t0x%08x\n", tc_get_be32(t + 0x2c));
            } else if (strcmp(name, "PVBR") == 0) {
                size_t i, nz = 0;

                for (i = hl; i < tl; i++) {
                    nz += t[i] != 0;
                }
                printf("PVBR\tbody\tnonzero-bytes\t%zu\n", nz);
            } else if (strcmp(name, "PCPT") == 0 || strcmp(name, "PCP2") == 0) {
                printf("%s\tmarker\tu24\t%u\n", name, (unsigned)(t[(name[3] == 'T' ? 0x1d : 0x11)] << 16 | t[(name[3] == 'T' ? 0x1e : 0x12)] << 8 | t[(name[3] == 'T' ? 0x1f : 0x13)]));
            }
        } else if (tag == NULL || strcmp(tag, name) == 0) {
            printf("tag\t%s\theader=%u\tlength=%u\n", name, hl, tl);
            if (raw && tag != NULL) {
                printf("header-words\t");
                hex(t + 12, hl - 12);
                printf("\nbody\t");
                hex(t + hl, tl - hl);
                putchar('\n');
            }
        }
        /* PCOB / PCO2 lists hold PCPT / PCP2 entries after their header: walk into them for --unknown */
        if (unknown && (strcmp(name, "PCOB") == 0 || strcmp(name, "PCO2") == 0)) {
            size_t e = at + hl;

            while (e + 12 <= at + tl) {
                const uint8_t *c = (const uint8_t *)data + e;
                uint32_t cl = tc_get_be32(c + 8);

                if (cl < 12 || e + cl > at + tl) {
                    break;
                }
                if (memcmp(c, "PCPT", 4) == 0 && cl >= 0x20) {
                    printf("PCPT\tmarker\tu24\t%u\n", (unsigned)(c[0x1d] << 16 | c[0x1e] << 8 | c[0x1f]));
                } else if (memcmp(c, "PCP2", 4) == 0 && cl >= 0x14) {
                    printf("PCP2\tmarker\tu24\t%u\n", (unsigned)(c[0x11] << 16 | c[0x12] << 8 | c[0x13]));
                }
                e += cl;
            }
        }
        at += tl;
    }
    if (!unknown) {
        anlz_beatgrid_t g;
        anlz_cue_list_t cl;
        static const struct { anlz_wave_kind_t k; const char *n; } waves[] = {
            { ANLZ_WAVE_PREVIEW, "PWAV" }, { ANLZ_WAVE_TINY, "PWV2" }, { ANLZ_WAVE_DETAIL, "PWV3" }, { ANLZ_WAVE_COLOR_PREVIEW, "PWV4" }, { ANLZ_WAVE_COLOR_DETAIL, "PWV5" } };
        size_t w, i;

        if ((tag == NULL || strcmp(tag, "PPTH") == 0) && anlz_path(x, &path) == ANLZ_OK) {
            printf("path\t%s\n", path);
        }
        if ((tag == NULL || strcmp(tag, "PQTZ") == 0) && anlz_beatgrid(x, &g) == ANLZ_OK) {
            anlz_beat_t b;

            if (g.count > 0) {
                anlz_beat_at(&g, 0, &b);
                printf("beats\tcount=%u\tfirst_bpm_x100=%u\tfirst_ms=%u\n", g.count, b.bpm_x100, b.time_ms);
            } else {
                printf("beats\tcount=0\n");
            }
            for (i = 0; beats && i < g.count; i++) {
                anlz_beat_at(&g, (uint32_t)i, &b);
                printf("beat\t%u\t%u\t%u\n", b.beat_number, b.bpm_x100, b.time_ms);
            }
        }
        for (a = 0; a < 2; a++) {
            const char *want = a ? "PCO2" : "PCOB";

            if (tag != NULL && strcmp(tag, want) != 0) {
                continue;
            }
            memset(&cl, 0, sizeof cl);
            if (anlz_cues_from(x, ANLZ_CUES_ALL, a, &cl) == ANLZ_OK) {
                for (i = 0; i < cl.count; i++) {
                    printf("cue\t%s\thot=%u\tloop=%u\tms=%u\tloop_end_ms=%u\tmarker=%u\n", want, cl.cues[i].hot_cue, cl.cues[i].is_loop, cl.cues[i].time_ms,
                           cl.cues[i].loop_time_ms, cl.cues[i].marker);
                }
                anlz_cue_list_free(&cl);
            }
        }
        for (w = 0; w < sizeof waves / sizeof *waves; w++) {
            anlz_waveform_t wf;

            if ((tag == NULL || strcmp(tag, waves[w].n) == 0) && anlz_waveform(x, waves[w].k, &wf) == ANLZ_OK) {
                printf("wave\t%s\tcolumns=%u\tentry_bytes=%u\n", waves[w].n, wf.count, wf.entry_bytes);
            }
        }
    }
    anlz_close(x);
    free(data);
    return TC_OK;
}
