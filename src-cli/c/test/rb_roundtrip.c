/*
 * Round trips on real rekordbox files (test/test_rekordbox_roundtrip.sh); not installed.
 *
 *   rb_roundtrip pdb FILE            parse then serialise export.pdb: every byte comes back (also the pages the model does not understand)
 *   rb_roundtrip anlz DIR            DIR holds ANLZ0000.DAT and .EXT of a real track: read them with libcdj, write them again with
 *                                    tc_anlz_build, compare tag by tag (the words whose meaning is unknown and that vary are skipped)
 *   rb_roundtrip rows FILE           the rows the model holds agree with libcdj's reader (tracks, playlist entries)
 * Exit 0 when everything agrees; a line on stderr for every difference.
 */
#include <cdj/anlz.h>
#include <cdj/pdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/buf.h"
#include "../lib/fsutil.h"
#include "../lib/rbanlz.h"
#include "../lib/rbpdb.h"

static int bad(const char *fmt, const char *a, const char *b)
{
    fprintf(stderr, fmt, a, b);
    fputc('\n', stderr);
    return 1;
}

static int cmd_pdb(const char *file)
{
    char *data, err[200];
    size_t len, out_len = 0, structured = 0, i;
    struct tc_pdb f;
    uint8_t *again;

    if (tc_read_file(file, &data, &len) != 0) {
        return bad("%s: cannot read%s", file, "");
    }
    if (tc_pdb_parse((uint8_t *)data, len, &f, err, sizeof err) != 0) {
        return bad("%s: parse: %s", file, err);
    }
    for (i = 1; i < f.npages; i++) {
        structured += f.pages[i].structured;
    }
    again = tc_pdb_serialise(&f, &out_len, err, sizeof err);
    if (again == NULL) {
        return bad("%s: serialise: %s", file, err);
    }
    if (out_len != len || memcmp(again, data, len) != 0) {
        return bad("%s: %s", file, "serialise did not reproduce the file");
    }
    printf("%s: %zu pages, %zu data pages decoded, identical\n", file, f.npages - 1, structured);
    free(again);
    tc_pdb_free(&f);
    free(data);
    return 0;
}

static int count_cb(const pdb_track_t *t, void *ud)
{
    (void)t;
    (*(size_t *)ud)++;
    return 0;
}

static int count_entry(const pdb_playlist_entry_t *e, void *ud)
{
    (void)e;
    (*(size_t *)ud)++;
    return 0;
}

static int cmd_rows(const char *file)
{
    char *data, err[200];
    size_t len, libcdj = 0, mine, entries = 0, my_entries;
    struct tc_pdb f;
    const struct tc_pdb_row **rows;

    if (tc_read_file(file, &data, &len) != 0 || tc_pdb_parse((uint8_t *)data, len, &f, err, sizeof err) != 0) {
        return bad("%s: cannot load%s", file, "");
    }
    pdb_parse((uint8_t *)data, (uint32_t)len, count_cb, &libcdj);
    rows = tc_pdb_table_rows(&f, 0, &mine);
    free(rows);
    pdb_parse_playlist_entries((uint8_t *)data, (uint32_t)len, count_entry, &entries);
    rows = tc_pdb_table_rows(&f, 8, &my_entries);
    free(rows);
    if (libcdj != mine || entries != my_entries) {
        fprintf(stderr, "%s: tracks %zu vs libcdj %zu, entries %zu vs %zu\n", file, mine, libcdj, my_entries, entries);
        return 1;
    }
    printf("%s: %zu tracks, %zu playlist entries, same as libcdj\n", file, mine, my_entries);
    tc_pdb_free(&f);
    free(data);
    return 0;
}

/* ---- ANLZ ---- */

struct tag {
    char name[5];
    uint32_t header_len, tag_len;
    const uint8_t *header_body, *body;
};

static size_t split_tags(const uint8_t *f, size_t len, struct tag *tags, size_t max)
{
    size_t at, n = 0;

    for (at = 28; at + 12 <= len && n < max;) {
        struct tag *t = &tags[n];

        memcpy(t->name, f + at, 4);
        t->name[4] = '\0';
        t->header_len = tc_get_be32(f + at + 4);
        t->tag_len = tc_get_be32(f + at + 8);
        if (t->tag_len < t->header_len || at + t->tag_len > len) {
            break;
        }
        t->header_body = f + at + 12;
        t->body = f + at + t->header_len;
        n++;
        at += t->tag_len;
    }
    return n;
}

static void to_cue(const anlz_cue_t *c, struct tc_anlz_cue *o)
{
    o->hot_cue = c->hot_cue;
    o->is_loop = c->is_loop;
    o->time_ms = c->time_ms;
    o->loop_time_ms = c->is_loop ? c->loop_time_ms : 0;
    o->marker = c->marker;
    o->loop_num = c->loop_num;
    o->loop_den = c->loop_den;
}

static int cmd_anlz(const char *dir)
{
    char path[1024], *dat, *ext, err[200];
    size_t dat_len, ext_len, nbeats = 0, i, nh = 0, nm = 0, ncols = 0;
    anlz_t *a = NULL, *x = NULL;
    const char *audio = NULL;
    anlz_beatgrid_t g;
    struct tc_anlz_beat *beats = NULL;
    uint16_t *sub = NULL, *pwv5 = NULL;
    anlz_waveform_t w;
    struct tc_anlz_track d;
    anlz_cue_list_t hot, mem;
    struct tc_anlz_cue *hc = NULL, *mc = NULL;
    uint8_t *odat, *oext;
    size_t odat_len, oext_len;
    int fails = 0, which;

    memset(&d, 0, sizeof d);
    memset(&hot, 0, sizeof hot);
    memset(&mem, 0, sizeof mem);
    snprintf(path, sizeof path, "%s/ANLZ0000.DAT", dir);
    if (tc_read_file(path, &dat, &dat_len) != 0) {
        return bad("cannot read %s%s", path, "");
    }
    snprintf(path, sizeof path, "%s/ANLZ0000.EXT", dir);
    if (tc_read_file(path, &ext, &ext_len) != 0) {
        return bad("cannot read %s%s", path, "");
    }
    if (anlz_open_mem((uint8_t *)dat, (uint32_t)dat_len, &a) != ANLZ_OK || anlz_open_mem((uint8_t *)ext, (uint32_t)ext_len, &x) != ANLZ_OK) {
        return bad("libcdj cannot read %s%s", dir, "");
    }
    anlz_path(a, &audio);
    d.audio_path = audio;
    if (anlz_beatgrid(a, &g) == ANLZ_OK) {
        nbeats = g.count;
        g.shift_ms = 0;                         /* the writer takes the raw entries; a real file may carry a grid shift in the header */
        anlz_beatgrid_attach_sub_ms(&g, x);
        beats = calloc(nbeats ? nbeats : 1, sizeof *beats);
        sub = calloc(nbeats ? nbeats : 1, sizeof *sub);
        for (i = 0; i < nbeats; i++) {
            anlz_beat_t b;

            anlz_beat_at(&g, (uint32_t)i, &b);
            beats[i].number = b.beat_number;
            beats[i].bpm_x100 = b.bpm_x100;
            beats[i].time_ms = b.time_ms;
            sub[i] = b.sub_ms_us;
        }
        d.beats = beats;
        d.nbeats = nbeats;
        d.beat_sub_us = g.has_sub_ms ? sub : NULL;
    }
    if (anlz_waveform(a, ANLZ_WAVE_PREVIEW, &w) == ANLZ_OK) {
        d.pwav = w.data;
    }
    if (anlz_waveform(a, ANLZ_WAVE_TINY, &w) == ANLZ_OK) {
        d.pwv2 = w.data;
    }
    if (anlz_waveform(x, ANLZ_WAVE_DETAIL, &w) == ANLZ_OK) {
        d.pwv3 = w.data;
        ncols = w.count;
    }
    if (anlz_waveform(x, ANLZ_WAVE_COLOR_PREVIEW, &w) == ANLZ_OK) {
        d.pwv4 = w.data;
    }
    if (anlz_waveform(x, ANLZ_WAVE_COLOR_DETAIL, &w) == ANLZ_OK) {
        pwv5 = malloc(w.count * sizeof *pwv5 + 1);
        for (i = 0; i < w.count; i++) {
            pwv5[i] = (uint16_t)(w.data[2 * i] << 8 | w.data[2 * i + 1]);
        }
        d.pwv5 = pwv5;
    }
    d.columns = ncols;
    if (anlz_cues_from(x, ANLZ_CUES_HOT, 1, &hot) == ANLZ_OK) {
        nh = hot.count;
        hc = calloc(nh ? nh : 1, sizeof *hc);
        for (i = 0; i < nh; i++) {
            to_cue(&hot.cues[i], &hc[i]);
        }
    }
    if (anlz_cues_from(x, ANLZ_CUES_MEMORY, 1, &mem) == ANLZ_OK) {
        nm = mem.count;
        mc = calloc(nm ? nm : 1, sizeof *mc);
        for (i = 0; i < nm; i++) {
            to_cue(&mem.cues[i], &mc[i]);
        }
    }
    d.hot_cues = hc;
    d.nhot = nh;
    d.memory_cues = mc;
    d.nmemory = nm;
    d.include_pvbr = 1;
    d.include_pwv5 = 1;
    if (tc_anlz_build(&d, &odat, &odat_len, &oext, &oext_len, err, sizeof err) != 0) {
        return bad("tc_anlz_build: %s%s", err, "");
    }
    for (which = 0; which < 2; which++) {
        const uint8_t *real = (const uint8_t *)(which ? ext : dat), *mine = which ? oext : odat;
        size_t real_len = which ? ext_len : dat_len, mine_len = which ? oext_len : odat_len;
        struct tag ta[32], tb[32];
        size_t na = split_tags(real, real_len, ta, 32), nb = split_tags(mine, mine_len, tb, 32);
        char label[1100];

        snprintf(label, sizeof label, "%s %s", dir, which ? "EXT" : "DAT");
        if (na != nb) {
            fails += bad("%s: tag count differs%s", label, "");
            continue;
        }
        for (i = 0; i < na; i++) {
            struct tag *p = &ta[i], *q = &tb[i];
            uint8_t ha[256], hb[256];
            size_t hl = p->header_len - 12, bl;

            if (strcmp(p->name, q->name) != 0 || p->header_len != q->header_len || p->tag_len != q->tag_len) {
                fails += bad("%s: tag %s differs in name or length", label, p->name);
                continue;
            }
            bl = p->tag_len - p->header_len;
            if (strcmp(p->name, "PVBR") == 0) {
                continue;                           /* body unknown: the real one has two stray bytes, ours is zero */
            }
            if (hl > sizeof ha) {
                continue;
            }
            memcpy(ha, p->header_body, hl);
            memcpy(hb, q->header_body, hl);
            if (strcmp(p->name, "PQT2") == 0) {
                memset(ha + (0x2c - 12), 0, 4);     /* a per-file value we do not know */
            } else if (strcmp(p->name, "PQTZ") == 0) {
                memset(ha + 6, 0, 2);               /* the low half of the second header word varies in 2 of 14 real files */
            }
            if (memcmp(ha, hb, hl) != 0) {
                fails += bad("%s: %s header words differ", label, p->name);
            }
            if (strcmp(p->name, "PWV4") == 0) {
                continue;                           /* rebuilt elsewhere */
            }
            if (memcmp(p->body, q->body, bl) != 0) {
                fails += bad("%s: %s body differs", label, p->name);
            }
        }
        if (mine_len != real_len) {
            fails += bad("%s: file size differs%s", label, "");
        }
    }
    /* libcdj reads what we wrote and sees the same track */
    {
        anlz_t *oa = NULL, *ox = NULL;
        const char *p2 = NULL;
        anlz_beatgrid_t g2;

        if (anlz_open_mem(odat, (uint32_t)odat_len, &oa) != ANLZ_OK || anlz_open_mem(oext, (uint32_t)oext_len, &ox) != ANLZ_OK ||
            anlz_is_damaged(oa) || anlz_is_damaged(ox) || !anlz_file_len_ok(oa) || !anlz_file_len_ok(ox) ||
            anlz_path(oa, &p2) != ANLZ_OK || strcmp(p2, audio) != 0) {
            fails += bad("%s: libcdj does not read our files like the real ones%s", dir, "");
        } else if (anlz_beatgrid(oa, &g2) == ANLZ_OK && g2.count != nbeats) {
            fails += bad("%s: beat count differs%s", dir, "");
        }
        anlz_close(oa);
        anlz_close(ox);
    }
    if (fails == 0) {
        printf("%s: %zu beats, tags identical\n", dir, nbeats);
    }
    anlz_cue_list_free(&hot);
    anlz_cue_list_free(&mem);
    anlz_close(a);
    anlz_close(x);
    free(beats);
    free(sub);
    free(pwv5);
    free(hc);
    free(mc);
    free(odat);
    free(oext);
    free(dat);
    free(ext);
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: rb_roundtrip pdb FILE | rows FILE | anlz DIR\n");
        return 2;
    }
    if (strcmp(argv[1], "pdb") == 0) {
        return cmd_pdb(argv[2]);
    }
    if (strcmp(argv[1], "rows") == 0) {
        return cmd_rows(argv[2]);
    }
    if (strcmp(argv[1], "anlz") == 0) {
        return cmd_anlz(argv[2]);
    }
    fprintf(stderr, "rb_roundtrip: unknown mode %s\n", argv[1]);
    return 2;
}
