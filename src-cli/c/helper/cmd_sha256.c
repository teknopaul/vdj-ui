/* sha256 FILE...: like sha256sum ("<hash>  <file>"), exit 1 when a file cannot be read */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../lib/sha256.h"
#include "../lib/status.h"

int cmd_sha256(int argc, char **argv)
{
    int i, rc = TC_OK;
    char hex[65];

    if (argc < 2) {
        fprintf(stderr, "usage: trakcrate-helper sha256 FILE...\n");
        return TC_USAGE;
    }
    for (i = 1; i < argc; i++) {
        if (tc_sha256_file(argv[i], hex) != 0) {
            fprintf(stderr, "trakcrate-helper sha256: %s: %s\n", argv[i], strerror(errno));
            rc = TC_FAIL;
            continue;
        }
        printf("%s  %s\n", hex, argv[i]);
    }
    return rc;
}
