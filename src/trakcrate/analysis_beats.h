#ifndef TRAKCRATE_ANALYSIS_BEATS_H
#define TRAKCRATE_ANALYSIS_BEATS_H

#include "audio_decode.h"
#include "track.h"

namespace trakcrate {

struct BeatAnalysis {
    BeatGrid grid;               // empty (bpm 0) when no usable tempo was found
    double confidence = 0;       // 0..1; below kMinConfidence the grid is left empty
    double periodSec = 0;        // unrounded beat period of the fitted grid (0 when none)
    int beatsFitted = 0;
    static constexpr double kMinConfidence = 0.5;
};

// Constant tempo beat grid (doc/trakcrate/beat-grid.md): onset envelope, autocorrelation for the tempo, folding to find the exact period and
// phase, then a weighted least squares fit through the onsets near the predicted beats. priorBpm (an ID3 tempo, 0 = none) picks the
// octave. The first beat is the first grid point at or after the start of the audio. No usable periodicity gives an empty grid:
// a wrong grid is worse than none.
BeatAnalysis analyseBeats(const DecodedPcm& pcm, double priorBpm = 0, double minBpm = 70, double maxBpm = 180);

} // namespace trakcrate

#endif
