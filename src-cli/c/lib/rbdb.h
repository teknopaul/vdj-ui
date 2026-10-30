#ifndef TC_RBDB_H
#define TC_RBDB_H

#include <stddef.h>
#include <stdint.h>

#include "rbpdb.h"

/*
 * Reads an export.pdb with libcdj's parsers (an independent reader, not the code that wrote the file) into plain structures, compares
 * it against a plan, and checks a whole rekordbox format drive: database, analysis files, artwork, audio, settings, checksums.
 */
struct tc_rbdb_track {
    uint32_t id, sample_rate, file_size, artwork_id, label_id, bitrate, track_number, bpm_x100, genre_id, album_id, artist_id;
    unsigned disc_number, year, duration_secs, rating;
    char *title, *filename, *file_path, *analyze_path;
};

struct tc_rbdb_named {
    uint32_t id;
    uint32_t artist_id;         /* albums */
    char *name;
};

struct tc_rbdb_playlist {
    uint32_t id, parent_id, sort_order;
    int is_folder;
    char *name;
};

struct tc_rbdb_entry {
    uint32_t entry_index, track_id, playlist_id;
};

struct tc_rbdb {
    struct tc_rbdb_track *tracks;
    size_t ntracks;
    struct tc_rbdb_named *artists, *genres, *labels, *artwork, *albums;
    size_t nartists, ngenres, nlabels, nartwork, nalbums;
    struct tc_rbdb_playlist *playlists;
    size_t nplaylists;
    struct tc_rbdb_entry *entries;
    size_t nentries;
};

/* 0, or -1 with err when the track table cannot be read or memory runs out */
int tc_rbdb_load(const uint8_t *data, size_t len, struct tc_rbdb *db, char *err, size_t errsz);
void tc_rbdb_free(struct tc_rbdb *db);
const struct tc_rbdb_named *tc_rbdb_find(const struct tc_rbdb_named *list, size_t n, uint32_t id);

/* Compares the database with the plan it was built from (counts, titles, ids, paths, playlists). One line per difference through
 * `emit`; returns the number of differences. */
typedef void (*tc_emit_fn)(void *ud, const char *msg);
size_t tc_rbdb_compare_plan(const struct tc_rbdb *db, const struct tc_export_plan *plan, const struct tc_built_pdb *built, tc_emit_fn emit, void *ud);

/* Findings of a drive check. severity is "error" or "warning". */
typedef void (*tc_finding_fn)(void *ud, const char *severity, const char *where, const char *code, const char *message);

struct tc_verify_stats {
    size_t tracks, playlists, files_checked, errors, warnings;
};

/* Checks the tree below root (the folder that holds PIONEER/ and Contents/). Returns 0 when there is no error, 1 when there is
 * at least one, -1 when it could not even start (out of memory). */
int tc_verify_drive(const char *root, tc_finding_fn emit, void *ud, struct tc_verify_stats *stats);

#endif /* TC_RBDB_H */
