#ifndef TRAKCRATE_CUEMARK_EDIT_H
#define TRAKCRATE_CUEMARK_EDIT_H

#include "track.h"

#include <string>

// The edits the track editor makes to cuemarks.txt, without any Qt. ms is the truth; the beat column is recomputed from the grid after every change.
// Marks are kept sorted by position. At most one mark holds each hot cue letter (A..C): giving a letter to a mark takes it away from the other.
namespace trakcrate {

enum class Snap { None, Beat, Bar };

// The position a click lands on: the nearest beat (or the nearest downbeat for Bar) of the constant grid within [0, durationMs]. A track without a
// grid, or Snap::None, returns ms unchanged (clamped to the track).
double snapMs(const BeatGrid& grid, double ms, Snap snap, double durationMs);

// beat = 1 + (ms - firstBeat) / beat period; NaN without a grid. Rounded to 3 decimals so the file does not churn.
double beatOf(const BeatGrid& grid, double ms);
void recomputeBeats(CuemarkFile* f, const BeatGrid& grid);

// Each returns the index of the affected mark after sorting, or -1 when nothing was done (bad index, a mark already within 1 ms for add).
int addCuemark(CuemarkFile* f, const BeatGrid& grid, double ms, const std::string& label = std::string());
int moveCuemark(CuemarkFile* f, const BeatGrid& grid, int index, double ms);
bool removeCuemark(CuemarkFile* f, int index);
bool setCuemarkHot(CuemarkFile* f, int index, char hot);          // 0 clears it
bool setCuemarkLabel(CuemarkFile* f, int index, const std::string& label);   // tabs and newlines become spaces

// Writes cuemarks.txt (atomically) and marks the cues group "edited" in source.txt.
bool saveCuemarkEdit(Track* t, const CuemarkFile& f, std::string* err = nullptr);

} // namespace trakcrate

#endif
