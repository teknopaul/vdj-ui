#ifndef VDJ_TRAKCRATE_UI_ANALYSIS_H
#define VDJ_TRAKCRATE_UI_ANALYSIS_H

#include <QString>

#include "track_analysis.h"

namespace vdj {

// The analysis of a track in the trakcrate master (beatgrid.txt, cuemarks.txt, waveform.bin) as the structures the waveform and the player
// already use for tracks read from a pendrive. Nothing is written. valid is false when the folder holds no usable analysis at all.
// Cuemarks with a hot cue letter A-C become hot cues 1-3; all other cuemarks become memory points.
TrackAnalysis trakcrateTrackAnalysis(const QString& trackDir);

} // namespace vdj

#endif
