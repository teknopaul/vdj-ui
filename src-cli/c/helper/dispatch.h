#ifndef TC_HELPER_DISPATCH_H
#define TC_HELPER_DISPATCH_H

#include <stdio.h>

/*
 * trakcrate-helper <subcommand> [args...]
 *
 * A subcommand is a function taking argc/argv with argv[0] the subcommand's name; it returns one of
 * the TC_* statuses of lib/status.h. The table is terminated by an entry with a NULL name.
 */
typedef int (*helper_fn)(int argc, char **argv);

struct helper_cmd {
    const char *name;
    helper_fn   fn;
    const char *summary;
};

/* NULL when there is no such subcommand */
const struct helper_cmd *helper_find(const struct helper_cmd *table, const char *name);

/*
 * Runs argv[1] from the table. Handles --help / -h (usage on out, TC_OK), --version (the "version"
 * subcommand), no arguments and unknown subcommands (message on err, TC_USAGE).
 */
int helper_run(const struct helper_cmd *table, FILE *out, FILE *err, int argc, char **argv);

#endif /* TC_HELPER_DISPATCH_H */
