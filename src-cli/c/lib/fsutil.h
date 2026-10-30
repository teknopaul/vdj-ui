#ifndef TC_FSUTIL_H
#define TC_FSUTIL_H

#include <stddef.h>
#include <sys/types.h>

/*
 * Files and names of a trakcrate library (TRAKCRATE-FORMAT.md sections 1, 2.1 and 16).
 */

/* 1 when name is one path component: not empty, not "." or "..", no '/', no NUL (always, it is a C string) or newline */
int tc_safe_name(const char *name);

/* Reads a whole file into a malloc'd, NUL terminated buffer (*len without the NUL). 0 on success, -1 with errno set. */
int tc_read_file(const char *path, char **data, size_t *len);

/* Replaces path by a file with this content: written to .<name>.tmp.<pid> in the same directory, then renamed.
 * A reader never sees a half written file. 0 on success, -1 with errno set (the temporary file is removed). */
int tc_write_atomic(const char *path, const void *data, size_t len);

/* 1 when path is strictly below root once symbolic links are resolved ("..", links out of the library: 0).
 * path need not exist; its existing part is resolved. */
int tc_path_within(const char *root, const char *path);

/*
 * The library lock: <library>/.trakcrate-lock holds the owner's pid.
 * tc_lock_acquire: 0 acquired; 1 busy, *holder is the pid of the live owner; -1 error (errno).
 * A lock whose process no longer exists is taken over. tc_lock_release removes the lock only if it is ours.
 */
int tc_lock_acquire(const char *library, pid_t *holder);
void tc_lock_release(const char *library);

#endif /* TC_FSUTIL_H */
