#ifndef TRAKCRATE_GRID_EDIT_H
#define TRAKCRATE_GRID_EDIT_H

#include "track.h"

#include <string>

// Beat grid edits without Qt (doc/trakcrate/track-editor.md, "Beat grid nudging"). The grid is t_i = t0 + i * 60000 / bpm; nudging moves t0.
// The step sizes are the ones of the editor's buttons.
namespace trakcrate {

constexpr double kNudgeCoarseMs = 10.0;
constexpr double kNudgeNormalMs = 1.0;
constexpr double kNudgeFineMs = 0.1;

// The grid moved by deltaMs (negative = earlier). The first beat is never moved before 0 ms. An empty grid is returned unchanged.
BeatGrid nudgedGrid(const BeatGrid& grid, double deltaMs);

// "Set beat 1 here": the grid moved so that the beat nearest to ms lands exactly on ms. Beat numbering (which beat is a downbeat) is kept.
// An empty grid is returned unchanged.
BeatGrid gridWithBeatAt(const BeatGrid& grid, double ms);

// Offset of the grid's first beat relative to a reference, in ms (for the readout "+3.4 ms").
double gridOffsetMs(const BeatGrid& now, const BeatGrid& reference);

// Writes beatgrid.txt (atomically) with the group "beatgrid" marked with `origin` (undo puts the earlier origin back), and recomputes the beat columns of the cuemarks and the vocal
// regions from their ms values (ms is the truth, the files only change in the beat columns).
bool saveGridEdit(Track* t, const BeatGrid& grid, std::string* err = nullptr, const std::string& origin = "edited");

} // namespace trakcrate

#endif
