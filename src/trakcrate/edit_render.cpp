#include "edit_render.h"

#include <algorithm>
#include <cmath>

namespace trakcrate {

namespace {

constexpr double kHalfPi = 1.57079632679489661923;

long long msFrames(double ms) { return std::llround(ms * kEditRate / 1000.0); }

// Linear fade of the n frames ending at `end` (down) or starting at `begin` (up).
void fadeOut(std::vector<float>& buf, long long end, long long n)
{
    for (long long k = 0; k < n; ++k) {
        const float g = float(n - 1 - k) / float(n);
        const size_t i = size_t(end - n + k) * 2;
        buf[i] *= g;
        buf[i + 1] *= g;
    }
}

void fadeIn(std::vector<float>& buf, long long begin, long long n)
{
    for (long long k = 0; k < n; ++k) {
        const float g = float(k) / float(n);
        const size_t i = size_t(begin + k) * 2;
        buf[i] *= g;
        buf[i + 1] *= g;
    }
}

// The n frames before `join` become src[e-n, e) fading out against src[s-n, s) fading in (equal power).
void crossPre(std::vector<float>& buf, long long join, long long n, const std::vector<float>& src, long long e, long long s)
{
    for (long long k = 0; k < n; ++k) {
        const double t = (double(k) + 0.5) / double(n) * kHalfPi;
        const float go = float(std::cos(t)), gi = float(std::sin(t));
        const size_t o = size_t(join - n + k) * 2, a = size_t(e - n + k) * 2, b = size_t(s - n + k) * 2;
        buf[o] = src[a] * go + src[b] * gi;
        buf[o + 1] = src[a + 1] * go + src[b + 1] * gi;
    }
}

// How the audio around one seam is treated. before / after = frames of output available on each side of it.
struct Seam {
    long long before, after;
};

long long clampN(long long n, long long a, long long b = (1LL << 60), long long c = (1LL << 60)) { return std::max(0LL, std::min(std::min(n, a), std::min(b, c))); }

} // namespace

bool renderEdit(const DecodedPcm& src, const EditPlan& plan, const BeatGrid& grid, DecodedPcm* out, const std::atomic<bool>* cancel)
{
    const long long total = (long long)src.frames();
    if (plan.joins.size() + 1 != plan.segments.size() && !(plan.segments.empty() && plan.joins.empty())) return false;
    out->sampleRate = src.sampleRate;
    out->stereo.clear();
    std::vector<long long> outStart;
    for (const EditSegment& s : plan.segments) {
        if (s.start < 0 || s.end > total || s.end <= s.start) return false;
        if (cancel && cancel->load()) return false;
        outStart.push_back((long long)out->frames());
        out->stereo.insert(out->stereo.end(), src.stereo.begin() + s.start * 2, src.stereo.begin() + s.end * 2);
    }
    for (size_t i = 0; i < plan.joins.size(); ++i) {
        if (cancel && cancel->load()) return false;
        const EditJoin& j = plan.joins[i];
        const long long J = outStart[i + 1];
        const Seam seam{J - outStart[i], (long long)plan.segments[i + 1].end - plan.segments[i + 1].start};
        std::vector<float>& buf = out->stereo;
        switch (j.algo) {
        case EditAlgo::Cut: {
            const long long n = msFrames(j.param);
            fadeOut(buf, J, clampN(n, seam.before));
            fadeIn(buf, J, clampN(n, seam.after));
            break;
        }
        case EditAlgo::Cross: {
            const long long n = clampN(std::llround(beatFrames(grid, j.param)), seam.before, j.outFrame, j.inFrame);
            if (n == 0) {                                       // no audio before the incoming point (a loop at the start): fall back to short fades
                fadeOut(buf, J, clampN(msFrames(5), seam.before));
                fadeIn(buf, J, clampN(msFrames(5), seam.after));
                break;
            }
            crossPre(buf, J, n, src.stereo, j.outFrame, j.inFrame);
            break;
        }
        case EditAlgo::Fadeout: {
            fadeOut(buf, J, clampN(std::llround(beatFrames(grid, j.param)), seam.before));
            fadeIn(buf, J, clampN(msFrames(5), seam.after));
            break;
        }
        case EditAlgo::TailOverlap: {
            // The removed range starts exactly where the outgoing audio would have continued, so the tail needs no fade in and the outgoing
            // audio no fade out; with no tail this is a plain 5 ms cut.
            const long long k = clampN(std::llround(beatFrames(grid, j.param)), seam.after, j.inFrame - j.outFrame, total - j.outFrame);
            if (k == 0) {
                const long long n = msFrames(5);
                fadeOut(buf, J, clampN(n, seam.before));
                fadeIn(buf, J, clampN(n, seam.after));
                break;
            }
            fadeIn(buf, J, clampN(msFrames(5), seam.after));
            for (long long t = 0; t < k; ++t) {
                const float g = float(k - t) / float(k);
                const size_t o = size_t(J + t) * 2, a = size_t(j.outFrame + t) * 2;
                buf[o] += src.stereo[a] * g;
                buf[o + 1] += src.stereo[a + 1] * g;
            }
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

DecodedPcm renderLoopSample(const DecodedPcm& src, const BeatGrid& grid, long long startBeat, long long lenBeats, EditAlgo algo, double param)
{
    DecodedPcm out;
    out.sampleRate = src.sampleRate;
    if (grid.empty() || startBeat < 0 || lenBeats <= 0) return out;
    if (algo != EditAlgo::Cut && algo != EditAlgo::Cross && algo != EditAlgo::Fadeout) return out;
    const long long a = beatFrame(grid, double(startBeat));
    const long long b = beatFrame(grid, double(startBeat + lenBeats));
    if (b > (long long)src.frames() || b <= a) return out;
    out.stereo.assign(src.stereo.begin() + a * 2, src.stereo.begin() + b * 2);
    const long long len = b - a;
    switch (algo) {
    case EditAlgo::Cut: {
        const long long n = clampN(msFrames(param), len / 2);
        fadeOut(out.stereo, len, n);
        fadeIn(out.stereo, 0, n);
        break;
    }
    case EditAlgo::Cross: {
        const long long n = clampN(std::llround(beatFrames(grid, param)), len / 2, a);   // no audio before the start: no pre-roll
        if (n > 0) crossPre(out.stereo, len, n, src.stereo, b, a);
        else {
            const long long f = clampN(msFrames(5), len / 2);
            fadeOut(out.stereo, len, f);
            fadeIn(out.stereo, 0, f);
        }
        break;
    }
    default: {
        fadeOut(out.stereo, len, clampN(std::llround(beatFrames(grid, param)), len / 2));
        fadeIn(out.stereo, 0, clampN(msFrames(5), len / 2));
        break;
    }
    }
    return out;
}

} // namespace trakcrate
