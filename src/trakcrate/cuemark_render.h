#ifndef TRAKCRATE_CUEMARK_RENDER_H
#define TRAKCRATE_CUEMARK_RENDER_H

#include "analysis_wave.h"
#include "track.h"

#include <vector>

// Draws cuemarks into a COPY of a waveform as a full height spike (doc/trakcrate/cuemarks.md). The master waveform is never modified: the exporter
// calls this on the copy it writes into the ANLZ files. Deterministic (same input, same bytes) so rsync only touches changed files.
namespace trakcrate {

struct CuemarkRenderOptions {
    bool enabled = true;
    int widthColumns = 2;            // columns on the detail waveforms (PWV3, PWV5); the previews always get 1
    bool green = false;              // PWV5 only: green instead of white (the mono tags stay white)
    bool drawHot = false;            // hot cue A-C marks are real cues the deck draws itself, so they are skipped by default
};

// The colours of the spike, as stored in the tags.
constexpr uint8_t kSpikeMono = 31 | (7 << 5);          // PWAV / PWV3: height 31, whiteness 7
constexpr uint8_t kSpikeTiny = 15;                     // PWV2: height 15
constexpr uint16_t kSpikeWhite5 = (7 << 13) | (7 << 10) | (7 << 7) | (31 << 2);
constexpr uint16_t kSpikeGreen5 = (7 << 10) | (31 << 2);

// Column of a time in the detail waveforms (150 per second), clamped to a waveform of `columns` columns.
size_t detailColumn(double ms, size_t columns);
// Column of a time in a preview of `columns` columns over durationMs, clamped.
size_t previewColumn(double ms, double durationMs, size_t columns);

// The waveform with the spikes drawn. A track with no marks (or disabled) comes back unchanged.
Waveform renderCuemarks(const Waveform& w, const std::vector<Cuemark>& marks, double durationMs, const CuemarkRenderOptions& o = CuemarkRenderOptions());

} // namespace trakcrate

#endif
