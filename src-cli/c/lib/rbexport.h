#ifndef TC_RBEXPORT_H
#define TC_RBEXPORT_H

#include <stddef.h>
#include <stdint.h>

#include "rbcue.h"

/*
 * Exports lists of tracks of a trakcrate library as a rekordbox format drive (doc/trakcrate/rekordbox-db-output.md). The metadata
 * (database, analysis files, artwork, settings) is built in a staging directory on tmpfs and checked there with libcdj's readers;
 * then everything is copied to the destination in one sequential pass, each file written once, audio first and export.pdb last, so an
 * interrupted export leaves no database; then the destination is read back and compared by SHA-256. The destination must be an
 * empty directory. Nothing is ever written anywhere else.
 */
struct tc_export_list {
    const char *name;               /* the playlist's name on the drive */
    const char *const *dirs;        /* its track directories, in play order */
    size_t ndirs;
};

struct tc_export_opts {
    const char *dest;               /* an empty directory, or one that does not exist yet */
    const char *staging_base;       /* NULL: /dev/shm, else $XDG_RUNTIME_DIR, else the system temp directory */
    const uint8_t *tpl;             /* the template export.pdb (required) */
    size_t tpl_len;
    const uint8_t *devsetting, *mysetting;      /* replacement settings files; NULL = the built in ones */
    size_t dev_len, my_len;
    int verify;                     /* read the destination back and compare checksums */
    int allow_unanalysed;           /* tracks without analysis get a flat waveform and no beat grid instead of refusing the export */
    int include_settings, include_artwork, include_pvbr, include_pwv4, include_pwv5, write_manifest;
    struct tc_cuerender cue;        /* the spikes drawn into the exported waveforms (never into the master) */
    int raise_limits;               /* the track/artist/album limits of newer decks instead of the lower ones of older decks (rblimits.h) */
    int dry_run;                    /* gather and check, write nothing (also no staging) */
    int link_audio;                 /* audio files become symbolic links to the master copy, no SHA256SUMS, no read back: the tree is
                                     * the input of an rsync that follows links (trakcrate sync) */
    uint64_t min_free_bytes;        /* space that must remain free on the destination */
    void (*progress)(void *ud, const char *phase, int done, int total, const char *current);
    void (*warn)(void *ud, const char *message);
    void *ud;
};

struct tc_export_result {
    int tracks, playlists, files, verified, staged_on_tmpfs;
    uint64_t audio_bytes, meta_bytes, bytes_written;
    char error[2048];               /* empty on success */
};

/* 0 on success, -1 on failure with res->error set (the destination is cleaned up) */
int tc_export(const struct tc_export_list *lists, size_t nlists, const struct tc_export_opts *o, struct tc_export_result *res);

/* A file name safe on FAT/exFAT: characters Windows forbids replaced by '_', no trailing dots or spaces, reserved device names
 * avoided. Writes at most outsz bytes. */
void tc_fat_safe_name(const char *name, char *out, size_t outsz);
/* The folder name rekordbox gives an artist or album under Contents/: characters Windows forbids become '_', 48 characters (code points) at most,
 * trailing spaces dropped, a final '.' becomes '_', and nothing at all gives `fallback` ("UnknownArtist", "UnknownAlbum"). */
void tc_rb_folder_name(const char *name, const char *fallback, char *out, size_t outsz);

#endif /* TC_RBEXPORT_H */
