#include "cuemark_edit.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace trakcrate {

double snapMs(const BeatGrid& grid, double ms, Snap snap, double durationMs)
{
    if (durationMs > 0) ms = std::min(ms, durationMs);
    ms = std::max(0.0, ms);
    if (snap == Snap::None || grid.empty()) return ms;
    const double period = 60000.0 / grid.bpm;
    const double first = double(grid.firstBeatUs) / 1000.0;
    const double step = snap == Snap::Bar ? period * 4 : period;
    double n = std::floor((ms - first) / step + 0.5);
    double r = first + n * step;
    if (r < 0) r += step;                                       // before the first beat: the first one at or after 0
    if (durationMs > 0 && r > durationMs) r -= step;
    return std::max(0.0, r);
}

double beatOf(const BeatGrid& grid, double ms)
{
    if (grid.empty()) return std::numeric_limits<double>::quiet_NaN();
    const double period = 60000.0 / grid.bpm;
    return std::round((1.0 + (ms - double(grid.firstBeatUs) / 1000.0) / period) * 1000.0) / 1000.0;
}

void recomputeBeats(CuemarkFile* f, const BeatGrid& grid)
{
    for (Cuemark& m : f->marks) m.beat = beatOf(grid, m.ms);
}

namespace {

// Sorts by position and returns where `ms` ended up (the first mark at that position).
int settle(CuemarkFile* f, const BeatGrid& grid, double ms)
{
    std::stable_sort(f->marks.begin(), f->marks.end(), [](const Cuemark& a, const Cuemark& b) { return a.ms < b.ms; });
    recomputeBeats(f, grid);
    for (size_t i = 0; i < f->marks.size(); ++i)
        if (f->marks[i].ms == ms) return int(i);
    return -1;
}

bool valid(const CuemarkFile* f, int i) { return i >= 0 && size_t(i) < f->marks.size(); }

} // namespace

int addCuemark(CuemarkFile* f, const BeatGrid& grid, double ms, const std::string& label)
{
    ms = std::max(0.0, std::round(ms * 10.0) / 10.0);          // 0.1 ms, like the file
    for (const Cuemark& m : f->marks)
        if (std::fabs(m.ms - ms) < 1.0) return -1;
    Cuemark c;
    c.ms = ms;
    c.label = label;
    f->marks.push_back(c);
    return settle(f, grid, ms);
}

int moveCuemark(CuemarkFile* f, const BeatGrid& grid, int index, double ms)
{
    if (!valid(f, index)) return -1;
    ms = std::max(0.0, std::round(ms * 10.0) / 10.0);
    for (size_t i = 0; i < f->marks.size(); ++i)
        if (int(i) != index && std::fabs(f->marks[i].ms - ms) < 1.0) return -1;
    f->marks[size_t(index)].ms = ms;
    return settle(f, grid, ms);
}

bool removeCuemark(CuemarkFile* f, int index)
{
    if (!valid(f, index)) return false;
    f->marks.erase(f->marks.begin() + index);
    return true;
}

bool setCuemarkHot(CuemarkFile* f, int index, char hot)
{
    if (!valid(f, index)) return false;
    if (hot != 0 && (hot < 'A' || hot > 'C')) return false;
    if (hot != 0)
        for (Cuemark& m : f->marks)
            if (m.hot == hot) m.hot = 0;
    f->marks[size_t(index)].hot = hot;
    return true;
}

bool setCuemarkLabel(CuemarkFile* f, int index, const std::string& label)
{
    if (!valid(f, index)) return false;
    std::string s = label;
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    f->marks[size_t(index)].label = s;
    return true;
}

bool saveCuemarkEdit(Track* t, const CuemarkFile& f, std::string* err)
{
    t->cuemarks = f;
    t->hasCuemarks = true;
    setOrigin(t->source, "cues", "edited");
    return saveCuemarks(*t, err) && saveSource(*t, err);
}

} // namespace trakcrate
