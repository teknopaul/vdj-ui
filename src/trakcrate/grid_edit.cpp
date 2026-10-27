#include "grid_edit.h"

#include "cuemark_edit.h"

#include <algorithm>
#include <cmath>

namespace trakcrate {

BeatGrid nudgedGrid(const BeatGrid& grid, double deltaMs)
{
    if (grid.empty()) return grid;
    BeatGrid g = grid;
    const long long us = std::llround(deltaMs * 1000.0);
    g.firstBeatUs = std::max(0LL, grid.firstBeatUs + us);
    return g;
}

BeatGrid gridWithBeatAt(const BeatGrid& grid, double ms)
{
    if (grid.empty()) return grid;
    const double periodUs = 60000000.0 / grid.bpm;
    const double targetUs = ms * 1000.0;
    const double n = std::floor((targetUs - double(grid.firstBeatUs)) / periodUs + 0.5);
    const double nearestUs = double(grid.firstBeatUs) + n * periodUs;
    BeatGrid g = grid;
    double first = double(grid.firstBeatUs) + (targetUs - nearestUs);
    while (first < 0) first += periodUs;                         // before the start of the audio: the same grid, one beat later
    g.firstBeatUs = std::llround(first);
    return g;
}

double gridOffsetMs(const BeatGrid& now, const BeatGrid& reference)
{
    if (now.empty() || reference.empty()) return 0.0;
    return double(now.firstBeatUs - reference.firstBeatUs) / 1000.0;
}

bool saveGridEdit(Track* t, const BeatGrid& grid, std::string* err, const std::string& origin)
{
    setBeatGrid(t->grid, grid);
    t->hasGrid = true;
    setOrigin(t->source, "beatgrid", origin);
    if (t->hasCuemarks) {
        recomputeBeats(&t->cuemarks, grid);
        if (!saveCuemarks(*t, err)) return false;
    }
    if (t->hasVocals) {
        for (VocalRegion& r : t->vocals.regions) {
            r.startBeat = beatOf(grid, r.startMs);
            r.endBeat = beatOf(grid, r.endMs);
        }
        if (!saveVocals(*t, err)) return false;
    }
    return saveBeatGrid(*t, err) && saveSource(*t, err);
}

} // namespace trakcrate
