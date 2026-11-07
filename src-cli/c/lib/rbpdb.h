#ifndef TC_RBPDB_H
#define TC_RBPDB_H

#include <stddef.h>
#include <stdint.h>

/*
 * The structure of a rekordbox export.pdb (doc/trakcrate/pdb-write-notes.md, UNKNOWN_BYTES.md) and a builder for one.
 *
 * The model keeps every byte of a file: pages that are not understood stay verbatim (blank pages, index pages, anything a
 * data page check refused), so parse followed by serialise reproduces any file exactly; data pages are held as rows and
 * rebuilt from them. The builder takes the structure and every byte whose meaning is not known from a TEMPLATE, a real
 * export.pdb (the 7 track drive); the caller exports audio, analysis and artwork files, this produces the database that
 * refers to them. Written independently of the C++ in src/trakcrate/ (its output is the test oracle).
 */
#define TC_PDB_PAGE_SIZE 4096u
#define TC_PDB_PAGE_HEADER 40u              /* the row heap starts at 0x28 */
#define TC_PDB_GROUP_SIZE 0x24u             /* 16 row offsets + present bits + row flags, growing down from the page end */
#define TC_PDB_ROWS_PER_GROUP 16u

struct tc_pdb_table {
    uint32_t type;                  /* 0 tracks, 1 genres, 2 artists, 3 albums, 4 labels, 5 keys, 6 colours, 7 playlists, 8 entries ... */
    uint32_t empty_candidate;
    uint32_t first_page;            /* the table's index page */
    uint32_t last_page;
};

struct tc_pdb_row {
    uint8_t *bytes;                 /* the row as stored in the heap */
    size_t len;
    int present;                    /* 0: deleted, the bytes remain in the heap */
};

struct tc_pdb_page {
    uint32_t zero0, index, type, next, u10, u14;
    uint8_t flags;                  /* 0x1b: 0x40 set = not a data page */
    uint16_t u20, num_large, u24, u26;
    int structured;
    uint8_t *raw;                   /* TC_PDB_PAGE_SIZE bytes when not structured */
    struct tc_pdb_row *rows;
    size_t nrows;
    uint16_t *group_flags;          /* one per group of 16 slots */
    size_t ngroups;
};

struct tc_pdb {
    uint32_t next_unused_page, unknown10, sequence, gap;
    struct tc_pdb_table *tables;
    size_t ntables;
    uint8_t *header_tail;           /* the rest of page 0 after the table entries */
    size_t header_tail_len;
    struct tc_pdb_page *pages;      /* pages[0] is unused (the header page is the fields above) */
    size_t npages;
};

/* 0, or -1 with a message in err (not a pdb this module can hold, or out of memory) */
int tc_pdb_parse(const uint8_t *data, size_t len, struct tc_pdb *out, char *err, size_t errsz);
/* malloc'd file bytes; NULL with err when a structured page does not fit or the file is inconsistent */
uint8_t *tc_pdb_serialise(const struct tc_pdb *f, size_t *len, char *err, size_t errsz);
void tc_pdb_free(struct tc_pdb *f);

/* the present rows of every page of the table `type`, walking its chain; *n rows; malloc'd array of pointers into f */
const struct tc_pdb_row **tc_pdb_table_rows(const struct tc_pdb *f, uint32_t type, size_t *n);

/* page geometry and copies, for the code that edits a parsed file (rbappend.c) */
size_t tc_pdb_index_span(size_t slots);                     /* bytes the row index takes at the end of a page with this many slots */
size_t tc_pdb_heap_bytes(const struct tc_pdb_page *p);      /* total bytes of the rows of the page */
size_t tc_pdb_present_rows(const struct tc_pdb_page *p);
void tc_pdb_page_free(struct tc_pdb_page *p);               /* frees the contents of a page, not the struct; leaves it zeroed */
int tc_pdb_clone(const struct tc_pdb *s, struct tc_pdb *d); /* a deep copy; 0, or -1 when out of memory */

/* DeviceSQL string: short ASCII, long ASCII, or UTF-16LE for anything else. Appends to out as a malloc'd string. */
uint8_t *tc_devsql_string(const char *utf8, size_t *len);

/* Reads the DeviceSQL string at byte `at` of a row (short ASCII, 0x40 long ASCII or 0x90 UTF-16LE) as a malloc'd UTF-8 string. 0, or -1 when it
 * does not fit in the row, has an unknown kind or memory ran out; *end (optional) is the offset just after the string. */
int tc_devsql_decode(const uint8_t *row, size_t rowlen, size_t at, char **utf8, size_t *end);

/* ---- the builder ---- */

struct tc_plan_track {
    const char *title, *artist, *album, *genre, *label, *comment, *mix_name;    /* NULL = empty */
    const char *file_name;          /* "song.mp3" */
    const char *file_path;          /* "/Contents/<folder>/song.mp3" */
    const char *analyze_path;       /* "/PIONEER/USBANLZ/P00C/0000D1E2/ANLZ0000.DAT"; NULL = derived from audio_hash */
    const char *audio_hash;         /* hex digest of the audio; NULL falls back to the hash of file_path */
    const char *artwork_key;        /* identical keys share one artwork row; NULL = no artwork */
    const char *artwork_path;       /* "/PIONEER/Artwork/00001/a1.jpg" */
    const char *date_added;         /* "YYYY-MM-DD"; NULL = the template's date */
    uint32_t file_size, sample_rate, bitrate_kbps, track_number, bpm_x100;
    uint32_t disc_number, year, sample_depth, duration_secs, play_count;
    uint8_t rating, color_id;
};

struct tc_plan_playlist {
    const char *name;
    const size_t *tracks;           /* indexes into the plan's tracks, in play order */
    size_t ntracks;
};

struct tc_export_plan {
    const struct tc_plan_track *tracks;
    size_t ntracks;
    const struct tc_plan_playlist *playlists;       /* all at the top level of the tree */
    size_t nplaylists;
};

struct tc_built_pdb {
    uint8_t *bytes;
    size_t len;
    char **analyze_paths;           /* as written, one per plan track */
    char **artwork_paths;           /* distinct artwork paths in id order (id = index + 1) */
    size_t nartwork;
    int data_pages;
};

/* ---- row encoders (each returns a malloc'd row padded to a multiple of 4 with a UTF-16 string on a 4 byte boundary; NULL when out of memory) ---- */

#define TC_TRACK_FIXED 94           /* bytes of a track row before its 21 string offsets */
#define TC_TRACK_SLOTS 21

struct tc_track_template {
    uint8_t fixed[TC_TRACK_FIXED];
    uint8_t *strings[TC_TRACK_SLOTS];
    size_t string_len[TC_TRACK_SLOTS];
    char default_date[32];
};

/* the first present track row of a database: its unknown bytes are copied into every new track row. 1 on success, 0 when it has no usable row */
int tc_track_template_load(const struct tc_pdb *t, struct tc_track_template *out);
void tc_track_template_free(struct tc_track_template *t);

uint8_t *tc_row_track(const struct tc_track_template *tpl, const struct tc_plan_track *t, uint32_t id, uint32_t genre_id, uint32_t album_id,
                      uint32_t artist_id, uint32_t label_id, uint32_t artwork_id, const char *analyze_path, size_t *len);
uint8_t *tc_row_name(uint32_t id, const char *name, size_t *len);                       /* genres, labels, artwork paths */
uint8_t *tc_row_artist(uint32_t id, const char *name, size_t *len);
uint8_t *tc_row_album(uint32_t id, uint32_t artist_id, const char *name, size_t *len);
uint8_t *tc_row_playlist(uint32_t id, uint32_t sort_order, const char *name, size_t *len);
uint8_t *tc_row_entry(uint32_t index, uint32_t track_id, uint32_t playlist_id, size_t *len);

/* 0, or -1 with err (unusable template, a row that cannot be laid out, out of memory) */
int tc_pdb_build(const struct tc_export_plan *plan, const uint8_t *tpl, size_t tpl_len, struct tc_built_pdb *out, char *err, size_t errsz);
void tc_built_pdb_free(struct tc_built_pdb *b);

/* Where the deck looks for the analysis of a track: /PIONEER/USBANLZ/P<partition>/<slot>/ANLZ<counter>.DAT is a function of the track's database
 * path (rekordbox's own rule, doc/trakcrate/usbanalz-hashing.md). */
uint32_t tc_analyze_slot(const char *path);      /* H mod 200003 */
unsigned tc_analyze_partition(uint32_t slot);    /* 0..127 */

#endif /* TC_RBPDB_H */
