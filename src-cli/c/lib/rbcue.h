#ifndef TC_RBCUE_H
#define TC_RBCUE_H

#include <stddef.h>
#include <stdint.h>

#include "cuemarks.h"
#include "wave.h"

/*
 * Draws cuemarks into a COPY of a waveform as a full height spike (doc/trakcrate/cuemarks.md). The master waveform is never
 * modified: the exporter calls this on the copy it writes into the ANLZ files. Deterministic.
 */
struct tc_cuerender {
    int enabled;
    int width_columns;      /* columns on the detail waveforms (PWV3, PWV5); the previews always get 1 */
    int green;              /* PWV5 only: green instead of white (the mono tags stay white) */
    int draw_hot;           /* hot cue A-C marks are real cues the deck draws itself, so they are skipped by default */
};

#define TC_SPIKE_MONO ((uint8_t)(31 | (7 << 5)))        /* PWAV / PWV3: height 31, whiteness 7 */
#define TC_SPIKE_TINY ((uint8_t)15)                     /* PWV2: height 15 */
#define TC_SPIKE_WHITE5 ((uint16_t)((7 << 13) | (7 << 10) | (7 << 7) | (31 << 2)))
#define TC_SPIKE_GREEN5 ((uint16_t)((7 << 10) | (31 << 2)))

/* a deep copy of w with the spikes drawn; 0, or -1 when out of memory */
int tc_cuerender(const struct tc_waveform *w, const struct tc_cuemarks *cm, double duration_ms, const struct tc_cuerender *o,
                 struct tc_waveform *out);

#endif /* TC_RBCUE_H */
