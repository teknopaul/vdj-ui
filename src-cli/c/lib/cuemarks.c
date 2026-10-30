#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cuemarks.h"

//SNIP_cuemarks

static char *cm_dup_n(const char *s, size_t n)
{
    char *d = malloc(n + 1);

    if (d != NULL) {
        memcpy(d, s, n);
        d[n] = '\0';
    }
    return d;
}

/* a number, or NAN for "-" and for anything that is not one; *ok is 0 when it was neither a number nor "-" */
static double parse_num(const char *s, size_t n, int *ok)
{
    char buf[64], *end;
    double v;

    *ok = 1;
    if (n == 0 || (n == 1 && s[0] == '-')) {
        return NAN;
    }
    if (n >= sizeof buf) {
        *ok = 0;
        return NAN;
    }
    memcpy(buf, s, n);
    buf[n] = '\0';
    v = strtod(buf, &end);
    if (end == buf || *end != '\0') {
        *ok = 0;
        return NAN;
    }
    return v;
}

/* a number as the format writes it: at most 4 decimals, at least one ("33.0"); "-" for NAN */
static int fmt_num(char *buf, size_t sz, double v)
{
    char *dot, *last;
    int n;

    if (isnan(v)) {
        return snprintf(buf, sz, "-");
    }
    n = snprintf(buf, sz, "%.4f", v);
    dot = strchr(buf, '.');
    if (dot == NULL) {
        return n;
    }
    last = buf + n - 1;
    while (last > dot + 1 && *last == '0') {
        *last-- = '\0';
    }
    return (int)strlen(buf);
}

/* splits a line into fields: on TABs, or on blanks when there is no TAB. Returns the number of fields; start/len arrays are filled. */
static size_t split_fields(const char *line, size_t len, const char **start, size_t *flen, size_t max)
{
    size_t n = 0, i = 0;

    if (memchr(line, '\t', len) != NULL) {
        size_t s = 0;
        for (i = 0; i <= len; i++) {
            if (i == len || line[i] == '\t') {
                if (n < max) {
                    start[n] = line + s;
                    flen[n] = i - s;
                    n++;
                }
                s = i + 1;
            }
        }
        return n;
    }
    while (i < len) {
        while (i < len && isspace((unsigned char)line[i])) {
            i++;
        }
        if (i >= len) {
            break;
        }
        start[n] = line + i;
        while (i < len && !isspace((unsigned char)line[i])) {
            i++;
        }
        flen[n] = (size_t)(line + i - start[n]);
        n++;
        if (n == max) {
            break;
        }
    }
    return n;
}

int tc_cuemarks_parse(const char *text, struct tc_cuemarks *out)
{
    const char *p = text, *end = text + strlen(text);
    size_t hcap = 1, hlen = 0, cap = 0;
    int seen_data = 0;

    memset(out, 0, sizeof *out);
    out->header = malloc(1);
    if (out->header == NULL) {
        return -1;
    }
    out->header[0] = '\0';
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *le = nl ? nl : end;
        size_t len = (size_t)(le - p), i;
        const char *fs[64];
        size_t fl[64], nf;
        const char *q = p;

        if (len > 0 && le[-1] == '\r') {
            len--;
        }
        while (q < p + len && isspace((unsigned char)*q)) {
            q++;
        }
        if (q == p + len) {
            p = nl ? nl + 1 : end;
            continue;
        }
        if (*q == '#') {
            if (!seen_data) {
                char *nh = realloc(out->header, hcap + len + 1);
                if (nh == NULL) {
                    tc_cuemarks_free(out);
                    return -1;
                }
                out->header = nh;
                memcpy(out->header + hlen, p, len);
                hlen += len;
                out->header[hlen++] = '\n';
                out->header[hlen] = '\0';
                hcap = hlen + 1;
            }
            p = nl ? nl + 1 : end;
            continue;
        }
        seen_data = 1;
        nf = split_fields(p, len, fs, fl, 64);
        if (nf >= 2) {
            int ok1, ok2;
            double beat = parse_num(fs[0], fl[0], &ok1), ms = parse_num(fs[1], fl[1], &ok2);
            if (ok1 && ok2 && !isnan(ms)) {
                struct tc_cuemark *m;
                if (out->n == cap) {
                    struct tc_cuemark *nm;
                    cap = cap ? cap * 2 : 16;
                    nm = realloc(out->marks, cap * sizeof *nm);
                    if (nm == NULL) {
                        tc_cuemarks_free(out);
                        return -1;
                    }
                    out->marks = nm;
                }
                m = &out->marks[out->n++];
                memset(m, 0, sizeof *m);
                m->beat = beat;
                m->ms = ms;
                m->label = nf > 2 ? cm_dup_n(fs[2], fl[2]) : cm_dup_n("", 0);
                if (nf > 3 && fl[3] > 0 && fs[3][0] != '-') {
                    char h = (char)toupper((unsigned char)fs[3][0]);
                    if (h >= 'A' && h <= 'C') {
                        m->hot = h;
                    }
                }
                m->colour = (nf > 4 && !(fl[4] == 1 && fs[4][0] == '-')) ? cm_dup_n(fs[4], fl[4]) : cm_dup_n("", 0);
                if (nf > 5) {
                    size_t total = 0, o = 0;
                    for (i = 5; i < nf; i++) {
                        total += fl[i] + 1;
                    }
                    m->extra = malloc(total + 1);
                    if (m->extra != NULL) {
                        for (i = 5; i < nf; i++) {
                            if (i > 5) {
                                m->extra[o++] = '\t';
                            }
                            memcpy(m->extra + o, fs[i], fl[i]);
                            o += fl[i];
                        }
                        m->extra[o] = '\0';
                    }
                } else {
                    m->extra = cm_dup_n("", 0);
                }
                if (m->label == NULL || m->colour == NULL || m->extra == NULL) {
                    tc_cuemarks_free(out);
                    return -1;
                }
            }
        }
        p = nl ? nl + 1 : end;
    }
    return 0;
}

static int by_ms(const void *a, const void *b)
{
    const struct tc_cuemark *x = a, *y = b;

    if (x->ms < y->ms) {
        return -1;
    }
    return x->ms > y->ms;
}

char *tc_cuemarks_serialise(const struct tc_cuemarks *cm, size_t *len)
{
    struct tc_cuemark *sorted = NULL;
    size_t i, total = strlen(cm->header) + 1;
    char *out, *o;

    if (cm->n > 0) {
        /* qsort is not stable: sort an index by (ms, original position) instead */
        size_t *idx = malloc(cm->n * sizeof *idx);
        size_t a, b;
        if (idx == NULL) {
            return NULL;
        }
        sorted = malloc(cm->n * sizeof *sorted);
        if (sorted == NULL) {
            free(idx);
            return NULL;
        }
        for (i = 0; i < cm->n; i++) {
            idx[i] = i;
        }
        for (a = 1; a < cm->n; a++) {           /* insertion sort: stable, marks are few */
            size_t cur = idx[a];
            for (b = a; b > 0 && by_ms(&cm->marks[idx[b - 1]], &cm->marks[cur]) > 0; b--) {
                idx[b] = idx[b - 1];
            }
            idx[b] = cur;
        }
        for (i = 0; i < cm->n; i++) {
            sorted[i] = cm->marks[idx[i]];
        }
        free(idx);
    }
    for (i = 0; i < cm->n; i++) {
        total += 2 * 32 + strlen(cm->marks[i].label) + strlen(cm->marks[i].colour) + strlen(cm->marks[i].extra) + 16;
    }
    out = o = malloc(total);
    if (out == NULL) {
        free(sorted);
        return NULL;
    }
    o += sprintf(o, "%s", cm->header);
    for (i = 0; i < cm->n; i++) {
        const struct tc_cuemark *m = &sorted[i];
        o += fmt_num(o, 32, m->beat);
        *o++ = '\t';
        o += fmt_num(o, 32, m->ms);
        o += sprintf(o, "\t%s\t%c\t%s", m->label, m->hot ? m->hot : '-', m->colour[0] ? m->colour : "-");
        if (m->extra[0]) {
            o += sprintf(o, "\t%s", m->extra);
        }
        *o++ = '\n';
    }
    *o = '\0';
    if (len) {
        *len = (size_t)(o - out);
    }
    free(sorted);
    return out;
}

void tc_cuemarks_free(struct tc_cuemarks *cm)
{
    size_t i;

    for (i = 0; i < cm->n; i++) {
        free(cm->marks[i].label);
        free(cm->marks[i].colour);
        free(cm->marks[i].extra);
    }
    free(cm->marks);
    free(cm->header);
    memset(cm, 0, sizeof *cm);
}

//SNIP_cuemarks
