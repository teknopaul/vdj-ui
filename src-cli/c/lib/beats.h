#ifndef TC_BEATS_H
#define TC_BEATS_H

#include "beatgrid.h"
#include "pcm.h"

/*
 * Constant tempo beat grid detection (doc/trakcrate/beat-grid.md): an onset envelope (low band kicks and high-passed
 * hats), autocorrelation for the tempo, folding to find the exact period and phase, then a weighted least squares fit
 * through the onsets near the predicted beats. A tempo prior (an ID3 TBPM, 0 = none) picks the octave. The first beat is
 * the first grid point at or after the start of the audio. No usable periodicity gives an empty grid (bpm 0): a wrong
 * grid is worse than none. Same input, same output.
 */
#define TC_BEATS_MIN_CONFIDENCE 0.5

struct tc_beat_analysis {
    struct tc_beatgrid grid;    /* bpm 0 when no usable tempo was found */
    double confidence;          /* 0..1; below TC_BEATS_MIN_CONFIDENCE the grid is left empty */
    double period_sec;          /* unrounded beat period of the fitted grid, 0 when none */
    int beats_fitted;
};

/* 0, or -1 when out of memory (res is then empty) */
int tc_analyse_beats(const struct tc_pcm *pcm, double prior_bpm, double min_bpm, double max_bpm, struct tc_beat_analysis *res);

#endif /* TC_BEATS_H */
