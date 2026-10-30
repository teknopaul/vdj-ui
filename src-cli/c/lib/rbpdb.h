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

/* DeviceSQL string: short ASCII, long ASCII, or UTF-16LE for anything else. Appends to out as a malloc'd string. */
uint8_t *tc_devsql_string(const char *utf8, size_t *len);

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

/* 0, or -1 with err (unusable template, a row that cannot be laid out, out of memory) */
int tc_pdb_build(const struct tc_export_plan *plan, const uint8_t *tpl, size_t tpl_len, struct tc_built_pdb *out, char *err, size_t errsz);
void tc_built_pdb_free(struct tc_built_pdb *b);

#endif /* TC_RBPDB_H */
