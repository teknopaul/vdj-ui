#ifndef TC_RBADD_H
#define TC_RBADD_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "rbcue.h"
#include "rbexport.h"

/*
 * Adds a playlist of the trakcrate library to a pendrive that already has a rekordbox export.pdb (doc/trakcrate/pdb-append-notes.md): the C port of
 * addToDrive in src/trakcrate/exporter.cpp. Tracks the stick has already are referenced, new audio, artwork and analysis files are added, nothing that
 * exists is changed except the one switch of the database: export.pdb.new is written next to the old one, then export.pdb becomes export.pdb.bak
 * (older .bak files rotate) and export.pdb.new becomes export.pdb, two renames. The old database is copied to <backup_dir>/rekordbox-edit-backup-export.
 * <stamp>.pdb first and that copy is never deleted. Cancel is honoured until the switch; before it an error or a cancel removes the files added.
 */
struct tc_add_opts {
    const char *drive;              /* the stick's root */
    const char *staging_base;       /* NULL: /dev/shm, else $XDG_RUNTIME_DIR, else the system temp directory */
    const char *backup_dir;         /* NULL: /var/tmp */
    const uint8_t *tpl;             /* the template export.pdb (required): new track rows copy its unknown bytes */
    size_t tpl_len;
    int allow_unanalysed, include_artwork, include_pvbr, include_pwv4, include_pwv5, raise_limits;
    struct tc_cuerender cue;
    int dry_run;                    /* check everything, write nothing (no backup copy either) */
    uint64_t min_free_bytes;        /* space that must remain free on the stick */
    const char *stop_at;            /* tests: "backup", "planned", "audio", "artwork", "analysis", "database", "switch", "between-renames", "done":
                                     * stop there at once, nothing cleaned up, as if the stick had been pulled */
    volatile int *cancel;           /* set non-zero (by a signal handler) to cancel until the switch */
    void (*progress)(void *ud, const char *phase, int done, int total, const char *current);
    void (*warn)(void *ud, const char *message);
    void *ud;
};

struct tc_add_result {
    int new_tracks, existing_tracks, files_copied;
    int switched, cancelled, staged_on_tmpfs;
    uint64_t bytes_written, audio_bytes, meta_bytes;     /* meta_bytes is an estimate */
    char **created;                 /* files added to the stick, relative to its root, in the order written; after a failure the ones still there */
    size_t ncreated;
    char backup_copy[PATH_MAX];     /* the copy of the old export.pdb made before anything was changed */
    char backup_on_drive[64];       /* PIONEER/rekordbox/export.pdb.bak after the switch */
    char error[2048];               /* empty on success */
};

/* 0 on success, -1 on failure with res->error set (also for a cancel and a test stop; res->cancelled tells the cancel) */
int tc_add_to_drive(const struct tc_export_list *list, const struct tc_add_opts *o, struct tc_add_result *res);
void tc_add_result_free(struct tc_add_result *res);

#endif /* TC_RBADD_H */
