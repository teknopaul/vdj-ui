#ifndef TRAKCRATE_STRETCH_PROBE_H
#define TRAKCRATE_STRETCH_PROBE_H

#include "stretch_engine.h"

namespace trakcrate {

struct StretchQuality {
    bool ok = false;
    std::string error;
    double durationError = 0;     // (output frames - expected) / expected
    double pitchRatio = 1;        // pitch of the 110 Hz tone after / before (1 = preserved)
    double onsetErrorMs = 0;      // mean distance of the clicks from where they should be
    double rmsRatio = 1;          // output level / input level
};

// Stretches synthetic material (a 110 Hz tone with a click on every beat of 120 bpm, 12 s) and measures the result.
// Says nothing about how music sounds; it catches broken engines and gross errors.
StretchQuality probeEngine(StretchEngine& engine, const StretchOptions& opts, double ratio);
DecodedPcm probeMaterial(double seconds = 12, double bpm = 120);

} // namespace trakcrate

#endif
