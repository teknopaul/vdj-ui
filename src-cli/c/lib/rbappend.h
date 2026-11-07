#ifndef TC_RBAPPEND_H
#define TC_RBAPPEND_H

#include <stddef.h>
#include <stdint.h>

#include "rbpdb.h"

/*
 * Adding a playlist to the export.pdb that is already on a rekordbox (or trakcrate) pendrive, without rebuilding the database
 * (doc/trakcrate/pdb-append-notes.md). A port of src/trakcrate/pdb_append.cpp: the same rules, the same messages, and byte identical
 * results (src-cli/test/test_append_oracle.sh). Existing rows and pages are never changed, index pages are left alone.
 */

#define TC_DRIVE_MAX_TABLE_TYPE 32

struct tc_drive_track {
    uint32_t id, file_size;
    char *title, *file_path, *analyze_path;
};

struct tc_name_id {                 /* an artist, genre, label or artwork path (artist 0), or an album (artist = the artist's id) */
    char *name;
    uint32_t artist, id;
};

/* What an existing database holds, indexed for the code that adds to it. Built by tc_drive_db_load, which also checks the database. */
struct tc_drive_db {
    struct tc_pdb file;                                 /* the whole file, unchanged */
    uint32_t max_id[TC_DRIVE_MAX_TABLE_TYPE];           /* per table type: the highest id of any row, present or deleted */
    struct tc_name_id *artist, *genre, *label, *artwork, *album;   /* of present rows; the lowest id of a name wins */
    size_t nartist, ngenre, nlabel, nartwork, nalbum;
    struct tc_drive_track *tracks;                      /* present rows */
    char **track_lower;                                 /* lower case file path of each of the tracks */
    size_t ntracks;
    char **analyze_paths;                               /* of every track row, present or deleted */
    size_t nanalyze;
    char **playlist_lower;                              /* lower case names of the present playlists, with their ids */
    uint32_t *playlist_id;
    size_t nplaylists;
    uint32_t max_root_sort_order;
    uint32_t artwork_count;
    char **warnings;                                    /* odd, but they do not stop an addition */
    size_t nwarnings;
};

/* 0 when the database can be added to. -1 with the reasons in *problems (a malloc'd array of nproblems malloc'd lines, at most 40; free with
 * tc_problems_free) when it cannot; `out` is filled as far as it got and must be freed with tc_drive_db_free either way. */
int tc_drive_db_load(const uint8_t *bytes, size_t len, struct tc_drive_db *out, char ***problems, size_t *nproblems);
void tc_drive_db_free(struct tc_drive_db *db);
void tc_problems_free(char **problems, size_t n);

struct tc_append_result {
    uint8_t *bytes;                 /* the new database */
    size_t len;
    uint32_t *track_ids;            /* one per track of the plan: the id of its row (a track nobody references gets 0) */
    int *track_is_new;              /* 0: the track was already on the stick, its row and files are used as they are */
    char **analyze_paths;           /* one per plan track, as the database names them */
    char **artwork_paths;           /* artwork paths of the new artwork rows */
    size_t ntracks, nartwork;
    uint32_t playlist_id;
    int rows_added, pages_added;
};

void tc_append_result_free(struct tc_append_result *r);

/* Adds the playlists of the plan to the database. A plan track whose file_path is the path of a track already in the database (case insensitive) is
 * referenced, not added. 0 on success: `db` is then replaced by the new database. -1 with err and `db` untouched when a playlist name already exists,
 * a row cannot be laid out or the result would not pass tc_drive_db_load. tpl is the template export.pdb of full exports. */
int tc_append_playlist(struct tc_drive_db *db, const struct tc_export_plan *plan, const uint8_t *tpl, size_t tpl_len, struct tc_append_result *out,
                       char *err, size_t errsz);

/* Compares the database before and after an addition and the plan (every old row and page unchanged, the new rows read back through libcdj, the
 * entries in order, flags and counters consistent). Returns the number of differences; the first up to 8 as malloc'd lines in *diffs (free with
 * tc_problems_free). 0 means the addition is what was asked for. */
size_t tc_verify_appended(const uint8_t *before, size_t before_len, const uint8_t *after, size_t after_len, const struct tc_export_plan *plan,
                          const struct tc_append_result *result, char ***diffs);

/* the entry of a sorted list of the database (artist, genre, label, artwork or album) called `name` (for albums: of the artist with that id; else artist 0);
 * NULL when there is none */
const struct tc_name_id *tc_name_find(const struct tc_name_id *v, size_t n, uint32_t artist, const char *name);

/* rekordbox's analysis folder of a track path (/PIONEER/USBANLZ/P<partition>/<slot>/ANLZ<counter>.DAT), the first counter whose path is not in
 * `used` (nused strings); the result is malloc'd. */
char *tc_analyze_path_for(const char *file_path, char *const *used, size_t nused);

#endif /* TC_RBAPPEND_H */
