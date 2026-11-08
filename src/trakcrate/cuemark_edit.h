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
// The first of A, B, C that no mark holds, or 0 when all three are taken.
char freeHotLetter(const CuemarkFile& f);
bool setCuemarkLabel(CuemarkFile* f, int index, const std::string& label);   // tabs and newlines become spaces

// Loops: a mark with a loop has a `loop_end_ms=<ms>` item in its extra columns (the same one the rekordbox import writes). Its length is one of
// kLoopBeats of the constant grid, so setting or resizing one needs a grid; removing one and moving the mark do not.
constexpr double kLoopBeats[] = {0.5, 1, 2, 4, 8, 16, 32};
constexpr size_t kLoopBeatsCount = sizeof(kLoopBeats) / sizeof(kLoopBeats[0]);

bool cuemarkLoopEnd(const Cuemark& m, double* endMs = nullptr);     // false without a loop item or when the end is not after the mark
// The allowed length nearest to `beats`, compared by ratio (3 beats -> 4, 2.5 -> 2); an exact tie on that scale goes to the shorter. Never 0.
double snapLoopBeats(double beats);
double loopLengthMs(const BeatGrid& grid, double beats);             // 0 without a grid
double cuemarkLoopBeats(const Cuemark& m, const BeatGrid& grid);     // length in beats as stored (not snapped); 0 without a loop or a grid
// Gives the mark a loop of `beats` (snapped to an allowed length). False without a grid, for a bad index, or when the end would pass durationMs
// (0 = the track length is not known, no limit).
bool setCuemarkLoop(CuemarkFile* f, const BeatGrid& grid, int index, double beats, double durationMs = 0);
bool clearCuemarkLoop(CuemarkFile* f, int index);                    // keeps every other extra column; false when there was no loop
// One allowed size up or down from the current length (an odd imported length is snapped first). False at 32 / 0.5, past durationMs, without a grid
// or a loop.
bool scaleCuemarkLoop(CuemarkFile* f, const BeatGrid& grid, int index, bool doubleIt, double durationMs = 0);
// The mark directly before ms (last with mark.ms < ms), of any kind; -1 when there is none. "End loop here" applies to it, if it has no loop yet.
int precedingCuemark(const CuemarkFile& f, double ms);

// "End loop here": the loop of mark `index` ends at the click `ms`, snapped to an allowed length. False without a grid, for a click less than a quarter
// of a beat after the mark, or when the loop would pass durationMs.
bool endCuemarkLoopAt(CuemarkFile* f, const BeatGrid& grid, int index, double ms, double durationMs = 0);

// What the right click menu offers at `ms`; hit is the mark under the pointer or -1. Indexes are -1 when the entry is not offered.
struct LoopMenuState {
    int endLoopFor = -1;        // "End loop here": the directly preceding mark, when it has no loop yet (never on a mark's own line)
    int insideLoop = -1;        // the pointer is inside this mark's loop (not on its line): "Double loop" / "Half loop"
    bool canDouble = false, canHalf = false;
    int removeLoopFor = -1;     // the pointer is on a mark that has a loop
    bool needsGrid = false;     // a loop entry would be offered but there is no grid (shown disabled)
};
LoopMenuState loopMenuState(const CuemarkFile& f, const BeatGrid& grid, double ms, int hit, double durationMs = 0);

// Writes cuemarks.txt (atomically) and marks the cues group "edited" in source.txt.
bool saveCuemarkEdit(Track* t, const CuemarkFile& f, std::string* err = nullptr);

} // namespace trakcrate

#endif
