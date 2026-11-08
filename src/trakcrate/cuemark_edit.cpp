#include "cuemark_edit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

const char kLoopKey[] = "loop_end_ms=";

// Index of the loop item in m.extra, or -1.
int loopItem(const Cuemark& m)
{
    for (size_t i = 0; i < m.extra.size(); ++i)
        if (m.extra[i].compare(0, sizeof(kLoopKey) - 1, kLoopKey) == 0) return int(i);
    return -1;
}

void putLoopEnd(Cuemark* m, double endMs)
{
    const std::string item = kLoopKey + std::to_string((long long)std::llround(endMs));
    const int i = loopItem(*m);
    if (i >= 0) m->extra[size_t(i)] = item;
    else m->extra.push_back(item);
}

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
    Cuemark& m = f->marks[size_t(index)];
    double end = 0;
    const bool looped = cuemarkLoopEnd(m, &end);
    if (looped) putLoopEnd(&m, end + (ms - m.ms));              // the loop keeps its length
    m.ms = ms;
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

char freeHotLetter(const CuemarkFile& f)
{
    for (char h = 'A'; h <= 'C'; ++h) {
        bool taken = false;
        for (const Cuemark& m : f.marks)
            if (m.hot == h) taken = true;
        if (!taken) return h;
    }
    return 0;
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

bool cuemarkLoopEnd(const Cuemark& m, double* endMs)
{
    const int i = loopItem(m);
    if (i < 0) return false;
    const char* num = m.extra[size_t(i)].c_str() + sizeof(kLoopKey) - 1;
    char* stop = nullptr;
    const double end = std::strtod(num, &stop);
    if (stop == num || !std::isfinite(end) || end <= m.ms) return false;
    if (endMs) *endMs = end;
    return true;
}

double snapLoopBeats(double beats)
{
    if (!(beats > 0)) return kLoopBeats[0];
    double best = kLoopBeats[0], bestD = std::fabs(std::log2(beats / best));
    for (size_t i = 1; i < kLoopBeatsCount; ++i) {
        const double d = std::fabs(std::log2(beats / kLoopBeats[i]));
        if (d < bestD - 1e-12) { best = kLoopBeats[i]; bestD = d; }          // a tie keeps the shorter
    }
    return best;
}

double loopLengthMs(const BeatGrid& grid, double beats)
{
    return grid.empty() ? 0.0 : beats * 60000.0 / grid.bpm;
}

double cuemarkLoopBeats(const Cuemark& m, const BeatGrid& grid)
{
    double end = 0;
    if (grid.empty() || !cuemarkLoopEnd(m, &end)) return 0.0;
    return (end - m.ms) / (60000.0 / grid.bpm);
}

bool setCuemarkLoop(CuemarkFile* f, const BeatGrid& grid, int index, double beats, double durationMs)
{
    if (!valid(f, index) || grid.empty()) return false;
    Cuemark& m = f->marks[size_t(index)];
    const double end = m.ms + loopLengthMs(grid, snapLoopBeats(beats));
    if (durationMs > 0 && end > durationMs) return false;
    putLoopEnd(&m, end);
    return true;
}

bool clearCuemarkLoop(CuemarkFile* f, int index)
{
    if (!valid(f, index)) return false;
    Cuemark& m = f->marks[size_t(index)];
    const int i = loopItem(m);
    if (i < 0) return false;
    m.extra.erase(m.extra.begin() + i);
    return true;
}

bool scaleCuemarkLoop(CuemarkFile* f, const BeatGrid& grid, int index, bool doubleIt, double durationMs)
{
    if (!valid(f, index)) return false;
    const double cur = snapLoopBeats(cuemarkLoopBeats(f->marks[size_t(index)], grid));
    if (cuemarkLoopBeats(f->marks[size_t(index)], grid) <= 0) return false;
    size_t k = 0;
    while (k + 1 < kLoopBeatsCount && kLoopBeats[k] != cur) ++k;
    if (doubleIt ? k + 1 >= kLoopBeatsCount : k == 0) return false;
    return setCuemarkLoop(f, grid, index, kLoopBeats[doubleIt ? k + 1 : k - 1], durationMs);
}

int precedingCuemark(const CuemarkFile& f, double ms)
{
    int found = -1;
    for (size_t i = 0; i < f.marks.size(); ++i)
        if (f.marks[i].ms < ms && (found < 0 || f.marks[i].ms >= f.marks[size_t(found)].ms)) found = int(i);
    return found;
}

bool endCuemarkLoopAt(CuemarkFile* f, const BeatGrid& grid, int index, double ms, double durationMs)
{
    if (!valid(f, index) || grid.empty()) return false;
    const double beats = (ms - f->marks[size_t(index)].ms) / (60000.0 / grid.bpm);
    if (beats < 0.25) return false;
    return setCuemarkLoop(f, grid, index, beats, durationMs);
}

LoopMenuState loopMenuState(const CuemarkFile& f, const BeatGrid& grid, double ms, int hit, double durationMs)
{
    LoopMenuState s;
    if (hit >= 0 && size_t(hit) < f.marks.size()) {
        if (cuemarkLoopEnd(f.marks[size_t(hit)])) s.removeLoopFor = hit;          // needs no grid
        return s;
    }
    const int prev = precedingCuemark(f, ms);
    if (prev >= 0 && !cuemarkLoopEnd(f.marks[size_t(prev)])) s.endLoopFor = prev;
    for (size_t i = 0; i < f.marks.size(); ++i) {
        double end = 0;
        if (cuemarkLoopEnd(f.marks[i], &end) && ms > f.marks[i].ms && ms < end) s.insideLoop = int(i);
    }
    if (s.insideLoop >= 0) {
        CuemarkFile a = f, b = f;
        s.canDouble = scaleCuemarkLoop(&a, grid, s.insideLoop, true, durationMs);
        s.canHalf = scaleCuemarkLoop(&b, grid, s.insideLoop, false, durationMs);
    }
    s.needsGrid = grid.empty() && (s.endLoopFor >= 0 || s.insideLoop >= 0);
    return s;
}

bool saveCuemarkEdit(Track* t, const CuemarkFile& f, std::string* err)
{
    t->cuemarks = f;
    t->hasCuemarks = true;
    setOrigin(t->source, "cues", "edited");
    return saveCuemarks(*t, err) && saveSource(*t, err);
}

} // namespace trakcrate
