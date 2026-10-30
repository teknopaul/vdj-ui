/*
 * natsort [-z]: reads lines (NUL terminated with -z) from stdin and writes them in the natural order of a
 * trakcrate library (lib/natsort.h). Duplicates are kept.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/natsort.h"
#include "../lib/status.h"

static int by_natural(const void *a, const void *b)
{
    return tc_natural_compare(*(char *const *)a, *(char *const *)b);
}

int cmd_natsort(int argc, char **argv)
{
    int sep = '\n';
    char **lines = NULL;
    size_t n = 0, cap = 0, i;
    char *buf = NULL;
    size_t len = 0, bufcap = 0;
    int c;

    if (argc == 2 && strcmp(argv[1], "-z") == 0) {
        sep = '\0';
    } else if (argc != 1) {
        fprintf(stderr, "usage: trakcrate-helper natsort [-z] < lines\n");
        return TC_USAGE;
    }
    while ((c = getchar()) != EOF) {
        if (len + 1 >= bufcap) {
            bufcap = bufcap ? bufcap * 2 : 256;
            buf = realloc(buf, bufcap);
            if (buf == NULL) {
                return TC_FAIL;
            }
        }
        if (c == sep) {
            buf[len] = '\0';
            if (n == cap) {
                cap = cap ? cap * 2 : 64;
                lines = realloc(lines, cap * sizeof *lines);
                if (lines == NULL) {
                    return TC_FAIL;
                }
            }
            lines[n++] = strdup(buf);
            len = 0;
        } else {
            buf[len++] = (char)c;
        }
    }
    if (len > 0) {
        buf[len] = '\0';
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            lines = realloc(lines, cap * sizeof *lines);
        }
        lines[n++] = strdup(buf);
    }
    if (n > 0) {
        qsort(lines, n, sizeof *lines, by_natural);
    }
    for (i = 0; i < n; i++) {
        fputs(lines[i], stdout);
        fputc(sep, stdout);
        free(lines[i]);
    }
    free(lines);
    free(buf);
    return TC_OK;
}
