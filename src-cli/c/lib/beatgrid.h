#ifndef TC_BEATGRID_H
#define TC_BEATGRID_H

#include "kv.h"

/*
 * beatgrid.txt (TRAKCRATE-FORMAT.md section 5): a constant tempo grid.
 * Beat n (from 0) is at first_beat_us + n * 60e6 / bpm microseconds. bpm 0 = analysed, no usable grid.
 */
struct tc_beatgrid {
    double bpm;                 /* 0: none */
    long long first_beat_us;
    double confidence;          /* 0..1, negative when the file has none */
    char analyser[64];          /* "" when absent */
    int variable_tempo;
};

/* from the key/value document; missing keys give bpm 0, first_beat_us 0, confidence -1 */
void tc_beatgrid_from(const tc_kvdoc *doc, struct tc_beatgrid *g);
/* into the document (other keys and comments stay); 0, or -1 when out of memory */
int tc_beatgrid_to(tc_kvdoc *doc, const struct tc_beatgrid *g);
/* time in microseconds of beat n; 0 when the grid is empty */
long long tc_beatgrid_beat_us(const struct tc_beatgrid *g, long n);

#endif /* TC_BEATGRID_H */
