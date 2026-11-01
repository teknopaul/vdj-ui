#ifndef TC_WAVE_H
#define TC_WAVE_H

#include <stddef.h>
#include <stdint.h>

#include "pcm.h"

/*
 * Pre-rendered waveforms in the shape of the ANLZ tags, and waveform.bin (TRAKCRATE-FORMAT.md section 9,
 * doc/trakcrate/waveforms.md). The height scale is FIXED: one absolute curve for every track (0 dBFS = 31), never
 * normalised per track, so a DJ can see that a track is quiet.
 */
#define TC_WAVE_COLUMNS_PER_SECOND 150
#define TC_WAVE_PREVIEW_COLUMNS 400     /* PWAV */
#define TC_WAVE_TINY_COLUMNS 100        /* PWV2 */

struct tc_waveform {
    uint8_t pwav[TC_WAVE_PREVIEW_COLUMNS];    /* low 5 bits height 0..31, high 3 bits whiteness 0..7 */
    uint8_t pwv2[TC_WAVE_TINY_COLUMNS];       /* low 4 bits height 0..15 */
    uint8_t *pwv3;                            /* columns entries, 150 per second, encoded as PWAV */
    uint16_t *pwv5;                           /* columns + pwv5_extra entries: R3 G3 B3 (bits 15..7), height5 (bits 6..2) */
    size_t columns;                           /* PWV3 entries */
    size_t pwv5_extra;                        /* PWV5 entries beyond columns: a few rekordbox files have one more; kept as imported, 0 otherwise */
};

int tc_height_from_peak(double peak);           /* absolute sample peak 0..1 to 0..31 */
int tc_band_level(double peak);                 /* band peak 0..1 to a 3 bit level */

/* 0, or -1 when out of memory */
int tc_analyse_waveform(const struct tc_pcm *pcm, struct tc_waveform *w);
void tc_waveform_free(struct tc_waveform *w);

/* waveform.bin: "TCWV", u32 version 1, u32 columns, then sections [4 byte tag][u32 length][bytes], lengths little
 * endian, PWV5 entries big endian like the ANLZ tag. malloc'd; NULL when out of memory. */
uint8_t *tc_waveform_serialise(const struct tc_waveform *w, size_t *len);
/* 0 on success, -1 when the bytes are not a valid waveform.bin (unknown sections are skipped; a PWV5 longer than PWV3 is kept in pwv5_extra) */
int tc_waveform_parse(const uint8_t *data, size_t len, struct tc_waveform *w);

/* sample peak and RMS of both channels over the whole track in dBFS (-120 for silence) */
void tc_measure_loudness(const struct tc_pcm *pcm, double *peak_db, double *rms_db);

#endif /* TC_WAVE_H */
