#include "rbpdb.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"
#include "sha256.h"

//SNIP_rbpdb

#define K_PAGE TC_PDB_PAGE_SIZE
#define K_HEAD TC_PDB_PAGE_HEADER
#define K_GROUP TC_PDB_GROUP_SIZE
#define K_PER TC_PDB_ROWS_PER_GROUP
#define K_EMPTY_ENTRY 0x03ffffffu

static void seterr(char *err, size_t sz, const char *fmt, ...) __attribute__((format(printf, 3, 4)));


static void seterr(char *err, size_t sz, const char *fmt, ...)
{
    va_list ap;

    if (err == NULL || sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(err, sz, fmt, ap);
    va_end(ap);
}

/* ---- page geometry ---- */

static size_t groups_for(size_t slots)
{
    return slots == 0 ? 0 : (slots - 1) / K_PER + 1;
}

/* Bytes the row index takes at the end of the page. Full groups take 36 bytes; the last group only as much as its used
 * offsets plus the two words (present bits, row flags): the unused offset slots below it are part of the free gap. */
static size_t index_span(size_t slots)
{
    size_t groups;

    if (slots == 0) {
        return 0;
    }
    groups = groups_for(slots);
    return (groups - 1) * K_GROUP + 4 + 2 * (slots - (groups - 1) * K_PER);
}

static size_t heap_bytes(const struct tc_pdb_page *p)
{
    size_t i, n = 0;

    for (i = 0; i < p->nrows; i++) {
        n += p->rows[i].len;
    }
    return n;
}

static size_t present_rows(const struct tc_pdb_page *p)
{
    size_t i, n = 0;

    for (i = 0; i < p->nrows; i++) {
        n += p->rows[i].present ? 1 : 0;
    }
    return n;
}

static int free_bytes(const struct tc_pdb_page *p)
{
    return (int)K_PAGE - (int)K_HEAD - (int)heap_bytes(p) - (2 * (int)p->nrows + 4 * (int)groups_for(p->nrows));
}

/* ---- memory ---- */

static void page_free(struct tc_pdb_page *p)
{
    size_t i;

    free(p->raw);
    for (i = 0; i < p->nrows; i++) {
        free(p->rows[i].bytes);
    }
    free(p->rows);
    free(p->group_flags);
    memset(p, 0, sizeof *p);
}

void tc_pdb_free(struct tc_pdb *f)
{
    size_t i;

    for (i = 0; i < f->npages; i++) {
        page_free(&f->pages[i]);
    }
    free(f->pages);
    free(f->tables);
    free(f->header_tail);
    memset(f, 0, sizeof *f);
}

/* a deep copy; 0, or -1 when out of memory */
static int page_clone(const struct tc_pdb_page *s, struct tc_pdb_page *d)
{
    size_t i;

    *d = *s;
    d->raw = NULL;
    d->rows = NULL;
    d->group_flags = NULL;
    if (s->raw != NULL) {
        d->raw = malloc(K_PAGE);
        if (d->raw == NULL) {
            return -1;
        }
        memcpy(d->raw, s->raw, K_PAGE);
    }
    if (s->nrows > 0) {
        d->rows = calloc(s->nrows, sizeof *d->rows);
        if (d->rows == NULL) {
            return -1;
        }
        for (i = 0; i < s->nrows; i++) {
            d->rows[i].len = s->rows[i].len;
            d->rows[i].present = s->rows[i].present;
            d->rows[i].bytes = malloc(s->rows[i].len ? s->rows[i].len : 1);
            if (d->rows[i].bytes == NULL) {
                return -1;
            }
            memcpy(d->rows[i].bytes, s->rows[i].bytes, s->rows[i].len);
        }
    }
    if (s->ngroups > 0) {
        d->group_flags = malloc(s->ngroups * sizeof *d->group_flags);
        if (d->group_flags == NULL) {
            return -1;
        }
        memcpy(d->group_flags, s->group_flags, s->ngroups * sizeof *d->group_flags);
    }
    return 0;
}

/* ---- parse ---- */

/* Decodes a data page into rows. 0 (page stays raw) when anything is not exactly what the writer would produce itself, so a
 * structured page always serialises back to the same bytes. -1 only when out of memory. */
static int decode_data_page(const uint8_t *pg, struct tc_pdb_page *p)
{
    uint32_t packed, present_count, present = 0;
    unsigned free_field, used;
    size_t slots, groups, g, i, heap_end, index_start;
    uint16_t *offs = NULL, *pres = NULL, *flg = NULL;
    struct tc_pdb_row *rows = NULL;
    int rc = 0;

    if (p->flags == 0 || (p->flags & 0x40)) {
        return 0;
    }
    packed = (uint32_t)pg[0x18] | (uint32_t)pg[0x19] << 8 | (uint32_t)pg[0x1a] << 16;
    slots = packed & 0x1FFF;
    present_count = packed >> 13;
    free_field = tc_get_le16(pg + 0x1c);
    used = tc_get_le16(pg + 0x1e);
    groups = groups_for(slots);
    if (slots == 0 || K_HEAD + (size_t)used + index_span(slots) > K_PAGE) {
        return 0;
    }
    offs = calloc(slots, sizeof *offs);
    pres = calloc(groups, sizeof *pres);
    flg = calloc(groups, sizeof *flg);
    rows = calloc(slots, sizeof *rows);
    if (offs == NULL || pres == NULL || flg == NULL || rows == NULL) {
        rc = -1;
        goto out;
    }
    for (g = 0; g < groups; g++) {
        size_t base = K_PAGE - g * K_GROUP, n = slots - g * K_PER;

        if (n > K_PER) {
            n = K_PER;
        }
        pres[g] = (uint16_t)tc_get_le16(pg + base - 4);
        flg[g] = (uint16_t)tc_get_le16(pg + base - 2);
        for (i = 0; i < n; i++) {
            offs[g * K_PER + i] = (uint16_t)tc_get_le16(pg + base - 6 - 2 * i);
        }
        if (n < K_PER) {
            uint16_t unused = (uint16_t)(0xFFFFu << n);

            if ((pres[g] & unused) || (flg[g] & unused)) {
                goto out;
            }
        }
    }
    for (i = 0; i < slots; i++) {                           /* rows lie back to back in slot order */
        uint32_t start = offs[i], end = i + 1 < slots ? offs[i + 1] : used;

        if ((i == 0 && start != 0) || end <= start || end > used) {
            goto out;
        }
    }
    heap_end = K_HEAD + used;
    index_start = K_PAGE - index_span(slots);
    for (i = heap_end; i < index_start; i++) {              /* the heap is followed by zeros up to the row index */
        if (pg[i] != 0) {
            goto out;
        }
    }
    for (i = 0; i < slots; i++) {
        uint32_t start = offs[i], end = i + 1 < slots ? offs[i + 1] : used;

        rows[i].len = end - start;
        rows[i].bytes = malloc(rows[i].len);
        if (rows[i].bytes == NULL) {
            rc = -1;
            goto out;
        }
        memcpy(rows[i].bytes, pg + K_HEAD + start, rows[i].len);
        rows[i].present = (pres[i / K_PER] >> (i % K_PER)) & 1;
        present += rows[i].present ? 1 : 0;
    }
    {
        struct tc_pdb_page tmp = *p;

        tmp.rows = rows;
        tmp.nrows = slots;
        tmp.group_flags = flg;
        tmp.ngroups = groups;
        if (present != present_count || free_field != (unsigned)free_bytes(&tmp)) {
            goto out;
        }
        free(p->raw);
        *p = tmp;
        p->raw = NULL;
        p->structured = 1;
        rows = NULL;
        flg = NULL;
    }
out:
    if (rows != NULL) {
        for (i = 0; i < slots; i++) {
            free(rows[i].bytes);
        }
        free(rows);
    }
    free(offs);
    free(pres);
    free(flg);
    return rc;
}

int tc_pdb_parse(const uint8_t *data, size_t len, struct tc_pdb *out, char *err, size_t errsz)
{
    struct tc_pdb f;
    uint32_t ntables, i;
    size_t count, n;

    memset(&f, 0, sizeof f);
    memset(out, 0, sizeof *out);
    if (len < K_PAGE || len % K_PAGE) {
        seterr(err, errsz, "size is not a multiple of the 4096 byte page");
        return -1;
    }
    if (tc_get_le32(data) != 0) {
        seterr(err, errsz, "bad header");
        return -1;
    }
    if (tc_get_le32(data + 4) != K_PAGE) {
        seterr(err, errsz, "page size is not 4096");
        return -1;
    }
    ntables = tc_get_le32(data + 8);
    if (ntables > (K_PAGE - 28) / 16) {
        seterr(err, errsz, "too many tables");
        return -1;
    }
    f.next_unused_page = tc_get_le32(data + 12);
    f.unknown10 = tc_get_le32(data + 16);
    f.sequence = tc_get_le32(data + 20);
    f.gap = tc_get_le32(data + 24);
    f.ntables = ntables;
    f.tables = calloc(ntables ? ntables : 1, sizeof *f.tables);
    f.header_tail_len = K_PAGE - 28 - 16 * (size_t)ntables;
    f.header_tail = malloc(f.header_tail_len ? f.header_tail_len : 1);
    count = len / K_PAGE;
    f.pages = calloc(count, sizeof *f.pages);
    f.npages = count;
    if (f.tables == NULL || f.header_tail == NULL || f.pages == NULL) {
        goto oom;
    }
    for (i = 0; i < ntables; i++) {
        f.tables[i].type = tc_get_le32(data + 28 + 16 * i);
        f.tables[i].empty_candidate = tc_get_le32(data + 32 + 16 * i);
        f.tables[i].first_page = tc_get_le32(data + 36 + 16 * i);
        f.tables[i].last_page = tc_get_le32(data + 40 + 16 * i);
    }
    memcpy(f.header_tail, data + 28 + 16 * (size_t)ntables, f.header_tail_len);
    for (n = 1; n < count; n++) {
        const uint8_t *pg = data + n * K_PAGE;
        struct tc_pdb_page *p = &f.pages[n];

        p->zero0 = tc_get_le32(pg);
        p->index = tc_get_le32(pg + 0x04);
        p->type = tc_get_le32(pg + 0x08);
        p->next = tc_get_le32(pg + 0x0c);
        p->u10 = tc_get_le32(pg + 0x10);
        p->u14 = tc_get_le32(pg + 0x14);
        p->flags = pg[0x1b];
        p->u20 = (uint16_t)tc_get_le16(pg + 0x20);
        p->num_large = (uint16_t)tc_get_le16(pg + 0x22);
        p->u24 = (uint16_t)tc_get_le16(pg + 0x24);
        p->u26 = (uint16_t)tc_get_le16(pg + 0x26);
        p->raw = malloc(K_PAGE);
        if (p->raw == NULL) {
            goto oom;
        }
        memcpy(p->raw, pg, K_PAGE);
        if (decode_data_page(pg, p) != 0) {
            goto oom;
        }
    }
    *out = f;
    return 0;
oom:
    tc_pdb_free(&f);
    seterr(err, errsz, "out of memory");
    return -1;
}

/* ---- serialise ---- */

static int encode_data_page(const struct tc_pdb_page *p, uint8_t *pg, char *err, size_t errsz)
{
    size_t slots = p->nrows, groups = groups_for(slots), heap = heap_bytes(p), i, g, at = K_HEAD;
    uint32_t packed;

    if (slots == 0 || slots > 0x1FFF) {
        seterr(err, errsz, "a data page needs 1 to 8191 rows");
        return -1;
    }
    if (p->ngroups != groups) {
        seterr(err, errsz, "row flag words do not match the number of row groups");
        return -1;
    }
    if (heap > 0xFFFF || K_HEAD + heap + index_span(slots) > K_PAGE) {
        seterr(err, errsz, "rows do not fit in a page");
        return -1;
    }
    memset(pg, 0, K_PAGE);
    tc_set_le32(pg + 0x00, p->zero0);
    tc_set_le32(pg + 0x04, p->index);
    tc_set_le32(pg + 0x08, p->type);
    tc_set_le32(pg + 0x0c, p->next);
    tc_set_le32(pg + 0x10, p->u10);
    tc_set_le32(pg + 0x14, p->u14);
    packed = (uint32_t)slots | ((uint32_t)present_rows(p) << 13);
    pg[0x18] = (uint8_t)packed;
    pg[0x19] = (uint8_t)(packed >> 8);
    pg[0x1a] = (uint8_t)(packed >> 16);
    pg[0x1b] = p->flags;
    tc_set_le16(pg + 0x1c, (unsigned)free_bytes(p));
    tc_set_le16(pg + 0x1e, (unsigned)heap);
    tc_set_le16(pg + 0x20, p->u20);
    tc_set_le16(pg + 0x22, p->num_large);
    tc_set_le16(pg + 0x24, p->u24);
    tc_set_le16(pg + 0x26, p->u26);
    for (i = 0; i < slots; i++) {
        size_t base = K_PAGE - (i / K_PER) * K_GROUP, k = i % K_PER;

        tc_set_le16(pg + base - 6 - 2 * k, (unsigned)(at - K_HEAD));
        if (p->rows[i].present) {
            tc_set_le16(pg + base - 4, tc_get_le16(pg + base - 4) | (1u << k));
        }
        memcpy(pg + at, p->rows[i].bytes, p->rows[i].len);
        at += p->rows[i].len;
    }
    for (g = 0; g < groups; g++) {
        tc_set_le16(pg + K_PAGE - g * K_GROUP - 2, p->group_flags[g]);
    }
    return 0;
}

uint8_t *tc_pdb_serialise(const struct tc_pdb *f, size_t *len, char *err, size_t errsz)
{
    uint8_t *res;
    size_t i, tail;

    if (f->npages == 0) {
        seterr(err, errsz, "no header page");
        return NULL;
    }
    tail = 28 + 16 * f->ntables;
    if (tail > K_PAGE || f->header_tail_len != K_PAGE - tail) {
        seterr(err, errsz, "header tail has the wrong size");
        return NULL;
    }
    res = calloc(f->npages, K_PAGE);
    if (res == NULL) {
        seterr(err, errsz, "out of memory");
        return NULL;
    }
    tc_set_le32(res + 4, K_PAGE);
    tc_set_le32(res + 8, (uint32_t)f->ntables);
    tc_set_le32(res + 12, f->next_unused_page);
    tc_set_le32(res + 16, f->unknown10);
    tc_set_le32(res + 20, f->sequence);
    tc_set_le32(res + 24, f->gap);
    for (i = 0; i < f->ntables; i++) {
        tc_set_le32(res + 28 + 16 * i, f->tables[i].type);
        tc_set_le32(res + 32 + 16 * i, f->tables[i].empty_candidate);
        tc_set_le32(res + 36 + 16 * i, f->tables[i].first_page);
        tc_set_le32(res + 40 + 16 * i, f->tables[i].last_page);
    }
    memcpy(res + tail, f->header_tail, f->header_tail_len);
    for (i = 1; i < f->npages; i++) {
        const struct tc_pdb_page *p = &f->pages[i];

        if (!p->structured) {
            if (p->raw == NULL) {
                seterr(err, errsz, "raw page %zu has no bytes", i);
                free(res);
                return NULL;
            }
            memcpy(res + i * K_PAGE, p->raw, K_PAGE);
        } else if (encode_data_page(p, res + i * K_PAGE, err, errsz) != 0) {
            char m[200];

            snprintf(m, sizeof m, "page %zu: %s", i, err);
            seterr(err, errsz, "%s", m);
            free(res);
            return NULL;
        }
    }
    *len = f->npages * K_PAGE;
    return res;
}

const struct tc_pdb_row **tc_pdb_table_rows(const struct tc_pdb *f, uint32_t type, size_t *n)
{
    size_t cap = 16, cnt = 0, i, guard;
    const struct tc_pdb_row **rows = malloc(cap * sizeof *rows);

    *n = 0;
    if (rows == NULL) {
        return NULL;
    }
    for (i = 0; i < f->ntables; i++) {
        uint32_t pn;

        if (f->tables[i].type != type) {
            continue;
        }
        pn = f->tables[i].first_page;
        for (guard = 0; pn > 0 && pn < f->npages && guard < f->npages; guard++) {
            const struct tc_pdb_page *p = &f->pages[pn];
            size_t r;

            if (p->type != type) {
                break;
            }
            if (p->structured) {
                for (r = 0; r < p->nrows; r++) {
                    if (!p->rows[r].present) {
                        continue;
                    }
                    if (cnt == cap) {
                        const struct tc_pdb_row **nr = realloc(rows, cap * 2 * sizeof *rows);

                        if (nr == NULL) {
                            free(rows);
                            return NULL;
                        }
                        rows = nr;
                        cap *= 2;
                    }
                    rows[cnt++] = &p->rows[r];
                }
            }
            if (pn == f->tables[i].last_page || p->next == pn) {
                break;
            }
            pn = p->next;
        }
        break;
    }
    *n = cnt;
    return rows;
}

/* ---- DeviceSQL strings ---- */

static int plain_ascii(const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;

        if (c < 0x20 || c >= 0x7f) {
            return 0;
        }
    }
    return 1;
}

static void put_utf16le_cp(struct tc_buf *o, uint32_t cp)
{
    if (cp >= 0x10000) {
        cp -= 0x10000;
        tc_buf_le16(o, 0xD800 + (cp >> 10));
        tc_buf_le16(o, 0xDC00 + (cp & 0x3FF));
    } else {
        tc_buf_le16(o, cp);
    }
}

/* UTF-8 to UTF-16LE; invalid bytes become U+FFFD */
static void utf8_to_utf16le(struct tc_buf *o, const char *s, size_t len)
{
    size_t i = 0;

    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0xFFFD;
        size_t n = 1;

        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < len && (s[i + 1] & 0xC0) == 0x80) {
            cp = (uint32_t)(c & 0x1F) << 6 | (uint32_t)(s[i + 1] & 0x3F);
            n = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < len && (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80) {
            cp = (uint32_t)(c & 0x0F) << 12 | (uint32_t)(s[i + 1] & 0x3F) << 6 | (uint32_t)(s[i + 2] & 0x3F);
            n = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < len && (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80 && (s[i + 3] & 0xC0) == 0x80) {
            cp = (uint32_t)(c & 0x07) << 18 | (uint32_t)(s[i + 1] & 0x3F) << 12 | (uint32_t)(s[i + 2] & 0x3F) << 6 | (uint32_t)(s[i + 3] & 0x3F);
            n = 4;
        }
        if (cp >= 0x110000 || (cp >= 0xD800 && cp < 0xE000)) {
            cp = 0xFFFD;
        }
        put_utf16le_cp(o, cp);
        i += n;
    }
}

/* Longest text stored in one string. A track row holds six of them and must fit in a 4096 byte page. */
#define K_MAX_STRING 500

uint8_t *tc_devsql_string(const char *utf8, size_t *len)
{
    struct tc_buf o = { NULL, 0, 0, 0 };
    size_t n = strlen(utf8);

    if (n > K_MAX_STRING) {
        size_t cut = K_MAX_STRING;

        while (cut > 0 && ((unsigned char)utf8[cut] & 0xC0) == 0x80) {
            cut--;
        }
        n = cut;
    }
    {
        char *tmp = strndup(utf8, n);

        if (tmp == NULL) {
            return NULL;
        }
        if (plain_ascii(tmp)) {
            if (n <= 126) {
                tc_buf_u8(&o, (unsigned)(((n + 1) << 1) | 1));     /* short ASCII: kind = (length + 1) << 1 | 1, empty = 0x03 */
            } else {
                tc_buf_u8(&o, 0x40);
                tc_buf_le16(&o, (unsigned)(n + 4));
                tc_buf_u8(&o, 0);
            }
            tc_buf_put(&o, tmp, n);
        } else {
            struct tc_buf u = { NULL, 0, 0, 0 };

            utf8_to_utf16le(&u, tmp, n);
            tc_buf_u8(&o, 0x90);
            tc_buf_le16(&o, (unsigned)(u.n + 4));
            tc_buf_u8(&o, 0);
            tc_buf_put(&o, u.d, u.n);
            if (u.err) {
                o.err = 1;
            }
            tc_buf_free(&u);
        }
        free(tmp);
    }
    if (o.err) {
        tc_buf_free(&o);
        return NULL;
    }
    *len = o.n;
    return o.d;
}

/* ---- row encoders (each returns a malloc'd row padded to a multiple of 4) ---- */

static void pad4(struct tc_buf *b)
{
    while (b->n % 4) {
        tc_buf_u8(b, 0);
    }
}

static void put_string(struct tc_buf *b, const char *s)
{
    size_t n;
    uint8_t *e = tc_devsql_string(s ? s : "", &n);

    if (e == NULL) {
        b->err = 1;
        return;
    }
    tc_buf_put(b, e, n);
    free(e);
}

static uint8_t *finish(struct tc_buf *b, size_t *len)
{
    pad4(b);
    if (b->err) {
        tc_buf_free(b);
        return NULL;
    }
    *len = b->n;
    return b->d;
}

static uint8_t *name_row(uint32_t id, const char *name, size_t *len)         /* genres, labels, artwork paths */
{
    struct tc_buf b = { NULL, 0, 0, 0 };

    tc_buf_le32(&b, id);
    put_string(&b, name);
    return finish(&b, len);
}

static uint8_t *artist_row(uint32_t id, const char *name, size_t *len)
{
    struct tc_buf b = { NULL, 0, 0, 0 };

    tc_buf_zeros(&b, 10);
    if (b.d != NULL) {
        tc_set_le16(b.d, 0x60);
        tc_set_le32(b.d + 4, id);
        b.d[8] = 0x03;
        b.d[9] = 0x0a;                                  /* the name starts right after this header */
    }
    put_string(&b, name);
    return finish(&b, len);
}

static uint8_t *album_row(uint32_t id, uint32_t artist_id, const char *name, size_t *len)
{
    struct tc_buf b = { NULL, 0, 0, 0 };

    tc_buf_zeros(&b, 22);
    if (b.d != NULL) {
        tc_set_le16(b.d, 0x80);
        tc_set_le32(b.d + 8, artist_id);
        tc_set_le32(b.d + 12, id);
        b.d[20] = 0x03;
        b.d[21] = 0x16;
    }
    put_string(&b, name);
    return finish(&b, len);
}

static uint8_t *playlist_row(uint32_t id, uint32_t sort_order, const char *name, size_t *len)
{
    struct tc_buf b = { NULL, 0, 0, 0 };

    tc_buf_le32(&b, 0);                                 /* parent: the root */
    tc_buf_le32(&b, 0);
    tc_buf_le32(&b, sort_order);
    tc_buf_le32(&b, id);
    tc_buf_le32(&b, 0);                                 /* not a folder */
    put_string(&b, name);
    return finish(&b, len);
}

static uint8_t *entry_row(uint32_t index, uint32_t track_id, uint32_t playlist_id, size_t *len)
{
    struct tc_buf b = { NULL, 0, 0, 0 };

    tc_buf_le32(&b, index);
    tc_buf_le32(&b, track_id);
    tc_buf_le32(&b, playlist_id);
    if (b.err) {
        tc_buf_free(&b);
        return NULL;
    }
    *len = b.n;
    return b.d;
}

/* The strings of a track row: 21 slots in the order the template row stores them. */
#define K_TRACK_FIXED 94
#define K_TRACK_SLOTS 21

static size_t devsql_length(const uint8_t *row, size_t rowlen, size_t at)
{
    unsigned kind;

    if (at >= rowlen) {
        return 0;
    }
    kind = row[at];
    if (kind == 0x40 || kind == 0x90) {
        return at + 2 < rowlen ? (size_t)row[at + 1] | (size_t)row[at + 2] << 8 : 0;
    }
    return kind >> 1;
}

struct track_template {
    uint8_t fixed[K_TRACK_FIXED];
    uint8_t *strings[K_TRACK_SLOTS];
    size_t string_len[K_TRACK_SLOTS];
    char default_date[32];
};

static void track_template_free(struct track_template *t)
{
    int i;

    for (i = 0; i < K_TRACK_SLOTS; i++) {
        free(t->strings[i]);
    }
}

static int load_track_template(const struct tc_pdb *t, struct track_template *out)
{
    size_t n, i;
    const struct tc_pdb_row **rows = tc_pdb_table_rows(t, 0, &n);
    const uint8_t *r;
    size_t rl;
    int ok = 0;

    memset(out, 0, sizeof *out);
    if (rows == NULL || n == 0 || rows[0]->len < (size_t)(K_TRACK_FIXED + K_TRACK_SLOTS * 2)) {
        goto out;
    }
    r = rows[0]->bytes;
    rl = rows[0]->len;
    memcpy(out->fixed, r, K_TRACK_FIXED);
    for (i = 0; i < K_TRACK_SLOTS; i++) {
        size_t ofs = tc_get_le16(r + K_TRACK_FIXED + 2 * i), len = devsql_length(r, rl, ofs);

        if (ofs + len > rl || len == 0) {
            goto out;
        }
        out->strings[i] = malloc(len);
        if (out->strings[i] == NULL) {
            goto out;
        }
        memcpy(out->strings[i], r + ofs, len);
        out->string_len[i] = len;
    }
    /* slot 10 holds the date added: "2026-09-20" in short ASCII form */
    if (out->string_len[10] > 1 && out->string_len[10] < sizeof out->default_date) {
        memcpy(out->default_date, out->strings[10] + 1, out->string_len[10] - 1);
    } else {
        snprintf(out->default_date, sizeof out->default_date, "2000-01-01");
    }
    ok = 1;
out:
    free(rows);
    if (!ok) {
        track_template_free(out);
    }
    return ok;
}

static uint8_t *track_row(const struct track_template *tpl, const struct tc_plan_track *t, uint32_t id, uint32_t genre_id, uint32_t album_id,
                          uint32_t artist_id, uint32_t label_id, uint32_t artwork_id, const char *analyze_path, size_t *len)
{
    uint8_t fixed[K_TRACK_FIXED];
    struct tc_buf row = { NULL, 0, 0, 0 };
    const char *date = t->date_added != NULL && t->date_added[0] ? t->date_added : tpl->default_date;
    uint8_t *s[K_TRACK_SLOTS];
    size_t sl[K_TRACK_SLOTS];
    int i, own[K_TRACK_SLOTS] = { 0 };

    memcpy(fixed, tpl->fixed, sizeof fixed);
    tc_set_le16(fixed + 0, 0x24);
    tc_set_le32(fixed + 8, t->sample_rate);
    tc_set_le32(fixed + 16, t->file_size);
    tc_set_le32(fixed + 20, 2406 + id);                 /* unknown; in the template it counts up with the track id */
    tc_set_le32(fixed + 28, artwork_id);
    tc_set_le32(fixed + 32, 0);                         /* key: the template has an empty keys table */
    tc_set_le32(fixed + 40, label_id);
    tc_set_le32(fixed + 44, 0);
    tc_set_le32(fixed + 48, t->bitrate_kbps);
    tc_set_le32(fixed + 52, t->track_number);
    tc_set_le32(fixed + 56, t->bpm_x100);
    tc_set_le32(fixed + 60, genre_id);
    tc_set_le32(fixed + 64, album_id);
    tc_set_le32(fixed + 68, artist_id);
    tc_set_le32(fixed + 72, id);
    tc_set_le16(fixed + 76, t->disc_number);
    tc_set_le16(fixed + 78, t->play_count);
    tc_set_le16(fixed + 80, t->year);
    tc_set_le16(fixed + 82, t->sample_depth);
    tc_set_le16(fixed + 84, t->duration_secs);
    fixed[88] = t->color_id;
    fixed[89] = t->rating;

    for (i = 0; i < K_TRACK_SLOTS; i++) {
        s[i] = tpl->strings[i];
        sl[i] = tpl->string_len[i];
    }
#define SLOT(n, str) do { s[n] = tc_devsql_string((str) ? (str) : "", &sl[n]); own[n] = 1; if (s[n] == NULL) goto oom; } while (0)
    memset(own, 0, sizeof own);
    SLOT(10, date);
    SLOT(11, "");
    SLOT(12, t->mix_name);
    SLOT(14, analyze_path);
    SLOT(15, date);
    SLOT(16, t->comment);
    SLOT(17, t->title);
    SLOT(19, t->file_name);
    SLOT(20, t->file_path);
#undef SLOT

    tc_buf_put(&row, fixed, sizeof fixed);
    tc_buf_zeros(&row, K_TRACK_SLOTS * 2);
    for (i = 0; i < K_TRACK_SLOTS; i++) {
        if (row.d != NULL) {
            tc_set_le16(row.d + K_TRACK_FIXED + 2 * i, (unsigned)row.n);
        }
        tc_buf_put(&row, s[i], sl[i]);
    }
    /* the template rows end with 38 to 70 zero bytes after the last string (reserve for edits); the length is a multiple of 4 */
    tc_buf_zeros(&row, 40);
    for (i = 0; i < K_TRACK_SLOTS; i++) {
        if (own[i]) {
            free(s[i]);
        }
    }
    return finish(&row, len);
oom:
    for (i = 0; i < K_TRACK_SLOTS; i++) {
        if (own[i]) {
            free(s[i]);
        }
    }
    tc_buf_free(&row);
    return NULL;
}

/* ---- pages ---- */

struct rowlist {
    uint8_t **bytes;
    size_t *len;
    size_t n, cap;
};

static int rowlist_add(struct rowlist *l, uint8_t *bytes, const size_t *lenp)
{
    size_t len = 0;

    if (bytes == NULL) {
        return -1;
    }
    len = *lenp;                    /* read after the row was encoded: the encoder fills it in */
    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 64;
        uint8_t **nb = realloc(l->bytes, nc * sizeof *nb);
        size_t *nl;

        if (nb == NULL) {
            free(bytes);
            return -1;
        }
        l->bytes = nb;
        nl = realloc(l->len, nc * sizeof *nl);
        if (nl == NULL) {
            free(bytes);
            return -1;
        }
        l->len = nl;
        l->cap = nc;
    }
    l->bytes[l->n] = bytes;
    l->len[l->n] = len;
    l->n++;
    return 0;
}

static void rowlist_free(struct rowlist *l)
{
    size_t i;

    for (i = 0; i < l->n; i++) {
        free(l->bytes[i]);
    }
    free(l->bytes);
    free(l->len);
    memset(l, 0, sizeof *l);
}

/* Splits the rows [from, n) of a list into pages: a row goes on the current page while heap + row index still fit. Returns the
 * count of rows that go on the page starting at `from`. */
static size_t page_rows(const struct rowlist *l, size_t from)
{
    size_t heap = 0, cnt = 0;

    while (from + cnt < l->n) {
        size_t slots = cnt + 1, groups = (slots - 1) / K_PER + 1;
        size_t span = (groups - 1) * K_GROUP + 4 + 2 * (slots - (groups - 1) * K_PER);

        if (cnt > 0 && (K_HEAD + heap + l->len[from + cnt] + span > K_PAGE || slots > 0x1FFF)) {
            break;
        }
        heap += l->len[from + cnt];
        cnt++;
    }
    return cnt;
}

struct idlist {
    char **names;
    uint32_t *artist;       /* albums only */
    char **aux;             /* artwork only: the path of the id */
    size_t n, cap;
};

static void idlist_free(struct idlist *l)
{
    size_t i;

    for (i = 0; i < l->n; i++) {
        free(l->names[i]);
        if (l->aux != NULL) {
            free(l->aux[i]);
        }
    }
    free(l->names);
    free(l->artist);
    free(l->aux);
    memset(l, 0, sizeof *l);
}

/* the id (index + 1) of a name, adding it when new; 0 for an empty name; (uint32_t)-1 when out of memory */
static uint32_t id_for(struct idlist *l, uint32_t artist, int with_artist, const char *name)
{
    size_t i;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    for (i = 0; i < l->n; i++) {
        if (strcmp(l->names[i], name) == 0 && (!with_artist || l->artist[i] == artist)) {
            return (uint32_t)(i + 1);
        }
    }
    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 64;
        char **nn = realloc(l->names, nc * sizeof *nn);
        uint32_t *na;

        if (nn == NULL) {
            return (uint32_t)-1;
        }
        l->names = nn;
        na = realloc(l->artist, nc * sizeof *na);
        if (na == NULL) {
            return (uint32_t)-1;
        }
        l->artist = na;
        {
            char **nx = realloc(l->aux, nc * sizeof *nx);

            if (nx == NULL) {
                return (uint32_t)-1;
            }
            l->aux = nx;
        }
        l->cap = nc;
    }
    l->names[l->n] = strdup(name);
    if (l->names[l->n] == NULL) {
        return (uint32_t)-1;
    }
    l->artist[l->n] = artist;
    l->aux[l->n] = NULL;
    return (uint32_t)++l->n;
}

void tc_built_pdb_free(struct tc_built_pdb *b)
{
    size_t i;

    free(b->bytes);
    if (b->analyze_paths != NULL) {
        for (i = 0; b->analyze_paths[i] != NULL; i++) {
            free(b->analyze_paths[i]);
        }
    }
    free(b->analyze_paths);
    if (b->artwork_paths != NULL) {
        for (i = 0; i < b->nartwork; i++) {
            free(b->artwork_paths[i]);
        }
    }
    free(b->artwork_paths);
    memset(b, 0, sizeof *b);
}

static int page_append(struct tc_pdb *f, size_t *cap, const struct tc_pdb_page *p)
{
    if (f->npages == *cap) {
        size_t nc = *cap ? *cap * 2 : 256;
        struct tc_pdb_page *np = realloc(f->pages, nc * sizeof *np);

        if (np == NULL) {
            return -1;
        }
        f->pages = np;
        *cap = nc;
    }
    f->pages[f->npages++] = *p;
    return 0;
}

#define NTABLES 20

int tc_pdb_build(const struct tc_export_plan *plan, const uint8_t *tpl_bytes, size_t tpl_len, struct tc_built_pdb *out, char *err, size_t errsz)
{
    struct tc_pdb tpl, f;
    struct track_template ttpl;
    struct idlist genre, label, artist, album, artwork;
    uint32_t *genre_id = NULL, *label_id = NULL, *artist_id = NULL, *album_id = NULL, *artwork_id = NULL;
    char **analyze = NULL;
    struct rowlist rows[NTABLES];
    size_t i, k, pcap = 0;
    uint32_t counter = 0;
    struct tc_built_pdb built;
    int have_tpl = 0, have_ttpl = 0, rc = -1;
    size_t t;

    memset(&f, 0, sizeof f);
    memset(&built, 0, sizeof built);
    memset(&genre, 0, sizeof genre);
    memset(&label, 0, sizeof label);
    memset(&artist, 0, sizeof artist);
    memset(&album, 0, sizeof album);
    memset(&artwork, 0, sizeof artwork);
    memset(rows, 0, sizeof rows);
    memset(out, 0, sizeof *out);

    if (tc_pdb_parse(tpl_bytes, tpl_len, &tpl, err, errsz) != 0) {
        return -1;
    }
    have_tpl = 1;
    if (tpl.ntables != NTABLES) {
        seterr(err, errsz, "the template must have 20 tables");
        goto out;
    }
    if (!load_track_template(&tpl, &ttpl)) {
        seterr(err, errsz, "the template has no usable track row");
        goto out;
    }
    have_ttpl = 1;
    for (i = 0; i < plan->nplaylists; i++) {
        for (k = 0; k < plan->playlists[i].ntracks; k++) {
            if (plan->playlists[i].tracks[k] >= plan->ntracks) {
                seterr(err, errsz, "playlist \"%s\" refers to a track that does not exist", plan->playlists[i].name);
                goto out;
            }
        }
    }

    /* ids: first seen, stable */
    {
        size_t n = plan->ntracks ? plan->ntracks : 1;

        genre_id = calloc(n, sizeof *genre_id);
        label_id = calloc(n, sizeof *label_id);
        artist_id = calloc(n, sizeof *artist_id);
        album_id = calloc(n, sizeof *album_id);
        artwork_id = calloc(n, sizeof *artwork_id);
        analyze = calloc(plan->ntracks + 1, sizeof *analyze);
        if (!genre_id || !label_id || !artist_id || !album_id || !artwork_id || !analyze) {
            goto oom;
        }
    }
    for (t = 0; t < plan->ntracks; t++) {
        const struct tc_plan_track *p = &plan->tracks[t];
        char buf[96];

        genre_id[t] = id_for(&genre, 0, 0, p->genre);
        label_id[t] = id_for(&label, 0, 0, p->label);
        artist_id[t] = id_for(&artist, 0, 0, p->artist);
        album_id[t] = id_for(&album, artist_id[t], 1, p->album);
        artwork_id[t] = id_for(&artwork, 0, 0, p->artwork_key);
        if (genre_id[t] == (uint32_t)-1 || label_id[t] == (uint32_t)-1 || artist_id[t] == (uint32_t)-1 || album_id[t] == (uint32_t)-1 ||
            artwork_id[t] == (uint32_t)-1) {
            goto oom;
        }
        /* the artwork path of an id is the one of the track that introduced it */
        if (artwork_id[t] != 0 && artwork.aux[artwork_id[t] - 1] == NULL) {
            artwork.aux[artwork_id[t] - 1] = strdup(p->artwork_path ? p->artwork_path : "");
            if (artwork.aux[artwork_id[t] - 1] == NULL) {
                goto oom;
            }
        }
        if (p->analyze_path != NULL && p->analyze_path[0]) {
            analyze[t] = strdup(p->analyze_path);
        } else {
            /* The real names look like P00C/0000D1E2: 3 and 8 hex digits. The rule is not known, but in all 2195 tracks of a real export the first
         * number is at most 0x7F and the second below 200003 (the slot count of USBANLZ/USBMNG.DAT), so both are kept in those ranges. */
            char hex[65], a3[4] = "", b8[9] = "";
            unsigned a = 0, b = 0;
            size_t j;

            if (p->audio_hash != NULL && p->audio_hash[0]) {
                snprintf(hex, sizeof hex, "%s", p->audio_hash);
            } else {
                tc_sha256_hex(p->file_path ? p->file_path : "", strlen(p->file_path ? p->file_path : ""), hex);
            }
            snprintf(a3, sizeof a3, "%.3s", hex);
            snprintf(b8, sizeof b8, "%.8s", strlen(hex) > 3 ? hex + 3 : "");
            sscanf(a3, "%x", &a);
            sscanf(b8, "%x", &b);
            for (;;) {
                int used = 0;

                snprintf(buf, sizeof buf, "/PIONEER/USBANLZ/P%03X/%08X/ANLZ0000.DAT", a % 128u, b % 200003u);
                for (j = 0; j < plan->ntracks && !used; j++) {
                    used = analyze[j] != NULL && strcmp(analyze[j], buf) == 0;
                }
                if (!used) {
                    break;
                }
                b++;                                    /* two tracks hashed alike: step to a free name */
            }
            analyze[t] = strdup(buf);
        }
        if (analyze[t] == NULL) {
            goto oom;
        }
    }
    /* explicit analyze paths must not be taken by a derived one that came first; the plan gives either all or none in practice */

    /* rows per table type */
    for (t = 0; t < plan->ntracks; t++) {
        size_t len = 0;

        if (rowlist_add(&rows[0], track_row(&ttpl, &plan->tracks[t], (uint32_t)(t + 1), genre_id[t], album_id[t], artist_id[t], label_id[t],
                                            artwork_id[t], analyze[t], &len), &len) != 0) {
            goto oom;
        }
    }
#define NAMES(tbl, list) \
    for (i = 0; i < (list).n; i++) { \
        size_t len; \
        if (rowlist_add(&rows[tbl], name_row((uint32_t)(i + 1), (list).names[i], &len), &len) != 0) goto oom; \
    }
    NAMES(1, genre)
    for (i = 0; i < artist.n; i++) {
        size_t len = 0;

        if (rowlist_add(&rows[2], artist_row((uint32_t)(i + 1), artist.names[i], &len), &len) != 0) {
            goto oom;
        }
    }
    for (i = 0; i < album.n; i++) {
        size_t len = 0;

        if (rowlist_add(&rows[3], album_row((uint32_t)(i + 1), album.artist[i], album.names[i], &len), &len) != 0) {
            goto oom;
        }
    }
    NAMES(4, label)
    for (i = 0; i < plan->nplaylists; i++) {
        size_t len = 0;

        if (rowlist_add(&rows[7], playlist_row((uint32_t)(i + 1), (uint32_t)i, plan->playlists[i].name, &len), &len) != 0) {
            goto oom;
        }
    }
    for (i = 0; i < plan->nplaylists; i++) {
        for (k = 0; k < plan->playlists[i].ntracks; k++) {
            size_t len = 0;

            if (rowlist_add(&rows[8], entry_row((uint32_t)(k + 1), (uint32_t)(plan->playlists[i].tracks[k] + 1), (uint32_t)(i + 1), &len), &len) != 0) {
                goto oom;
            }
        }
    }
    for (i = 0; i < artwork.n; i++) {
        size_t len = 0;

        if (rowlist_add(&rows[13], name_row((uint32_t)(i + 1), artwork.aux[i], &len), &len) != 0) {
            goto oom;
        }
    }
#undef NAMES
    for (i = 0; i < NTABLES; i++) {
        for (k = 0; k < rows[i].n; k++) {
            if (K_HEAD + rows[i].len[k] + 8 > K_PAGE) {
                seterr(err, errsz, "a row of table %zu is too large for a page", i);
                goto out;
            }
        }
    }

    /* lay the file out: for each table an index page, its data pages, then one blank page (the table's "empty candidate") */
    f.unknown10 = tpl.unknown10;
    f.gap = tpl.gap;
    f.header_tail_len = tpl.header_tail_len;
    f.header_tail = malloc(f.header_tail_len);
    f.tables = calloc(tpl.ntables, sizeof *f.tables);
    if (f.header_tail == NULL || f.tables == NULL) {
        goto oom;
    }
    memcpy(f.header_tail, tpl.header_tail, f.header_tail_len);
    f.ntables = tpl.ntables;
    {
        struct tc_pdb_page zero;

        memset(&zero, 0, sizeof zero);
        if (page_append(&f, &pcap, &zero) != 0) {
            goto oom;
        }
    }
    for (i = 0; i < tpl.ntables; i++) {
        const struct tc_pdb_table *tt = &tpl.tables[i];
        uint32_t type = tt->type, index_no = (uint32_t)f.npages, blank_no, first_data, nsrc = 0;
        int is_static = type == 6 || type == 16 || type == 17 || type == 18 || type == 19;
        struct tc_pdb_page *data = NULL;
        size_t ndata = 0, dcap = 0, d, owned = 0;
        struct tc_pdb_page idx;

        if (tt->first_page == 0 || tt->first_page >= tpl.npages || tpl.pages[tt->first_page].structured) {
            seterr(err, errsz, "the template has no index page for table %u", type);
            goto out;
        }
#define PUSH_DATA(pg) do { \
            if (ndata == dcap) { \
                size_t nc = dcap ? dcap * 2 : 8; \
                struct tc_pdb_page *nd = realloc(data, nc * sizeof *nd); \
                if (nd == NULL) { goto data_oom; } \
                data = nd; dcap = nc; \
            } \
            data[ndata++] = (pg); \
        } while (0)
        if (is_static) {
            uint32_t n = tt->first_page;
            size_t guard;

            for (guard = 0; n < tpl.npages && guard < tpl.npages; guard++) {
                const struct tc_pdb_page *p = &tpl.pages[n];

                if (p->type != type) {
                    break;
                }
                if (p->structured) {
                    struct tc_pdb_page c;

                    if (page_clone(p, &c) != 0) {
                        page_free(&c);
                        goto data_oom;
                    }
                    PUSH_DATA(c);
                }
                if (n == tt->last_page || p->next == n) {
                    break;
                }
                n = p->next;
            }
            (void)nsrc;
        } else if (rows[type].n > 0) {
            size_t from = 0;

            while (from < rows[type].n) {
                size_t cnt = page_rows(&rows[type], from), r;
                struct tc_pdb_page p;

                memset(&p, 0, sizeof p);
                p.structured = 1;
                p.type = type;
                p.flags = 0x24;
                p.u20 = 1;                              /* guess: 1 on every freshly written page of the big export */
                p.num_large = (uint16_t)(cnt - 1);      /* guess: "last slot" on freshly written pages */
                p.rows = calloc(cnt, sizeof *p.rows);
                p.nrows = cnt;
                p.ngroups = groups_for(cnt);
                p.group_flags = calloc(p.ngroups, sizeof *p.group_flags);
                if (p.rows == NULL || p.group_flags == NULL) {
                    page_free(&p);
                    goto data_oom;
                }
                for (r = 0; r < cnt; r++) {
                    p.rows[r].bytes = rows[type].bytes[from + r];     /* the page takes the row */
                    p.rows[r].len = rows[type].len[from + r];
                    p.rows[r].present = 1;
                    rows[type].bytes[from + r] = NULL;
                    if (type == 0 || type == 2 || type == 3) {
                        tc_set_le16(p.rows[r].bytes + 2, (unsigned)(r * 0x20));       /* the index shift */
                    }
                }
                for (r = 0; r < p.ngroups; r++) {
                    size_t n = cnt - r * K_PER;

                    if (n > K_PER) {
                        n = K_PER;
                    }
                    p.group_flags[r] = n == 16 ? 0xFFFFu : (uint16_t)((1u << n) - 1);
                }
                PUSH_DATA(p);
                from += cnt;
            }
        }
#undef PUSH_DATA

        /* index page: the template's, with the page numbers of this file */
        blank_no = index_no + 1 + (uint32_t)ndata;
        first_data = ndata == 0 ? 0 : index_no + 1;
        memset(&idx, 0, sizeof idx);
        idx.raw = malloc(K_PAGE);
        if (idx.raw == NULL) {
            goto data_oom;
        }
        memcpy(idx.raw, tpl.pages[tt->first_page].raw, K_PAGE);
        idx.zero0 = tpl.pages[tt->first_page].zero0;
        idx.type = tpl.pages[tt->first_page].type;
        idx.index = index_no;
        idx.next = ndata == 0 ? blank_no : first_data;
        tc_set_le32(idx.raw + 0x04, index_no);
        tc_set_le32(idx.raw + 0x0c, idx.next);
        tc_set_le32(idx.raw + 0x28, index_no);
        tc_set_le32(idx.raw + 0x2c, ndata == 0 ? K_EMPTY_ENTRY : first_data);
        if (page_append(&f, &pcap, &idx) != 0) {
            free(idx.raw);
            goto data_oom;
        }
        for (d = 0; d < ndata; d++) {
            data[d].index = index_no + 1 + (uint32_t)d;
            data[d].next = (d + 1 < ndata) ? data[d].index + 1 : blank_no;
            if (!is_static) {
                data[d].u10 = ++counter;
            }
            if (page_append(&f, &pcap, &data[d]) != 0) {
                goto data_oom;
            }
            owned = d + 1;
            built.data_pages++;
        }
        free(data);
        data = NULL;
        {
            struct tc_pdb_page blank;

            memset(&blank, 0, sizeof blank);
            blank.raw = calloc(1, K_PAGE);
            if (blank.raw == NULL || page_append(&f, &pcap, &blank) != 0) {
                free(blank.raw);
                goto oom;
            }
        }
        f.tables[i].type = type;
        f.tables[i].empty_candidate = blank_no;
        f.tables[i].first_page = index_no;
        f.tables[i].last_page = ndata == 0 ? index_no : index_no + (uint32_t)ndata;
        continue;
data_oom:
        for (d = owned; d < ndata; d++) {
            page_free(&data[d]);
        }
        free(data);
        goto oom;
    }
    f.next_unused_page = (uint32_t)f.npages;
    f.sequence = counter + 1;

    built.bytes = tc_pdb_serialise(&f, &built.len, err, errsz);
    if (built.bytes == NULL) {
        goto out;
    }
    built.analyze_paths = analyze;
    analyze = NULL;
    built.artwork_paths = calloc(artwork.n ? artwork.n : 1, sizeof *built.artwork_paths);
    if (built.artwork_paths == NULL) {
        goto oom;
    }
    for (i = 0; i < artwork.n; i++) {
        built.artwork_paths[i] = artwork.aux[i];
        artwork.aux[i] = NULL;
    }
    built.nartwork = artwork.n;
    *out = built;
    memset(&built, 0, sizeof built);
    rc = 0;
    goto out;
oom:
    seterr(err, errsz, "out of memory");
out:
    tc_built_pdb_free(&built);
    if (analyze != NULL) {
        for (i = 0; i < plan->ntracks; i++) {
            free(analyze[i]);
        }
        free(analyze);
    }
    free(genre_id);
    free(label_id);
    free(artist_id);
    free(album_id);
    free(artwork_id);
    idlist_free(&genre);
    idlist_free(&label);
    idlist_free(&artist);
    idlist_free(&album);
    idlist_free(&artwork);
    for (i = 0; i < NTABLES; i++) {
        rowlist_free(&rows[i]);
    }
    if (have_ttpl) {
        track_template_free(&ttpl);
    }
    if (have_tpl) {
        tc_pdb_free(&tpl);
    }
    tc_pdb_free(&f);
    return rc;
}

//SNIP_rbpdb
