#ifndef TC_BUF_H
#define TC_BUF_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * A growable byte buffer for building binary files. An allocation failure sets `err` and later appends do nothing,
 * so a builder checks `err` once at the end instead of after every append.
 */
struct tc_buf {
    uint8_t *d;
    size_t n, cap;
    int err;
};

static inline void tc_buf_free(struct tc_buf *b)
{
    free(b->d);
    b->d = NULL;
    b->n = b->cap = 0;
}

static inline void tc_buf_put(struct tc_buf *b, const void *p, size_t len)
{
    if (b->err || len == 0) {
        return;
    }
    if (b->n + len > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        uint8_t *nd;

        while (cap < b->n + len) {
            cap *= 2;
        }
        nd = realloc(b->d, cap);
        if (nd == NULL) {
            b->err = 1;
            return;
        }
        b->d = nd;
        b->cap = cap;
    }
    memcpy(b->d + b->n, p, len);
    b->n += len;
}

static inline void tc_buf_zeros(struct tc_buf *b, size_t len)
{
    uint8_t z[64] = { 0 };

    while (len > 0) {
        size_t c = len < sizeof z ? len : sizeof z;

        tc_buf_put(b, z, c);
        len -= c;
    }
}

static inline void tc_buf_str(struct tc_buf *b, const char *s)
{
    tc_buf_put(b, s, strlen(s));
}

static inline void tc_buf_u8(struct tc_buf *b, unsigned v)
{
    uint8_t c = (uint8_t)v;

    tc_buf_put(b, &c, 1);
}

static inline void tc_buf_le16(struct tc_buf *b, unsigned v)
{
    uint8_t c[2] = { (uint8_t)v, (uint8_t)(v >> 8) };

    tc_buf_put(b, c, 2);
}

static inline void tc_buf_le32(struct tc_buf *b, uint32_t v)
{
    uint8_t c[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };

    tc_buf_put(b, c, 4);
}

static inline void tc_buf_be16(struct tc_buf *b, unsigned v)
{
    uint8_t c[2] = { (uint8_t)(v >> 8), (uint8_t)v };

    tc_buf_put(b, c, 2);
}

static inline void tc_buf_be32(struct tc_buf *b, uint32_t v)
{
    uint8_t c[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };

    tc_buf_put(b, c, 4);
}

/* in place, inside what was already written */
static inline void tc_set_le16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void tc_set_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void tc_set_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static inline unsigned tc_get_le16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static inline uint32_t tc_get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline unsigned tc_get_be16(const uint8_t *p)
{
    return (unsigned)p[0] << 8 | (unsigned)p[1];
}

static inline uint32_t tc_get_be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

#endif /* TC_BUF_H */
