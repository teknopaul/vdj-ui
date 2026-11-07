/*
 * Test aid (src-cli/test/test_append_oracle.sh); not installed. The C counterpart of test/oracle/cxx_append.cpp: adds the plans to an export.pdb with
 * tc_append_playlist and prints the same summary lines.
 *
 *   append_plan IN.pdb TEMPLATE.pdb OUT.pdb PLAN...
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/fsutil.h"
#include "../lib/rbappend.h"

struct plan {
    struct tc_plan_track *tracks;
    size_t ntracks;
    struct tc_plan_playlist *lists;
    size_t nlists;
    char **owned;
    size_t nowned;
};

static char *own(struct plan *p, const char *s, size_t n)
{
    char *c = strndup(s, n);

    p->owned = realloc(p->owned, (p->nowned + 1) * sizeof *p->owned);
    p->owned[p->nowned++] = c;
    return c;
}

static int read_plan(const char *path, struct plan *p)
{
    FILE *f = fopen(path, "r");
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;

    memset(p, 0, sizeof *p);
    if (f == NULL) {
        return -1;
    }
    while ((n = getline(&line, &cap, f)) >= 0) {
        char *fld[12];
        int nf = 0;
        char *s = line;

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (n == 0) {
            continue;
        }
        for (;;) {
            char *tab = strchr(s, '\t');

            if (nf < 12) {
                fld[nf++] = s;
            }
            if (tab == NULL) {
                break;
            }
            *tab = '\0';
            s = tab + 1;
        }
        if (strcmp(fld[0], "T") == 0 && nf >= 10) {
            struct tc_plan_track t;
            const char *slash;

            memset(&t, 0, sizeof t);
            t.title = own(p, fld[1], strlen(fld[1]));
            t.artist = own(p, fld[2], strlen(fld[2]));
            t.album = own(p, fld[3], strlen(fld[3]));
            t.genre = own(p, fld[4], strlen(fld[4]));
            t.label = own(p, fld[5], strlen(fld[5]));
            t.file_path = own(p, fld[6], strlen(fld[6]));
            slash = strrchr(t.file_path, '/');
            t.file_name = slash != NULL ? slash + 1 : t.file_path;
            t.artwork_key = fld[7][0] ? own(p, fld[7], strlen(fld[7])) : NULL;
            t.artwork_path = fld[8][0] ? own(p, fld[8], strlen(fld[8])) : NULL;
            t.file_size = (uint32_t)strtoul(fld[9], NULL, 10);
            t.sample_rate = 44100;
            t.sample_depth = 16;
            t.date_added = "2026-11-07";
            p->tracks = realloc(p->tracks, (p->ntracks + 1) * sizeof *p->tracks);
            p->tracks[p->ntracks++] = t;
        } else if (strcmp(fld[0], "P") == 0 && nf >= 3) {
            struct tc_plan_playlist pl;
            size_t *idx = NULL, cnt = 0;
            char *q = fld[2];

            while (*q) {
                idx = realloc(idx, (cnt + 1) * sizeof *idx);
                idx[cnt++] = (size_t)strtoul(q, &q, 10);
                if (*q == ',') {
                    q++;
                }
            }
            pl.name = own(p, fld[1], strlen(fld[1]));
            pl.tracks = idx;
            pl.ntracks = cnt;
            p->lists = realloc(p->lists, (p->nlists + 1) * sizeof *p->lists);
            p->lists[p->nlists++] = pl;
        }
    }
    free(line);
    fclose(f);
    return 0;
}

static void free_plan(struct plan *p)
{
    size_t i;

    for (i = 0; i < p->nlists; i++) {
        free((void *)p->lists[i].tracks);
    }
    for (i = 0; i < p->nowned; i++) {
        free(p->owned[i]);
    }
    free(p->owned);
    free(p->tracks);
    free(p->lists);
}

int main(int argc, char **argv)
{
    char *in, *tpl;
    size_t inlen, tpllen, a, i;
    uint8_t *bytes;
    size_t len;
    FILE *out;

    if (argc < 5) {
        fprintf(stderr, "usage: append_plan IN.pdb TEMPLATE.pdb OUT.pdb PLAN...\n");
        return 2;
    }
    if (tc_read_file(argv[1], &in, &inlen) != 0 || tc_read_file(argv[2], &tpl, &tpllen) != 0) {
        fprintf(stderr, "cannot read the input\n");
        return 2;
    }
    bytes = (uint8_t *)in;
    len = inlen;
    for (a = 4; a < (size_t)argc; a++) {
        struct plan p;
        struct tc_export_plan ep;
        struct tc_drive_db db;
        struct tc_append_result r;
        char **problems, **diffs;
        size_t np, nd;
        char err[600];

        if (read_plan(argv[a], &p) != 0) {
            fprintf(stderr, "cannot read %s\n", argv[a]);
            return 2;
        }
        if (tc_drive_db_load(bytes, len, &db, &problems, &np) != 0) {
            printf("error: the database cannot be added to: %s\n", np > 0 ? problems[0] : "?");
            return 1;
        }
        ep.tracks = p.tracks;
        ep.ntracks = p.ntracks;
        ep.playlists = p.lists;
        ep.nplaylists = p.nlists;
        if (tc_append_playlist(&db, &ep, (const uint8_t *)tpl, tpllen, &r, err, sizeof err) != 0) {
            printf("error: %s\n", err);
            return 1;
        }
        nd = tc_verify_appended(bytes, len, r.bytes, r.len, &ep, &r, &diffs);
        if (nd > 0) {
            printf("error: verify: %s\n", diffs[0]);
            return 1;
        }
        printf("plan %d: rows %d pages %d playlist %u tracks", (int)a - 3, r.rows_added, r.pages_added, r.playlist_id);
        for (i = 0; i < r.ntracks; i++) {
            printf(" %u:%s:%s", r.track_ids[i], r.track_is_new[i] ? "new" : "old", r.analyze_paths[i] ? r.analyze_paths[i] : "");
        }
        printf(" artwork");
        for (i = 0; i < r.nartwork; i++) {
            printf(" %s", r.artwork_paths[i]);
        }
        printf("\n");
        free(bytes);
        bytes = r.bytes;
        len = r.len;
        r.bytes = NULL;
        tc_append_result_free(&r);
        tc_drive_db_free(&db);
        free_plan(&p);
    }
    out = fopen(argv[3], "wb");
    if (out == NULL || fwrite(bytes, 1, len, out) != len) {
        fprintf(stderr, "cannot write %s\n", argv[3]);
        return 2;
    }
    fclose(out);
    free(bytes);
    free(tpl);
    return 0;
}
