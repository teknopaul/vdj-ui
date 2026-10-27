#ifndef TRAKCRATE_ANALYSIS_H
#define TRAKCRATE_ANALYSIS_H

#include "analysis_beats.h"
#include "analysis_wave.h"

#include <atomic>
#include <string>
#include <vector>

namespace trakcrate {

struct AnalyseOptions {
    bool force = false;          // also redo fields whose origin is rekordbox or edited (the origin becomes generated)
    bool beatgrid = true;
    bool waveform = true;
};

struct AnalyseResult {
    bool ok = false;
    std::string error;
    bool beatgridWritten = false;
    bool waveformWritten = false;
    BeatGrid grid;
    double confidence = 0;
    std::vector<std::string> skipped;       // "beatgrid (origin rekordbox)" ...
};

// Analyses one track folder: decodes the audio once, then writes beatgrid.txt (bpm 0 when no tempo was found), waveform.bin and the
// loudness values (peak_dbfs, rms_dbfs) in meta.txt. Fields whose origin is rekordbox or edited are left alone unless force is set.
// Output is deterministic: the same audio gives the same bytes.
AnalyseResult analyseTrack(const fs::path& trackDir, const AnalyseOptions& opts = AnalyseOptions(), const std::atomic<bool>* cancel = nullptr);

// Reads waveform.bin of a track folder.
bool loadWaveform(const fs::path& trackDir, Waveform* out, std::string* err = nullptr);

} // namespace trakcrate

#endif
