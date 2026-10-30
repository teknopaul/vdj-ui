/*
 * analyse TRACKDIR [options]: decode the audio of one track once and write beatgrid.txt, waveform.bin and the loudness
 * values of meta.txt (doc/trakcrate/cli.md). Groups whose origin in source.txt is rekordbox or edited are left alone
 * unless --force. Output is deterministic: the same audio gives the same bytes.
 *
 *   --only LIST        beatgrid, waveform or beatgrid,waveform (default both)
 *   --force            also redo protected groups (their origin becomes generated)
 *   --bpm-min N, --bpm-max N   tempo range (default 70..180)
 *   --prior-bpm N      tempo hint (default: bpm_tag of meta.txt); --no-prior ignores it
 *   --print            analyse the beat grid only and write nothing
 *
 * stdout, one TSV line: bpm  first_beat_us  confidence  beatgrid_written  waveform_written  skipped  peak_dbfs  rms_dbfs
 * (skipped: "beatgrid:rekordbox,waveform:edited" or "-"). Exit 1 with a message on stderr on failure.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../lib/beatgrid.h"
#include "../lib/beats.h"
#include "../lib/decode.h"
#include "../lib/fsops.h"
#include "../lib/fsutil.h"
#include "../lib/kv.h"
#include "../lib/status.h"
#include "../lib/wave.h"

static int fail(const char *dir, const char *what, const char *detail)
{
    fprintf(stderr, "trakcrate-helper analyse: %s: %s%s%s\n", dir, what, detail ? ": " : "", detail ? detail : "");
    return TC_FAIL;
}

static int protected_origin(const char *o)
{
    return o != NULL && (strcmp(o, "rekordbox") == 0 || strcmp(o, "edited") == 0);
}

int cmd_analyse(int argc, char **argv)
{
    const char *dir = NULL;
    int do_grid = 1, do_wave = 1, force = 0, print_only = 0, no_prior = 0, i, rc = TC_FAIL;
    double min_bpm = 70, max_bpm = 180, prior = -1;
    char path[PATH_MAX], audio[PATH_MAX], err[256], skipped[128] = "", num[64];
    tc_kvdoc *meta = NULL, *source = NULL, *grid_doc = NULL;
    int grid_existed = 0, wave_ok = 0;
    struct tc_pcm pcm = { NULL, 0, TC_SAMPLE_RATE };
    struct tc_beat_analysis ba;
    struct tc_waveform wf;
    double peak_db = -120, rms_db = -120;
    const char *o;

    memset(&wf, 0, sizeof wf);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            const char *l = argv[++i];
            do_grid = strstr(l, "beatgrid") != NULL;
            do_wave = strstr(l, "waveform") != NULL;
            if (!do_grid && !do_wave) {
                fprintf(stderr, "trakcrate-helper analyse: --only wants beatgrid and/or waveform\n");
                return TC_USAGE;
            }
        } else if (strcmp(argv[i], "--force") == 0) {
            force = 1;
        } else if (strcmp(argv[i], "--print") == 0) {
            print_only = 1;
        } else if (strcmp(argv[i], "--no-prior") == 0) {
            no_prior = 1;
        } else if (strcmp(argv[i], "--bpm-min") == 0 && i + 1 < argc) {
            min_bpm = atof(argv[++i]);
        } else if (strcmp(argv[i], "--bpm-max") == 0 && i + 1 < argc) {
            max_bpm = atof(argv[++i]);
        } else if (strcmp(argv[i], "--prior-bpm") == 0 && i + 1 < argc) {
            prior = atof(argv[++i]);
        } else if (argv[i][0] == '-' || dir != NULL) {
            fprintf(stderr, "usage: trakcrate-helper analyse TRACKDIR [--only LIST] [--force] [--bpm-min N] [--bpm-max N] [--prior-bpm N|--no-prior] [--print]\n");
            return TC_USAGE;
        } else {
            dir = argv[i];
        }
    }
    if (dir == NULL || min_bpm <= 0 || max_bpm <= min_bpm) {
        fprintf(stderr, "usage: trakcrate-helper analyse TRACKDIR [options]\n");
        return TC_USAGE;
    }
    if (print_only) {
        do_wave = 0;
    }
    if (!tc_find_audio(dir, audio, sizeof audio)) {
        return fail(dir, "no audio file", NULL);
    }
    snprintf(path, sizeof path, "%s/meta.txt", dir);
    if (tc_kv_load(path, &meta, NULL) != 0) {
        return fail(path, "cannot read", strerror(errno));
    }
    snprintf(path, sizeof path, "%s/source.txt", dir);
    if (tc_kv_load(path, &source, NULL) != 0) {
        fail(path, "cannot read", strerror(errno));
        goto done;
    }
    snprintf(path, sizeof path, "%s/beatgrid.txt", dir);
    if (tc_kv_load(path, &grid_doc, &grid_existed) != 0) {
        fail(path, "cannot read", strerror(errno));
        goto done;
    }

    if (do_grid && !force && !print_only && protected_origin(o = tc_kv_get(source, "origin.beatgrid"))) {
        snprintf(skipped + strlen(skipped), sizeof skipped - strlen(skipped), "%sbeatgrid:%s", skipped[0] ? "," : "", o);
        do_grid = 0;
    }
    if (do_wave && !force && protected_origin(o = tc_kv_get(source, "origin.waveform"))) {
        snprintf(skipped + strlen(skipped), sizeof skipped - strlen(skipped), "%swaveform:%s", skipped[0] ? "," : "", o);
        do_wave = 0;
    }
    memset(&ba, 0, sizeof ba);
    if (!do_grid && !do_wave) {
        printf("0\t0\t0\t0\t0\t%s\t0\t0\n", skipped[0] ? skipped : "-");
        rc = TC_OK;
        goto done;
    }

    if (tc_decode(audio, &pcm, err, sizeof err) != 0) {
        fail(audio, "cannot decode", err);
        goto done;
    }
    if (do_grid) {
        double p = no_prior ? 0 : (prior >= 0 ? prior : tc_kv_get_double(meta, "bpm_tag", 0));
        if (tc_analyse_beats(&pcm, p, min_bpm, max_bpm, &ba) != 0) {
            fail(dir, "out of memory", NULL);
            goto done;
        }
    }
    if (do_wave) {
        if (tc_analyse_waveform(&pcm, &wf) != 0) {
            fail(dir, "out of memory", NULL);
            goto done;
        }
        wave_ok = 1;
    }
    tc_measure_loudness(&pcm, &peak_db, &rms_db);

    if (print_only) {
        printf("%.2f\t%lld\t%.3f\t0\t0\t%s\t%.1f\t%.1f\n", ba.grid.bpm, ba.grid.first_beat_us, ba.confidence, skipped[0] ? skipped : "-", peak_db, rms_db);
        rc = TC_OK;
        goto done;
    }

    if (do_grid) {
        struct tc_beatgrid g = ba.grid;
        g.confidence = ba.confidence;
        snprintf(g.analyser, sizeof g.analyser, "trakcrate-beats 1");
        if (!grid_existed) {
            tc_kv_add_comment(grid_doc, "trakcrate beatgrid");
        }
        tc_beatgrid_to(grid_doc, &g);
        tc_kv_set(source, "origin.beatgrid", "generated");
    }
    if (do_wave) {
        tc_kv_set(source, "origin.waveform", "generated");
    }
    snprintf(num, sizeof num, "%.1f", peak_db);
    tc_kv_set(meta, "peak_dbfs", num);
    snprintf(num, sizeof num, "%.1f", rms_db);
    tc_kv_set(meta, "rms_dbfs", num);

    if (wave_ok) {
        size_t len;
        uint8_t *bytes = tc_waveform_serialise(&wf, &len);
        snprintf(path, sizeof path, "%s/waveform.bin", dir);
        if (bytes == NULL || tc_write_atomic(path, bytes, len) != 0) {
            free(bytes);
            fail(path, "cannot write", strerror(errno));
            goto done;
        }
        free(bytes);
    }
    if (do_grid) {
        snprintf(path, sizeof path, "%s/beatgrid.txt", dir);
        if (tc_kv_save(grid_doc, path) != 0) {
            fail(path, "cannot write", strerror(errno));
            goto done;
        }
    }
    snprintf(path, sizeof path, "%s/meta.txt", dir);
    if (tc_kv_save(meta, path) != 0) {
        fail(path, "cannot write", strerror(errno));
        goto done;
    }
    snprintf(path, sizeof path, "%s/source.txt", dir);
    if (tc_kv_save(source, path) != 0) {
        fail(path, "cannot write", strerror(errno));
        goto done;
    }
    printf("%.2f\t%lld\t%.3f\t%d\t%d\t%s\t%.1f\t%.1f\n", ba.grid.bpm, ba.grid.first_beat_us, ba.confidence, do_grid, do_wave, skipped[0] ? skipped : "-", peak_db, rms_db);
    rc = TC_OK;

done:
    tc_pcm_free(&pcm);
    tc_waveform_free(&wf);
    tc_kv_free(meta);
    tc_kv_free(source);
    tc_kv_free(grid_doc);
    return rc;
}
