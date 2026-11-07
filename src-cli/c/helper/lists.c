#define _GNU_SOURCE
#include "lists.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int lists_add_entry(struct lists *l, const char *name, const char *dir)
{
    size_t i;

    for (i = 0; i < l->n; i++) {
        if (strcmp(l->names[i], name) == 0) {
            break;
        }
    }
    if (i == l->n) {
        char **nn = realloc(l->names, (l->n + 1) * sizeof *nn);
        char ***nd;
        size_t *nc;

        if (nn == NULL) {
            return -1;
        }
        l->names = nn;
        nd = realloc(l->dirs, (l->n + 1) * sizeof *nd);
        if (nd == NULL) {
            return -1;
        }
        l->dirs = nd;
        nc = realloc(l->counts, (l->n + 1) * sizeof *nc);
        if (nc == NULL) {
            return -1;
        }
        l->counts = nc;
        l->names[i] = strdup(name);
        l->dirs[i] = NULL;
        l->counts[i] = 0;
        if (l->names[i] == NULL) {
            return -1;
        }
        l->n++;
    }
    if (dir[0] != '\0') {
        char **nd = realloc(l->dirs[i], (l->counts[i] + 1) * sizeof *nd);

        if (nd == NULL) {
            return -1;
        }
        l->dirs[i] = nd;
        l->dirs[i][l->counts[i]] = strdup(dir);
        if (l->dirs[i][l->counts[i]] == NULL) {
            return -1;
        }
        l->counts[i]++;
    }
    return 0;
}

int lists_read(FILE *in, struct lists *l)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;

    while ((n = getline(&line, &cap, in)) >= 0) {
        char *tab;

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (n == 0) {
            continue;
        }
        tab = strchr(line, '\t');
        if (tab == NULL) {
            free(line);
            errno = EINVAL;
            return -1;
        }
        *tab = '\0';
        if (lists_add_entry(l, line, tab + 1) != 0) {
            free(line);
            return -1;
        }
    }
    free(line);
    return 0;
}


void lists_free(struct lists *l)
{
    size_t i, k;

    for (i = 0; i < l->n; i++) {
        for (k = 0; k < l->counts[i]; k++) {
            free(l->dirs[i][k]);
        }
        free(l->dirs[i]);
        free(l->names[i]);
    }
    free(l->dirs);
    free(l->names);
    free(l->counts);
    memset(l, 0, sizeof *l);
}
