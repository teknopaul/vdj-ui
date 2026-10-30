#ifndef TC_CUEMARKS_H
#define TC_CUEMARKS_H

#include <stddef.h>

/*
 * cuemarks.txt (TRAKCRATE-FORMAT.md section 6, doc/trakcrate/cuemarks.md): TAB separated columns
 *   beat  ms  label  hot  colour  extra...
 * A line without a TAB is split on blanks (hand written files); '-' is a missing value; a line that is not a
 * mark is ignored; extra columns and the leading '#' lines are kept. Written sorted by position.
 */
struct tc_cuemark {
    double beat;            /* from the first beat of the grid; NAN when the track has no grid */
    double ms;
    char *label;            /* never NULL, may be "" */
    char hot;               /* 0, or 'A'..'C' */
    char *colour;           /* "" when none */
    char *extra;            /* the remaining columns joined by TAB, "" when none */
};

struct tc_cuemarks {
    char *header;           /* the leading '#' lines, each ending in a newline; "" when none */
    struct tc_cuemark *marks;
    size_t n;
};

/* 0, or -1 when out of memory */
int tc_cuemarks_parse(const char *text, struct tc_cuemarks *out);
/* sorted by ms (stable); malloc'd, NUL terminated */
char *tc_cuemarks_serialise(const struct tc_cuemarks *cm, size_t *len);
void tc_cuemarks_free(struct tc_cuemarks *cm);

#endif /* TC_CUEMARKS_H */
