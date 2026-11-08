#include "edit_plan.h"

#include <algorithm>
#include <cmath>

namespace trakcrate {

namespace {

template <size_t N, class T>
bool oneOf(const T (&set)[N], double v)
{
    for (T x : set)
        if (std::fabs(double(x) - v) < 1e-9) return true;
    return false;
}

bool joinParamOk(EditAlgo a, double p)
{
    switch (a) {
    case EditAlgo::Cut: return oneOf(kCutFadeMs, p);
    case EditAlgo::Cross: return oneOf(kCrossBeats, p);
    case EditAlgo::Fadeout: return oneOf(kFadeoutBeats, p);
    case EditAlgo::TailOverlap: return oneOf(kTailBeats, p);
    default: return false;
    }
}

std::string tag(size_t i) { return "Region " + std::to_string(i + 1) + ": "; }

} // namespace

long long beatFrame(const BeatGrid& grid, double beatIndex, int rate)
{
    return std::llround((double(grid.firstBeatUs) / 1e6 + beatIndex * 60.0 / grid.bpm) * rate);
}

double beatFrames(const BeatGrid& grid, double beats, int rate)
{
    return beats * 60.0 / grid.bpm * rate;
}

std::string validateRegions(const BeatGrid& grid, const std::vector<EditRegion>& regions, long long totalFrames)
{
    if (regions.empty()) return std::string();
    if (grid.empty()) return "A beat grid is needed to make an edit";
    for (size_t i = 0; i < regions.size(); ++i) {
        const EditRegion& r = regions[i];
        if (r.startBeat < 0) return tag(i) + "starts before the first beat";
        if (r.lenBeats <= 0) return tag(i) + "is empty";
        if (r.kind == EditKind::Cut) {
            if (r.lenBeats % 4 != 0) return tag(i) + "a cut must be a whole number of bars";
            if (r.algo == EditAlgo::EarlyCut) {
                if (!oneOf(kEarlyShift, r.earlyShift)) return tag(i) + "early cut shifts by 1, 2 or 3 beats";
                if (r.earlyJoinAlgo != EditAlgo::Cut && r.earlyJoinAlgo != EditAlgo::Cross)
                    return tag(i) + "an early cut joins with cut or cross";
                if (!joinParamOk(r.earlyJoinAlgo, r.earlyJoinParam)) return tag(i) + "illegal join setting";
            } else if (!joinParamOk(r.algo, r.algoParam)) {
                return tag(i) + "illegal setting for this algorithm";
            }
        } else {
            if (!oneOf(kLoopBars, r.lenBeats / 4.0) || r.lenBeats % 4 != 0) return tag(i) + "a loop is 1, 2, 4, 8 or 16 bars";
            if (r.loopTimes < kLoopTimesMin || r.loopTimes > kLoopTimesMax) return tag(i) + "a loop is played 2 to 16 times";
            if (r.algo != EditAlgo::Cut && r.algo != EditAlgo::Cross && r.algo != EditAlgo::Fadeout)
                return tag(i) + "a loop uses cut, cross or fadeout";
            if (!joinParamOk(r.algo, r.algoParam)) return tag(i) + "illegal setting for this algorithm";
        }
    }
    // Overlaps are checked on the resolved ranges (an early cut moves its range). Keep the user's numbers by resolving in place.
    std::vector<std::pair<EditRegion, size_t>> rs;
    for (size_t i = 0; i < regions.size(); ++i) rs.push_back({resolveRegions({regions[i]}).front(), i});
    std::stable_sort(rs.begin(), rs.end(), [](const auto& a, const auto& b) { return a.first.startBeat < b.first.startBeat; });
    for (size_t k = 0; k < rs.size(); ++k) {
        const EditRegion& r = rs[k].first;
        if (r.startBeat < 0) return tag(rs[k].second) + "the early cut moves it before the first beat";
        if (beatFrame(grid, r.startBeat + r.lenBeats) > totalFrames + 1) return tag(rs[k].second) + "ends after the end of the track";
        if (k + 1 < rs.size()) {
            const EditRegion& n = rs[k + 1].first;
            if (r.startBeat + r.lenBeats > n.startBeat) return tag(rs[k + 1].second) + "overlaps region " + std::to_string(rs[k].second + 1);
            if (r.kind == EditKind::Cut && n.kind == EditKind::Cut && r.startBeat + r.lenBeats == n.startBeat)
                return tag(rs[k + 1].second) + "touches cut " + std::to_string(rs[k].second + 1) + ", make it one cut";
        }
    }
    return std::string();
}

std::vector<EditRegion> resolveRegions(const std::vector<EditRegion>& regions)
{
    std::vector<EditRegion> out = regions;
    for (EditRegion& r : out) {
        if (r.kind != EditKind::Cut || r.algo != EditAlgo::EarlyCut) continue;
        r.startBeat += r.earlyAfter ? r.earlyShift : -r.earlyShift;
        r.algo = r.earlyJoinAlgo;
        r.algoParam = r.earlyJoinParam;
    }
    std::stable_sort(out.begin(), out.end(), [](const EditRegion& a, const EditRegion& b) { return a.startBeat < b.startBeat; });
    return out;
}

EditPlan planEdit(const BeatGrid& grid, const std::vector<EditRegion>& regions, long long totalFrames)
{
    EditPlan plan;
    if (!validateRegions(grid, regions, totalFrames).empty() || totalFrames <= 0) return plan;
    long long cur = 0;
    auto push = [&](long long a, long long b) {
        if (b > a) plan.segments.push_back({a, b});
    };
    for (const EditRegion& r : resolveRegions(regions)) {
        const long long a = std::min(beatFrame(grid, r.startBeat), totalFrames);
        const long long b = std::min(beatFrame(grid, r.startBeat + r.lenBeats), totalFrames);
        if (r.kind == EditKind::Cut) {
            // A cut at the very start or end has nothing on one side, so no seam.
            if (a > cur && b < totalFrames) {
                push(cur, a);
                plan.joins.push_back({a, b, r.algo, r.algoParam});
            } else if (a > cur) {
                push(cur, a);
            }
            cur = b;
        } else {
            for (int k = 1; k < r.loopTimes; ++k) {
                push(cur, b);
                plan.joins.push_back({b, a, r.algo, r.algoParam});
                cur = a;
            }
        }
    }
    push(cur, totalFrames);
    // A dropped empty segment must not leave a dangling join.
    if (plan.joins.size() + 1 > plan.segments.size()) plan.joins.resize(plan.segments.empty() ? 0 : plan.segments.size() - 1);
    return plan;
}

long long planOutputFrames(const EditPlan& plan)
{
    long long n = 0;
    for (const EditSegment& s : plan.segments) n += s.end - s.start;
    return n;
}

BeatGrid outputGrid(const BeatGrid& in, const EditPlan& plan, int rate)
{
    BeatGrid out = in;
    out.firstBeatUs = 0;
    if (in.empty()) return out;
    long long outStart = 0;
    for (const EditSegment& s : plan.segments) {
        const double periodFrames = beatFrames(in, 1, rate);
        long long i = std::max(0LL, (long long)std::ceil((double(s.start) - double(in.firstBeatUs) / 1e6 * rate) / periodFrames - 1e-9));
        const long long f = beatFrame(in, double(i), rate);
        if (f >= s.start && f < s.end) {
            out.firstBeatUs = std::llround(double(outStart + f - s.start) * 1e6 / rate);
            return out;
        }
        outStart += s.end - s.start;
    }
    return out;
}

} // namespace trakcrate
