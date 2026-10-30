#include "rbanlz.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "buf.h"

//SNIP_rbanlz

/* A tag: fourcc, header length, tag length, then header_body (the header after the first 12 bytes) and body. */
static void tag(struct tc_buf *out, const char *fourcc, const struct tc_buf *hb, const void *body, size_t body_len)
{
    tc_buf_put(out, fourcc, 4);
    tc_buf_be32(out, (uint32_t)(12 + hb->n));
    tc_buf_be32(out, (uint32_t)(12 + hb->n + body_len));
    tc_buf_put(out, hb->d, hb->n);
    tc_buf_put(out, body, body_len);
}

/* UTF-8 to UTF-16BE, NUL terminated */
static void utf16be(struct tc_buf *o, const char *s)
{
    size_t i = 0, len = strlen(s);

    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0xFFFD;
        size_t n = 1;

        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < len) {
            cp = (uint32_t)(c & 0x1F) << 6 | (uint32_t)(s[i + 1] & 0x3F);
            n = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < len) {
            cp = (uint32_t)(c & 0x0F) << 12 | (uint32_t)(s[i + 1] & 0x3F) << 6 | (uint32_t)(s[i + 2] & 0x3F);
            n = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < len) {
            cp = (uint32_t)(c & 0x07) << 18 | (uint32_t)(s[i + 1] & 0x3F) << 12 | (uint32_t)(s[i + 2] & 0x3F) << 6 | (uint32_t)(s[i + 3] & 0x3F);
            n = 4;
        }
        i += n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            tc_buf_be16(o, 0xD800 + (cp >> 10));
            tc_buf_be16(o, 0xDC00 + (cp & 0x3FF));
        } else {
            tc_buf_be16(o, cp);
        }
    }
    tc_buf_be16(o, 0);
}

static void ppth(struct tc_buf *out, const char *path)
{
    struct tc_buf p = { NULL, 0, 0, 0 }, hb = { NULL, 0, 0, 0 };

    utf16be(&p, path);
    tc_buf_be32(&hb, (uint32_t)p.n);
    tag(out, "PPTH", &hb, p.d, p.n);
    if (p.err) {
        out->err = 1;
    }
    tc_buf_free(&p);
    tc_buf_free(&hb);
}

static void pqtz(struct tc_buf *out, const struct tc_anlz_beat *beats, size_t n)
{
    struct tc_buf hb = { NULL, 0, 0, 0 }, body = { NULL, 0, 0, 0 };
    size_t i;

    tc_buf_be32(&hb, 0);
    tc_buf_be32(&hb, 0x00080000);                       /* unknown, the same in every real file */
    tc_buf_be32(&hb, (uint32_t)n);
    for (i = 0; i < n; i++) {
        tc_buf_be16(&body, beats[i].number);
        tc_buf_be16(&body, beats[i].bpm_x100);
        tc_buf_be32(&body, beats[i].time_ms);
    }
    tag(out, "PQTZ", &hb, body.d, body.n);
    out->err |= hb.err | body.err;
    tc_buf_free(&hb);
    tc_buf_free(&body);
}

static void pqt2(struct tc_buf *out, const struct tc_anlz_beat *beats, size_t n, const uint16_t *sub)
{
    struct tc_buf hb = { NULL, 0, 0, 0 }, body = { NULL, 0, 0, 0 };
    const struct tc_anlz_beat *f = &beats[0], *l = &beats[n - 1];
    size_t i;

    tc_buf_be32(&hb, 0);
    tc_buf_be32(&hb, 0x01000002);                       /* constant in every real file that has a grid */
    tc_buf_be32(&hb, 0);
    tc_buf_be16(&hb, f->number); tc_buf_be16(&hb, f->bpm_x100); tc_buf_be32(&hb, f->time_ms);
    tc_buf_be16(&hb, l->number); tc_buf_be16(&hb, l->bpm_x100); tc_buf_be32(&hb, l->time_ms);
    tc_buf_be32(&hb, (uint32_t)n);
    tc_buf_be32(&hb, 0);                                /* 0x2c: differs in every real file (a hash?); unknown, written as 0 */
    tc_buf_be32(&hb, 0);
    tc_buf_be32(&hb, 0);
    for (i = 0; i < n; i++) {
        tc_buf_be16(&body, sub != NULL ? sub[i] : 0);
    }
    tag(out, "PQT2", &hb, body.d, body.n);
    out->err |= hb.err | body.err;
    tc_buf_free(&hb);
    tc_buf_free(&body);
}

static void marker3(struct tc_buf *e, uint32_t m)
{
    tc_buf_u8(e, m >> 16);
    tc_buf_u8(e, m >> 8);
    tc_buf_u8(e, m);
}

static void cue_pcpt(struct tc_buf *e, const struct tc_anlz_cue *c)
{
    tc_buf_put(e, "PCPT", 4);
    tc_buf_be32(e, 0x1c);
    tc_buf_be32(e, 0x38);
    tc_buf_be32(e, (uint32_t)c->hot_cue);
    tc_buf_be32(e, 1);                                  /* status: 1 in every sample */
    tc_buf_be32(e, 0);
    tc_buf_be32(e, 0xffffffff);
    tc_buf_u8(e, c->is_loop ? 2 : 1);
    marker3(e, c->marker);
    tc_buf_be32(e, c->time_ms);
    if (c->is_loop) {                                   /* the three patterns seen: loop, plain cue, and the first cue "A" with marker 150 */
        tc_buf_be32(e, c->loop_time_ms);
        tc_buf_be32(e, 0xffffffff); tc_buf_be32(e, 0xffffffff); tc_buf_be32(e, 0xffffffff); tc_buf_be32(e, 0xffffffff);
    } else if (c->marker == 150) {
        tc_buf_be32(e, 0); tc_buf_be32(e, 2); tc_buf_be32(e, 0); tc_buf_be32(e, 0); tc_buf_be32(e, 0);
    } else {
        tc_buf_be32(e, 0); tc_buf_be32(e, 0xffffffff); tc_buf_be32(e, 0); tc_buf_be32(e, 0xffffffff); tc_buf_be32(e, 0);
    }
}

static void cue_pcp2(struct tc_buf *e, const struct tc_anlz_cue *c)
{
    tc_buf_put(e, "PCP2", 4);
    tc_buf_be32(e, 0x10);
    tc_buf_be32(e, 0x2c);
    tc_buf_be32(e, (uint32_t)c->hot_cue);
    tc_buf_u8(e, c->is_loop ? 2 : 1);
    marker3(e, c->marker);
    tc_buf_be32(e, c->time_ms);
    tc_buf_be32(e, c->is_loop ? c->loop_time_ms : 0);
    tc_buf_be32(e, 0);                                  /* 0x1c colour id and bytes: none */
    tc_buf_be32(e, 0);
    tc_buf_be16(e, c->is_loop ? c->loop_num : 0);
    tc_buf_be16(e, c->is_loop ? c->loop_den : 0);
    tc_buf_be32(e, 0);                                  /* comment length: no comment */
}

static void pcob(struct tc_buf *out, int list_type, const struct tc_anlz_cue *cues, size_t n)
{
    struct tc_buf hb = { NULL, 0, 0, 0 }, body = { NULL, 0, 0, 0 };
    size_t i;

    tc_buf_be32(&hb, (uint32_t)list_type);
    tc_buf_be16(&hb, 0);
    tc_buf_be16(&hb, (unsigned)n);
    tc_buf_be32(&hb, 0xffffffff);
    for (i = 0; i < n; i++) {
        cue_pcpt(&body, &cues[i]);
    }
    tag(out, "PCOB", &hb, body.d, body.n);
    out->err |= hb.err | body.err;
    tc_buf_free(&hb);
    tc_buf_free(&body);
}

static void pco2(struct tc_buf *out, int list_type, const struct tc_anlz_cue *cues, size_t n)
{
    struct tc_buf hb = { NULL, 0, 0, 0 }, body = { NULL, 0, 0, 0 };
    size_t i;

    tc_buf_be32(&hb, (uint32_t)list_type);
    tc_buf_be16(&hb, (unsigned)n);
    tc_buf_be16(&hb, 0);
    for (i = 0; i < n; i++) {
        cue_pcp2(&body, &cues[i]);
    }
    tag(out, "PCO2", &hb, body.d, body.n);
    out->err |= hb.err | body.err;
    tc_buf_free(&hb);
    tc_buf_free(&body);
}

static void pmai(struct tc_buf *b)
{
    tc_buf_put(b, "PMAI", 4);
    tc_buf_be32(b, 0x1c); tc_buf_be32(b, 0); tc_buf_be32(b, 0); tc_buf_be32(b, 0x00010000); tc_buf_be32(b, 0x00010000); tc_buf_be32(b, 0);
}

static void simple_tag(struct tc_buf *out, const char *name, uint32_t a, uint32_t b, const void *body, size_t len)
{
    struct tc_buf hb = { NULL, 0, 0, 0 };

    tc_buf_be32(&hb, a);
    tc_buf_be32(&hb, b);
    tag(out, name, &hb, body, len);
    out->err |= hb.err;
    tc_buf_free(&hb);
}

int tc_anlz_build(const struct tc_anlz_track *d, uint8_t **dat_out, size_t *dat_len, uint8_t **ext_out, size_t *ext_len, char *err, size_t errsz)
{
    struct tc_buf dat = { NULL, 0, 0, 0 }, ext = { NULL, 0, 0, 0 }, tmp = { NULL, 0, 0, 0 };
    size_t hot3 = d->nhot < 3 ? d->nhot : 3, i;
    const char *msg = NULL;

    if (d->nbeats > 65535) {
        msg = "too many beats";
    } else if (d->pwav == NULL) {
        msg = "PWAV needs 400 columns";
    } else if (d->pwv2 == NULL) {
        msg = "PWV2 needs 100 columns";
    } else if (d->include_pwv5 && d->pwv5 == NULL) {
        msg = "PWV3 and PWV5 need the same number of columns";
    }
    if (msg != NULL) {
        if (err != NULL && errsz > 0) {
            snprintf(err, errsz, "%s", msg);
        }
        return -1;
    }

    pmai(&dat);
    ppth(&dat, d->audio_path);
    if (d->include_pvbr) {      /* PVBR: the seek index of variable bitrate files. Layout unknown; all zero is a constant bitrate file. */
        struct tc_buf hb = { NULL, 0, 0, 0 };

        tc_buf_be32(&hb, 0);
        tc_buf_zeros(&tmp, 1604);
        tag(&dat, "PVBR", &hb, tmp.d, tmp.n);
        tc_buf_free(&hb);
        tc_buf_free(&tmp);
    }
    pqtz(&dat, d->beats, d->nbeats);
    simple_tag(&dat, "PWAV", 400, 0x00010000, d->pwav, 400);
    simple_tag(&dat, "PWV2", 100, 0x00010000, d->pwv2, 100);
    pcob(&dat, 1, d->hot_cues, hot3);
    pcob(&dat, 0, d->memory_cues, d->nmemory);
    if (dat.d != NULL) {
        tc_set_be32(dat.d + 8, (uint32_t)dat.n);
    }

    pmai(&ext);
    ppth(&ext, d->audio_path);
    {
        struct tc_buf hb = { NULL, 0, 0, 0 };

        tc_buf_be32(&hb, 1);
        tc_buf_be32(&hb, (uint32_t)d->columns);
        tc_buf_be32(&hb, 0x00960000);
        tag(&ext, "PWV3", &hb, d->pwv3, d->columns);
        tc_buf_free(&hb);
    }
    pcob(&ext, 1, NULL, 0);                             /* the .EXT has PCOB lists that are empty even when there are cues */
    pcob(&ext, 0, NULL, 0);
    pco2(&ext, 1, d->hot_cues, d->nhot);
    pco2(&ext, 0, d->memory_cues, d->nmemory);
    if (d->nbeats > 0) {
        pqt2(&ext, d->beats, d->nbeats, d->beat_sub_us);
    }
    if (d->include_pwv5) {
        struct tc_buf hb = { NULL, 0, 0, 0 }, body = { NULL, 0, 0, 0 };

        tc_buf_be32(&hb, 2);
        tc_buf_be32(&hb, (uint32_t)d->columns);
        tc_buf_be32(&hb, 0x00960305);
        for (i = 0; i < d->columns; i++) {
            tc_buf_be16(&body, d->pwv5[i]);
        }
        tag(&ext, "PWV5", &hb, body.d, body.n);
        ext.err |= hb.err | body.err;
        tc_buf_free(&hb);
        tc_buf_free(&body);
    }
    if (d->pwv4 != NULL) {
        struct tc_buf hb = { NULL, 0, 0, 0 };

        tc_buf_be32(&hb, 6);
        tc_buf_be32(&hb, 1200);
        tc_buf_be32(&hb, 0);
        tag(&ext, "PWV4", &hb, d->pwv4, 1200 * 6);
        tc_buf_free(&hb);
    }
    if (ext.d != NULL) {
        tc_set_be32(ext.d + 8, (uint32_t)ext.n);
    }
    if (dat.err || ext.err || dat.d == NULL || ext.d == NULL) {
        tc_buf_free(&dat);
        tc_buf_free(&ext);
        if (err != NULL && errsz > 0) {
            snprintf(err, errsz, "out of memory");
        }
        return -1;
    }
    *dat_out = dat.d;
    *dat_len = dat.n;
    *ext_out = ext.d;
    *ext_len = ext.n;
    return 0;
}

int tc_anlz_beats_from_grid(const struct tc_beatgrid *g, double duration_ms, struct tc_anlz_beat **beats, uint16_t **sub, size_t *n)
{
    size_t cap = 0, i;
    double period_us;
    uint16_t tempo;
    struct tc_anlz_beat *b = NULL;
    uint16_t *s = NULL;

    *beats = NULL;
    *sub = NULL;
    *n = 0;
    if (g->bpm <= 0 || duration_ms <= 0) {
        return 0;
    }
    period_us = 60e6 / g->bpm;
    tempo = (uint16_t)lround(g->bpm * 100.0);
    cap = (size_t)(duration_ms * 1000.0 / period_us) + 4;
    if (cap > 65535) {
        cap = 65535;
    }
    b = calloc(cap, sizeof *b);
    s = calloc(cap, sizeof *s);
    if (b == NULL || s == NULL) {
        free(b);
        free(s);
        return -1;
    }
    for (i = 0;; i++) {
        double us = (double)g->first_beat_us + (double)i * period_us;
        long long t;

        if (us / 1000.0 > duration_ms || *n >= 65535 || *n >= cap) {
            break;
        }
        t = llround(us);
        b[*n].number = (uint16_t)(i % 4 + 1);
        b[*n].bpm_x100 = tempo;
        b[*n].time_ms = (uint32_t)(t / 1000);
        s[*n] = (uint16_t)(t % 1000);
        (*n)++;
    }
    *beats = b;
    *sub = s;
    return 0;
}

uint8_t *tc_anlz_pwv4_from(const uint16_t *pwv5, size_t cols)
{
    uint8_t *o = calloc(1200 * 6, 1);
    size_t i, k;

    if (o == NULL) {
        return NULL;
    }
    if (cols == 0) {
        for (i = 0; i < 1200; i++) {
            o[i * 6 + 1] = 255;
        }
        return o;
    }
    for (i = 0; i < 1200; i++) {
        size_t a = i * cols / 1200, b = (i + 1) * cols / 1200;
        int h = 0, g = 0, bl = 0, h127;
        uint8_t *e = &o[i * 6];

        if (b < a + 1) {
            b = a + 1;
        }
        if (b > cols) {
            b = cols;
        }
        if (a >= cols) {
            a = cols - 1;
        }
        for (k = a; k < b; k++) {
            int hh = (pwv5[k] >> 2) & 0x1F, gg = (pwv5[k] >> 10) & 7, bb = (pwv5[k] >> 7) & 7;

            h = hh > h ? hh : h;
            g = gg > g ? gg : g;
            bl = bb > bl ? bb : bl;
        }
        h127 = h * 127 / 31;
        e[0] = (uint8_t)h127;
        e[1] = (uint8_t)(255 - h127);
        e[2] = (uint8_t)h127;
        e[3] = (uint8_t)h127;
        e[4] = (uint8_t)(g * 127 / 7);
        e[5] = (uint8_t)(bl * 127 / 7);
    }
    return o;
}

//SNIP_rbanlz
