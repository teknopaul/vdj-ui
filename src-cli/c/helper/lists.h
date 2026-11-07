#ifndef TC_HELPER_LISTS_H
#define TC_HELPER_LISTS_H

#include <stdio.h>
#include <stddef.h>

/* The lists of track directories the export and add-to-drive commands read on stdin: NAME TAB TRACKDIR per line; entries of one NAME are one
 * playlist in the order of the lines (an empty TRACKDIR makes an empty list); the lists are in the order of their first line. */
struct lists {
    char **names;
    char ***dirs;
    size_t *counts;
    size_t n;
};

int lists_add_entry(struct lists *l, const char *name, const char *dir);
/* 0, or -1 with errno set (EINVAL: a line without a TAB) */
int lists_read(FILE *in, struct lists *l);
void lists_free(struct lists *l);

#endif /* TC_HELPER_LISTS_H */
