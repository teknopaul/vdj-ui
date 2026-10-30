/*
 * verify ROOT: check a rekordbox format drive (a real rekordbox export too): export.pdb, the analysis files of every track, audio
 * and artwork files, DEVSETTING.DAT and MYSETTING.DAT, SHA256SUMS when present. libcdj's readers do the reading.
 *
 * stdout, one TSV line per finding: where  severity  code  message. Exit 0 when there is no error, 1 when there is at least one.
 * The last line on stderr says how many tracks and playlists were checked.
 */
#include <stdio.h>
#include <string.h>

#include "../lib/rbdb.h"
#include "../lib/status.h"

static void on_finding(void *ud, const char *sev, const char *where, const char *code, const char *msg)
{
    (void)ud;
    printf("%s\t%s\t%s\t%s\n", where, sev, code, msg);
}

int cmd_verify(int argc, char **argv)
{
    struct tc_verify_stats st;
    int rc;

    if (argc != 2 || argv[1][0] == '-') {
        fprintf(stderr, "usage: trakcrate-helper verify ROOT\n");
        return TC_USAGE;
    }
    rc = tc_verify_drive(argv[1], on_finding, NULL, &st);
    if (rc < 0) {
        return TC_FAIL;
    }
    fprintf(stderr, "checked %zu tracks, %zu playlists, %zu files: %zu errors, %zu warnings\n", st.tracks, st.playlists, st.files_checked, st.errors, st.warnings);
    return rc == 0 ? TC_OK : TC_FAIL;
}

/* settings-check FILE...: exit 0 when every file is a well formed DEVSETTING.DAT / MYSETTING.DAT (CRC included) */
#include "../lib/rbsettings.h"
#include "../lib/fsutil.h"
#include <stdlib.h>

int cmd_settings_check(int argc, char **argv)
{
    int i, bad = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: trakcrate-helper settings-check FILE...\n");
        return TC_USAGE;
    }
    for (i = 1; i < argc; i++) {
        uint8_t *b;
        size_t n;
        char err[300];

        if (tc_settings_load(argv[i], &b, &n, err, sizeof err) != 0) {
            fprintf(stderr, "%s\n", err);
            bad = 1;
        } else {
            free(b);
        }
    }
    return bad ? TC_FAIL : TC_OK;
}
