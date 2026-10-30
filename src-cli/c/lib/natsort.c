#include <string.h>

#include "natsort.h"

//SNIP_natsort

static int is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static unsigned char lower(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c;
}

int tc_natural_compare(const char *a, const char *b)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;

    while (*p && *q) {
        if (is_digit(*p) && is_digit(*q)) {
            const unsigned char *pe, *qe;
            size_t pl, ql;
            int c;

            while (*p == '0' && is_digit(p[1])) {
                p++;
            }
            while (*q == '0' && is_digit(q[1])) {
                q++;
            }
            for (pe = p; is_digit(*pe); pe++) {
            }
            for (qe = q; is_digit(*qe); qe++) {
            }
            pl = (size_t)(pe - p);
            ql = (size_t)(qe - q);
            if (pl != ql) {
                return pl < ql ? -1 : 1;
            }
            c = memcmp(p, q, pl);
            if (c != 0) {
                return c < 0 ? -1 : 1;
            }
            p = pe;
            q = qe;
            continue;
        }
        if (lower(*p) != lower(*q)) {
            return lower(*p) < lower(*q) ? -1 : 1;
        }
        p++;
        q++;
    }
    if (*p) {
        return 1;
    }
    if (*q) {
        return -1;
    }
    {
        int c = strcmp(a, b);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
}

//SNIP_natsort
