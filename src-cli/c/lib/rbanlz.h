#ifndef TC_RBANLZ_H
#define TC_RBANLZ_H

#include <stddef.h>
#include <stdint.h>

#include "beatgrid.h"
#include "wave.h"

/*
 * Writes rekordbox analysis files, ANLZ0000.DAT and ANLZ0000.EXT (layouts in libcdj/doc-internal/rekordbox-anlz-format.text,
 * checked tag by tag against the real files of the 7 track drive). Unknown words use the values those files have, or zero
 * where they vary (doc/trakcrate/UNKNOWN_BYTES.md). Independent of the C++ writer in src/trakcrate/ (its output is the oracle).
 */
struct tc_anlz_beat {
    uint16_t number;                /* 1..4 within the bar */
    uint16_t bpm_x100;
    uint32_t time_ms;
};

struct tc_anlz_cue {
    int hot_cue;                    /* 0 = memory point, 1..8 = hot cue A..H */
    int is_loop;
    uint32_t time_ms;
    uint32_t loop_time_ms;
    uint32_t marker;                /* 3 bytes; 150 on the first cue of the second sample drive, 1000 on the others */
    uint16_t loop_num, loop_den;
};

struct tc_anlz_track {
    const char *audio_path;                 /* PPTH: "/Contents/Folder/song.mp3" */
    const struct tc_anlz_beat *beats;       /* PQTZ; n == 0 = a track without a grid */
    size_t nbeats;
    const uint16_t *beat_sub_us;            /* PQT2 fractions, one per beat; NULL = zeros */
    const uint8_t *pwav;                    /* 400 */
    const uint8_t *pwv2;                    /* 100 */
    const uint8_t *pwv3;                    /* columns */
    const uint16_t *pwv5;                   /* columns + pwv5_extra */
    size_t columns;
    size_t pwv5_extra;                      /* PWV5 entries beyond columns, written as given */
    const uint8_t *pwv4;                    /* 1200 x 6 bytes; NULL = no PWV4 tag */
    const struct tc_anlz_cue *hot_cues;     /* the .DAT lists the first three, the .EXT all of them */
    size_t nhot;
    const struct tc_anlz_cue *memory_cues;
    size_t nmemory;
    int include_pvbr, include_pwv5;
};

/* 0, or -1 with err for data the format cannot hold (more than 65535 beats ...). *dat and *ext are malloc'd. */
int tc_anlz_build(const struct tc_anlz_track *d, uint8_t **dat, size_t *dat_len, uint8_t **ext, size_t *ext_len, char *err, size_t errsz);

/* A constant tempo grid over duration_ms: beat i at first + i x 60/bpm, numbers 1..4 from the first beat, the time in whole ms
 * plus the microsecond remainder in *sub. Both malloc'd, *n entries; an empty grid gives *n == 0. -1 when out of memory. */
int tc_anlz_beats_from_grid(const struct tc_beatgrid *g, double duration_ms, struct tc_anlz_beat **beats, uint16_t **sub, size_t *n);

/* PWV4 approximation (layout only partly understood, doc/trakcrate/UNKNOWN_BYTES.md) from the detail waveform: 1200 x 6 bytes, malloc'd */
uint8_t *tc_anlz_pwv4_from(const uint16_t *pwv5, size_t columns);

#endif /* TC_RBANLZ_H */
