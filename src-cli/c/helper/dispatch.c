#include <string.h>

#include "../lib/status.h"
#include "dispatch.h"

//SNIP_dispatch

const struct helper_cmd *helper_find(const struct helper_cmd *table, const char *name)
{
    const struct helper_cmd *c;

    if (name == NULL) {
        return NULL;
    }
    for (c = table; c->name != NULL; c++) {
        if (strcmp(c->name, name) == 0) {
            return c;
        }
    }
    return NULL;
}

static void helper_usage(const struct helper_cmd *table, FILE *f)
{
    const struct helper_cmd *c;

    fprintf(f, "Usage: trakcrate-helper <subcommand> [args...]\n\n");
    fprintf(f, "An implementation detail of the trakcrate commands; use 'trakcrate <command>' instead.\n\n");
    fprintf(f, "Subcommands:\n");
    for (c = table; c->name != NULL; c++) {
        fprintf(f, "  %-12s %s\n", c->name, c->summary);
    }
}

int helper_run(const struct helper_cmd *table, FILE *out, FILE *err, int argc, char **argv)
{
    const struct helper_cmd *c;
    const char *name;

    if (argc < 2) {
        helper_usage(table, err);
        return TC_USAGE;
    }
    name = argv[1];
    if (strcmp(name, "--help") == 0 || strcmp(name, "-h") == 0) {
        helper_usage(table, out);
        return TC_OK;
    }
    if (strcmp(name, "--version") == 0 || strcmp(name, "-V") == 0) {
        name = "version";
    }
    c = helper_find(table, name);
    if (c == NULL) {
        fprintf(err, "trakcrate-helper: '%s' is not a subcommand\n", argv[1]);
        return TC_USAGE;
    }
    return c->fn(argc - 1, argv + 1);
}

//SNIP_dispatch
