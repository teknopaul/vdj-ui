#ifndef TC_KV_H
#define TC_KV_H

#include <stdio.h>
#include <stddef.h>

/*
 * Key/value files: meta.txt, source.txt, beatgrid.txt (TRAKCRATE-FORMAT.md section 3).
 *
 * "key: value" lines; the key is everything before the first ':' (trimmed), the value the rest (trimmed),
 * a backslash is written \\ and a newline \n inside a value. A line whose first non-blank character is '#',
 * blank lines and lines without a ':' are kept verbatim. A repeated key: the last one wins. Order, comments
 * and unknown keys survive a rewrite; a line that was not changed is written back byte for byte.
 */
typedef struct tc_kvdoc tc_kvdoc;

tc_kvdoc *tc_kv_parse(const char *text, size_t len);          /* NULL only when out of memory */
/* Missing file: an empty document (*existed = 0). 0 on success, -1 with errno set. existed may be NULL. */
int tc_kv_load(const char *path, tc_kvdoc **doc, int *existed);
char *tc_kv_serialise(const tc_kvdoc *doc, size_t *len);      /* malloc'd, NUL terminated */
int tc_kv_save(const tc_kvdoc *doc, const char *path);        /* atomic: tc_write_atomic */
void tc_kv_free(tc_kvdoc *doc);

/* the (unescaped) value of the last line with this key; NULL when there is none. Valid until the next change. */
const char *tc_kv_get(const tc_kvdoc *doc, const char *key);
long long tc_kv_get_int(const tc_kvdoc *doc, const char *key, long long def);
double tc_kv_get_double(const tc_kvdoc *doc, const char *key, double def);
/* replaces the last line of the key in place, or appends one. 0, or -1 when out of memory */
int tc_kv_set(tc_kvdoc *doc, const char *key, const char *value);
int tc_kv_set_int(tc_kvdoc *doc, const char *key, long long value);
void tc_kv_remove(tc_kvdoc *doc, const char *key);            /* every line of the key */
int tc_kv_add_comment(tc_kvdoc *doc, const char *text);       /* appends "# text"; 0, or -1 when out of memory */
size_t tc_kv_count(const tc_kvdoc *doc);                      /* lines that hold a key */

/* writes "key: value\n" with the value escaped */
void tc_kv_print(FILE *f, const char *key, const char *value);

#endif /* TC_KV_H */
