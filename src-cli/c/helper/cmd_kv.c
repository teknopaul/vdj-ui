/*
 * kv get FILE KEY | kv set FILE KEY VALUE | kv unset FILE KEY
 * The key/value rules of TRAKCRATE-FORMAT.md section 3 (lib/kv.h) for scripts: get prints the unescaped value
 * (exit 1 when the key or file is missing), set and unset rewrite the file atomically, keeping comments and
 * unknown keys.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../lib/kv.h"
#include "../lib/status.h"

static int usage(void)
{
    fprintf(stderr, "usage: trakcrate-helper kv get FILE KEY | set FILE KEY VALUE | unset FILE KEY\n");
    return TC_USAGE;
}

int cmd_kv(int argc, char **argv)
{
    tc_kvdoc *doc;
    int existed, rc = TC_OK;

    if (argc < 4) {
        return usage();
    }
    if (tc_kv_load(argv[2], &doc, &existed) != 0) {
        fprintf(stderr, "trakcrate-helper kv: %s: %s\n", argv[2], strerror(errno));
        return TC_FAIL;
    }
    if (strcmp(argv[1], "get") == 0 && argc == 4) {
        const char *v = tc_kv_get(doc, argv[3]);
        if (v == NULL) {
            rc = TC_FAIL;
        } else {
            printf("%s\n", v);
        }
    } else if (strcmp(argv[1], "set") == 0 && argc == 5) {
        if (tc_kv_set(doc, argv[3], argv[4]) != 0 || tc_kv_save(doc, argv[2]) != 0) {
            fprintf(stderr, "trakcrate-helper kv: cannot write %s: %s\n", argv[2], strerror(errno));
            rc = TC_FAIL;
        }
    } else if (strcmp(argv[1], "unset") == 0 && argc == 4) {
        if (existed) {
            tc_kv_remove(doc, argv[3]);
            if (tc_kv_save(doc, argv[2]) != 0) {
                fprintf(stderr, "trakcrate-helper kv: cannot write %s: %s\n", argv[2], strerror(errno));
                rc = TC_FAIL;
            }
        }
    } else {
        rc = usage();
    }
    tc_kv_free(doc);
    return rc;
}
