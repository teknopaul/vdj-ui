#ifndef TC_RBEXPORT_INTERNAL_H
#define TC_RBEXPORT_INTERNAL_H

/*
 * The pieces of the exporter (rbexport.c) that the code adding a playlist to an existing drive (rbadd.c) shares: gathering the tracks of lists,
 * staging their analysis files and artwork, and copying files with a temporary check of the hash. Not part of the library's interface.
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "beatgrid.h"
#include "cuemarks.h"
#include "kv.h"
#include "rbanlz.h"
#include "rbappend.h"
#include "rbexport.h"
#include "rbpdb.h"
#include "wave.h"

struct strlist {
    char **v;
    size_t n, cap;
};

struct gtrack {
    char *dir, audio[PATH_MAX], canonical[PATH_MAX];
    tc_kvdoc *meta, *source, *grid_doc;
    struct tc_cuemarks cues;
    struct tc_waveform wave;
    int have_wave;
    struct tc_beatgrid grid;
    double duration_ms;
    struct tc_anlz_cue hot[3];
    size_t nhot;
    uint8_t *art;
    size_t art_len;
    char art_key[65];
    uint64_t audio_bytes;
    int on_stick;           /* adding to a drive: 0 new, 1 the drive has the track (row and identical file), 2 identical file but no row */
    /* owned strings of the plan track */
    char *title, *artist, *album, *genre, *comment, *file_name, *file_path, *audio_hash, art_path[96];
    char date[16];
    struct tc_plan_track plan;
};

struct gathered {
    struct gtrack *tracks;
    size_t ntracks, cap;
    struct tc_plan_playlist *lists;
    size_t **list_tracks;
    size_t nlists;
    struct strlist problems;
};

struct outfile {
    char *rel;
    char *src;              /* path on disk, or NULL when mem is used */
    const uint8_t *mem;
    size_t mem_len;
    char hash[65];          /* known before the copy for staged files, learnt during it for audio */
    int hash_known;
    int audio;              /* a track's audio file */
};

/* Adding to a drive that already has a database: what is on it, so that paths, folder spellings and artwork numbers continue what is there. */
struct tcx_seed {
    const struct tc_drive_db *db;
    const char *drive;              /* the stick's root */
};

int tcx_sl_add(struct strlist *l, const char *s);
int tcx_sl_addf(struct strlist *l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void tcx_sl_free(struct strlist *l);
void tcx_today(char *out, size_t sz);
void tcx_lower_into(const char *s, char *out, size_t sz);
void tcx_progress(const struct tc_export_opts *o, const char *phase, int done, int total, const char *cur);

/* The tracks of the lists as the exporter plans them. With a seed (adding to a drive) paths, folder spellings and artwork numbers continue the
 * drive's, tracks it has already are flagged in on_stick and the deck limits are not checked here. 0, or -1 when out of memory. */
int tcx_gather(const struct tc_export_list *lists, size_t nlists, const struct tc_export_opts *o, const struct tcx_seed *seed, struct gathered *g);
void tcx_gathered_free(struct gathered *g);
uint64_t tcx_estimate_metadata(const struct gathered *g);

int tcx_write_staged(const char *stage, const char *rel, const void *data, size_t len, char *err, size_t errsz);
int tcx_pick_staging_base(const struct tc_export_opts *o, char *out, size_t outsz, int *tmpfs, const struct tc_export_opts *warn_to);
int tcx_stage_track_files(const struct gathered *g, const struct tc_export_opts *o, char *const *analyze_paths, const char *only, const char *stage,
                          struct strlist *anlz_rel, char *errout, size_t errsz);
int tcx_verify_staged_anlz(const char *stage, const char *dat_rel, const char *file_path, char *err, size_t errsz);

/* Writes dest/f->rel: file created exclusively, hashed while it is written, fsynced. */
int tcx_copy_one(const char *dest, struct outfile *f, uint64_t *total, char *err, size_t errsz);

#endif /* TC_RBEXPORT_INTERNAL_H */
