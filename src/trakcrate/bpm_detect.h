#ifndef TRAKCRATE_BPM_DETECT_H
#define TRAKCRATE_BPM_DETECT_H

#include "fsutil.h"

#include <atomic>
#include <string>
#include <vector>

namespace trakcrate {

// A BPM estimate from libaubio (the "default" tempo tracker). The user confirms, halves, doubles or replaces it before it is used:
// tempo trackers are often an octave out.
struct BpmDetection {
    bool ok = false;
    std::string error;
    double bpm = 0;                      // 0 = aubio found no tempo
    double confidence = 0;               // aubio's own 0..1 figure
    long long firstBeatUs = 0;           // phase of the beats folded back to the first period of the track
    std::vector<double> beatsSeconds;
};

// True when the library was built with libaubio. Without it detectBpm fails with an explanatory error.
bool bpmDetectAvailable();
BpmDetection detectBpm(const fs::path& audioFile, const std::atomic<bool>* cancel = nullptr);

// Pure helpers (no aubio): the median beat interval as BPM (0 if fewer than 4 beats), and the time of the first beat of a grid at bpm that
// best fits the beat times (circular mean of the beats modulo the period).
double bpmFromBeats(const std::vector<double>& beatsSeconds);
long long firstBeatFromBeats(const std::vector<double>& beatsSeconds, double bpm);

} // namespace trakcrate

#endif
