/*
 * export --to DEST [options] < LISTS: build a rekordbox format drive from lists of track directories (doc/trakcrate/cli.md,
 * rekordbox-db-output.md). LISTS on stdin, one entry per line: NAME TAB TRACKDIR (an empty TRACKDIR makes an empty list); entries of
 * one NAME are one playlist, in the order of the lines; the lists are in the order of their first line.
 *
 *   --to DEST            the empty (or not yet existing) destination folder
 *   --template FILE      the template export.pdb (default $TRAKCRATE_DATA/template-export.pdb)
 *   --tmpdir DIR         where the staging folder goes (default /dev/shm, $XDG_RUNTIME_DIR, the system temp folder)
 *   --settings-from DIR  DEVSETTING.DAT and MYSETTING.DAT of a real drive (DIR/PIONEER/... or DIR/...) instead of the built in ones
 *   --no-verify          do not read the destination back
 *   --no-artwork  --no-settings  --no-pvbr  --no-pwv4  --no-pwv5  --no-manifest   leave a piece out (compatibility testing)
 *   --raise-limits       apply the limits of newer decks (20000 tracks, 16000 artists and albums) instead of 10000 of each
 *   --allow-unanalysed   tracks without waveform get a flat one and no beat grid instead of refusing the export
 *   --no-cuemarks  --cuemark-width N  --cuemark-green  --cuemark-hot    how cuemarks are drawn into the exported waveforms
 *   --link-audio         audio files are symbolic links to the master copy, no SHA256SUMS, no read back (the input of `trakcrate sync`)
 *   --dry-run            check everything, write nothing
 *   --progress-fd N      JSON phase events on file descriptor N
 *
 * stdout, one TSV line: dest playlists tracks files bytes verified(0|1) audio_bytes meta_bytes. Warnings and errors on stderr.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/fsutil.h"
#include "../lib/rbexport.h"
#include "../lib/rbsettings.h"
#include "../lib/status.h"

struct ctx {
    FILE *prog;
    char last_phase[16];
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
    fprintf(stderr, "trakcrate-helper export: warning: %s\n", msg);
}

struct lists {
    char **names;
    char ***dirs;
    size_t *counts;
    size_t n;
};

static int add_entry(struct lists *l, const char *name, const char *dir)
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

static int read_lists(FILE *in, struct lists *l)
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
        if (add_entry(l, line, tab + 1) != 0) {
            free(line);
            return -1;
        }
    }
    free(line);
    return 0;
}

static int load_setting(const char *dir, const char *name, uint8_t **out, size_t *len)
{
    char path[PATH_MAX], err[300];

    snprintf(path, sizeof path, "%s/PIONEER/%s", dir, name);
    if (tc_settings_load(path, out, len, err, sizeof err) == 0) {
        return 0;
    }
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (tc_settings_load(path, out, len, err, sizeof err) == 0) {
        return 0;
    }
    fprintf(stderr, "trakcrate-helper export: %s\n", err);
    return -1;
}

int cmd_export(int argc, char **argv)
{
    struct tc_export_opts o;
    struct tc_export_result res;
    struct lists l = { NULL, NULL, NULL, 0 };
    struct tc_export_list *el = NULL;
    struct ctx c = { NULL, "" };
    const char *template_path = NULL, *settings_from = NULL, *data;
    char tpl_default[PATH_MAX];
    char *tpl = NULL;
    size_t tpl_len = 0, i, k;
    uint8_t *dev = NULL, *my = NULL;
    size_t dev_len = 0, my_len = 0;
    int rc = TC_FAIL, a;

    memset(&o, 0, sizeof o);
    o.verify = 1;
    o.include_settings = o.include_artwork = o.include_pvbr = o.include_pwv4 = o.include_pwv5 = o.write_manifest = 1;
    o.cue.enabled = 1;
    o.cue.width_columns = 2;
    o.min_free_bytes = 1u << 20;
    o.progress = on_progress;
    o.warn = on_warn;
    o.ud = &c;
    for (a = 1; a < argc; a++) {
        const char *s = argv[a];

        if (strcmp(s, "--to") == 0 && a + 1 < argc) {
            o.dest = argv[++a];
        } else if (strcmp(s, "--template") == 0 && a + 1 < argc) {
            template_path = argv[++a];
        } else if (strcmp(s, "--tmpdir") == 0 && a + 1 < argc) {
            o.staging_base = argv[++a];
        } else if (strcmp(s, "--settings-from") == 0 && a + 1 < argc) {
            settings_from = argv[++a];
        } else if (strcmp(s, "--no-verify") == 0) {
            o.verify = 0;
        } else if (strcmp(s, "--no-artwork") == 0) {
            o.include_artwork = 0;
        } else if (strcmp(s, "--no-settings") == 0) {
            o.include_settings = 0;
        } else if (strcmp(s, "--no-pvbr") == 0) {
            o.include_pvbr = 0;
        } else if (strcmp(s, "--no-pwv4") == 0) {
            o.include_pwv4 = 0;
        } else if (strcmp(s, "--no-pwv5") == 0) {
            o.include_pwv5 = 0;
        } else if (strcmp(s, "--no-manifest") == 0) {
            o.write_manifest = 0;
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
        } else if (strcmp(s, "--link-audio") == 0) {
            o.link_audio = 1;
        } else if (strcmp(s, "--dry-run") == 0) {
            o.dry_run = 1;
        } else if (strcmp(s, "--progress-fd") == 0 && a + 1 < argc) {
            int fd = atoi(argv[++a]);

            c.prog = fdopen(fd, "w");
        } else {
            fprintf(stderr, "usage: trakcrate-helper export --to DEST [--template FILE] [--tmpdir DIR] [--settings-from DIR] [options] < LISTS\n");
            return TC_USAGE;
        }
    }
    if (o.dest == NULL) {
        fprintf(stderr, "trakcrate-helper export: --to DEST is needed\n");
        return TC_USAGE;
    }
    if (template_path == NULL) {
        data = getenv("TRAKCRATE_DATA");
        snprintf(tpl_default, sizeof tpl_default, "%s/template-export.pdb", data != NULL && data[0] ? data : "/usr/share/trakcrate");
        template_path = tpl_default;
    }
    if (tc_read_file(template_path, &tpl, &tpl_len) != 0) {
        fprintf(stderr, "trakcrate-helper export: cannot read the template database %s: %s\n", template_path, strerror(errno));
        return TC_FAIL;
    }
    o.tpl = (const uint8_t *)tpl;
    o.tpl_len = tpl_len;
    if (settings_from != NULL) {
        if (load_setting(settings_from, "DEVSETTING.DAT", &dev, &dev_len) != 0 || load_setting(settings_from, "MYSETTING.DAT", &my, &my_len) != 0) {
            goto done;
        }
        o.devsetting = dev;
        o.dev_len = dev_len;
        o.mysetting = my;
        o.my_len = my_len;
    }
    if (read_lists(stdin, &l) != 0) {
        fprintf(stderr, "trakcrate-helper export: bad list input (want NAME TAB TRACKDIR per line)\n");
        rc = TC_USAGE;
        goto done;
    }
    el = calloc(l.n ? l.n : 1, sizeof *el);
    if (el == NULL) {
        goto done;
    }
    for (i = 0; i < l.n; i++) {
        el[i].name = l.names[i];
        el[i].dirs = (const char *const *)l.dirs[i];
        el[i].ndirs = l.counts[i];
    }
    if (tc_export(el, l.n, &o, &res) != 0) {
        fprintf(stderr, "trakcrate-helper export: %s\n", res.error);
        goto done;
    }
    printf("%s\t%d\t%d\t%d\t%llu\t%d\t%llu\t%llu\n", o.dest, res.playlists, res.tracks, res.files, (unsigned long long)res.bytes_written,
           res.verified, (unsigned long long)res.audio_bytes, (unsigned long long)res.meta_bytes);
    rc = TC_OK;
done:
    for (i = 0; i < l.n; i++) {
        for (k = 0; k < l.counts[i]; k++) {
            free(l.dirs[i][k]);
        }
        free(l.dirs[i]);
        free(l.names[i]);
    }
    free(l.dirs);
    free(l.names);
    free(l.counts);
    free(el);
    free(tpl);
    free(dev);
    free(my);
    if (c.prog != NULL) {
        fflush(c.prog);
    }
    return rc;
}
