#ifndef TC_STATUS_H
#define TC_STATUS_H

/*
 * Exit statuses of every trakcrate program (doc/trakcrate/cli-interface.md).
 * The bash commands use the same numbers.
 */
#define TC_OK       0   /* success */
#define TC_FAIL     1   /* runtime failure */
#define TC_USAGE    2   /* usage error: bad option, unknown subcommand, missing argument */
#define TC_PARTIAL  3   /* some items failed, the rest were done */
#define TC_BUSY     4   /* the library lock is held by a live process, nothing was changed */

/* version of the machine interface (TRAKCRATE_API), bumped on an incompatible change */
#define TC_API_VERSION 1

#endif /* TC_STATUS_H */
