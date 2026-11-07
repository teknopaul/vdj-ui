/*
 * add-to-drive --drive DIR [options] < LIST: add one playlist of track directories to the rekordbox pendrive DIR, which already has a PIONEER/rekordbox/
 * export.pdb (doc/trakcrate/cli.md, pdb-append-notes.md). LIST on stdin as for export: NAME TAB TRACKDIR per line, one playlist.
 *
 *   --drive DIR          the stick's root
 *   --template FILE      the template export.pdb (default $TRAKCRATE_DATA/template-export.pdb)
 *   --tmpdir DIR         where the staging folder goes (default /dev/shm, $XDG_RUNTIME_DIR, the system temp folder)
 *   --backup-dir DIR     where the copy of the old export.pdb goes (default /var/tmp); the copy is never deleted
 *   --no-artwork  --no-pvbr  --no-pwv4  --no-pwv5     leave a piece out (compatibility testing)
 *   --raise-limits       apply the limits of newer decks (20000 tracks, 16000 artists and albums) instead of 10000 of each
 *   --allow-unanalysed   tracks without waveform get a flat one and no beat grid instead of refusing
 *   --no-cuemarks  --cuemark-width N  --cuemark-green  --cuemark-hot    how cuemarks are drawn into the waveforms of new tracks
 *   --dry-run            check everything, write nothing
 *   --progress-fd N      JSON phase events on file descriptor N
 *   --stop-at STEP       tests: stop at once at this step of the engine, as if the stick had been pulled
 * SIGINT and SIGTERM cancel the addition until the database is switched; what was added is removed again.
 *
 * stdout, one TSV line: drive playlist new_tracks existing_tracks files bytes switched backup_copy backup_on_drive audio_bytes meta_bytes.
 * Warnings and errors on stderr.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/fsutil.h"
#include "../lib/rbadd.h"
#include "../lib/status.h"
#include "lists.h"

static volatile int g_cancel_flag;

static void on_signal(int sig)
{
    (void)sig;

    g_cancel_flag = 1;
}

struct ctx {
    FILE *prog;
};

static void on_progress(void *ud, const char *phase, int done, int total, const char *current)
{
    struct ctx *c = ud;

    (void)current;
    if (c->prog == NULL) {
        return;
    }
    fprintf(c->prog, "{\"event\":\"phase\",\"name\":\"%s\",\"done\":%d,\"total\":%d}\n", phase, done, total);
    fflush(c->prog);
}

static void on_warn(void *ud, const char *msg)
{
    (void)ud;
    fprintf(stderr, "trakcrate-helper add-to-drive: warning: %s\n", msg);
}

int cmd_add_to_drive(int argc, char **argv)
{
    struct tc_add_opts o;
    struct tc_add_result res;
    struct lists l = { NULL, NULL, NULL, 0 };
    struct tc_export_list el;
    struct ctx c = { NULL };
    const char *template_path = NULL, *data;
    char tpl_default[PATH_MAX];
    char *tpl = NULL;
    size_t tpl_len = 0;
    struct sigaction sa;
    int rc = TC_FAIL, a;

    memset(&o, 0, sizeof o);
    memset(&res, 0, sizeof res);
    o.include_artwork = o.include_pvbr = o.include_pwv4 = o.include_pwv5 = 1;
    o.cue.enabled = 1;
    o.cue.width_columns = 2;
    o.min_free_bytes = 1u << 20;
    o.cancel = &g_cancel_flag;
    o.progress = on_progress;
    o.warn = on_warn;
    o.ud = &c;
    for (a = 1; a < argc; a++) {
        const char *s = argv[a];

        if (strcmp(s, "--drive") == 0 && a + 1 < argc) {
            o.drive = argv[++a];
        } else if (strcmp(s, "--template") == 0 && a + 1 < argc) {
            template_path = argv[++a];
        } else if (strcmp(s, "--tmpdir") == 0 && a + 1 < argc) {
            o.staging_base = argv[++a];
        } else if (strcmp(s, "--backup-dir") == 0 && a + 1 < argc) {
            o.backup_dir = argv[++a];
        } else if (strcmp(s, "--stop-at") == 0 && a + 1 < argc) {
            o.stop_at = argv[++a];
        } else if (strcmp(s, "--no-artwork") == 0) {
            o.include_artwork = 0;
        } else if (strcmp(s, "--no-pvbr") == 0) {
            o.include_pvbr = 0;
        } else if (strcmp(s, "--no-pwv4") == 0) {
            o.include_pwv4 = 0;
        } else if (strcmp(s, "--no-pwv5") == 0) {
            o.include_pwv5 = 0;
        } else if (strcmp(s, "--raise-limits") == 0) {
            o.raise_limits = 1;
        } else if (strcmp(s, "--allow-unanalysed") == 0) {
            o.allow_unanalysed = 1;
        } else if (strcmp(s, "--no-cuemarks") == 0) {
            o.cue.enabled = 0;
        } else if (strcmp(s, "--cuemark-width") == 0 && a + 1 < argc) {
            o.cue.width_columns = atoi(argv[++a]);
        } else if (strcmp(s, "--cuemark-green") == 0) {
            o.cue.green = 1;
        } else if (strcmp(s, "--cuemark-hot") == 0) {
            o.cue.draw_hot = 1;
        } else if (strcmp(s, "--dry-run") == 0) {
            o.dry_run = 1;
        } else if (strcmp(s, "--progress-fd") == 0 && a + 1 < argc) {
            c.prog = fdopen(atoi(argv[++a]), "w");
        } else {
            fprintf(stderr, "usage: trakcrate-helper add-to-drive --drive DIR [--template FILE] [--tmpdir DIR] [--backup-dir DIR] [options] < LIST\n");
            return TC_USAGE;
        }
    }
    if (o.drive == NULL) {
        fprintf(stderr, "trakcrate-helper add-to-drive: --drive DIR is needed\n");
        return TC_USAGE;
    }
    if (template_path == NULL) {
        data = getenv("TRAKCRATE_DATA");
        snprintf(tpl_default, sizeof tpl_default, "%s/template-export.pdb", data != NULL && data[0] ? data : "/usr/share/trakcrate");
        template_path = tpl_default;
    }
    if (tc_read_file(template_path, &tpl, &tpl_len) != 0) {
        fprintf(stderr, "trakcrate-helper add-to-drive: cannot read the template database %s: %s\n", template_path, strerror(errno));
        return TC_FAIL;
    }
    o.tpl = (const uint8_t *)tpl;
    o.tpl_len = tpl_len;
    if (lists_read(stdin, &l) != 0) {
        fprintf(stderr, "trakcrate-helper add-to-drive: bad list input (want NAME TAB TRACKDIR per line)\n");
        rc = TC_USAGE;
        goto done;
    }
    if (l.n != 1) {
        fprintf(stderr, "trakcrate-helper add-to-drive: add exactly one playlist\n");
        rc = TC_USAGE;
        goto done;
    }
    el.name = l.names[0];
    el.dirs = (const char *const *)l.dirs[0];
    el.ndirs = l.counts[0];
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;                           /* no SA_RESTART: a read or write in progress returns and sees the flag */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    if (tc_add_to_drive(&el, &o, &res) != 0) {
        fprintf(stderr, "trakcrate-helper add-to-drive: %s\n", res.error);
        if (res.ncreated > 0) {
            size_t i;

            fprintf(stderr, "trakcrate-helper add-to-drive: %zu files added to the pendrive are not used by its database (nothing refers to them):\n", res.ncreated);
            for (i = 0; i < res.ncreated && i < 20; i++) {
                fprintf(stderr, "  %s\n", res.created[i]);
            }
        }
        if (res.backup_copy[0]) {
            fprintf(stderr, "trakcrate-helper add-to-drive: the old database was copied to %s\n", res.backup_copy);
        }
        rc = res.cancelled ? TC_FAIL : TC_FAIL;
        goto done;
    }
    printf("%s\t%s\t%d\t%d\t%d\t%llu\t%d\t%s\t%s\t%llu\t%llu\n", o.drive, l.names[0], res.new_tracks, res.existing_tracks, res.files_copied,
           (unsigned long long)res.bytes_written, res.switched, res.backup_copy, res.backup_on_drive, (unsigned long long)res.audio_bytes,
           (unsigned long long)res.meta_bytes);
    rc = TC_OK;
done:
    tc_add_result_free(&res);
    lists_free(&l);
    free(tpl);
    if (c.prog != NULL) {
        fflush(c.prog);
    }
    return rc;
}
