#include "beat_phase.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace vdj {

namespace {
constexpr double kTolerances[] = { 0.25, 0.5, 1.0 };    // ms of arrival jitter allowed, tried in turn
constexpr int kScanSteps = 2000;
}

void BeatPhase::reset()
{
    times_.clear();
    lastBarPos_ = 0;
}

void BeatPhase::setQuantumHint(double ms)
{
    quantum_ = (ms >= kMinQuantumMs && ms <= kMaxQuantumMs) ? ms : 0.0;
}

void BeatPhase::setReportedBpm(double bpm)
{
    if (std::fabs(bpm - reportedBpm_) > 0.001)
        reset();
    reportedBpm_ = bpm;
}

void BeatPhase::addBeat(int barPos, int64_t steadyNs)
{
    if (barPos < 1 || barPos > 4) {
        reset();
        return;
    }
    const double ms = double(steadyNs) / 1e6;
    if (!times_.empty()) {
        const bool inSequence = barPos == lastBarPos_ % 4 + 1;
        bool plausible = ms > times_.back();
        if (inSequence && plausible && times_.size() >= 2) {
            const double avg = (times_.back() - times_.front()) / double(times_.size() - 1);
            plausible = std::fabs((ms - times_.back()) - avg) <= avg * kMaxDeviation;
        } else if (inSequence && plausible) {
            const double interval = ms - times_.back();
            plausible = interval > 150.0 && interval < 2000.0;   // 30 .. 400 BPM
        }
        if (!inSequence || !plausible)
            times_.clear();
    }
    times_.push_back(ms);
    lastBarPos_ = barPos;
    while (int(times_.size()) > kMaxBeats)
        times_.pop_front();
}

double BeatPhase::learnQuantum(const std::deque<double>& t)
{
    if (t.size() < 6)
        return 0.0;
    std::vector<double> iv;
    for (size_t i = 1; i < t.size(); ++i)
        iv.push_back(t[i] - t[i - 1]);
    std::sort(iv.begin(), iv.end());
    // levels: runs of intervals less than 0.6 ms apart, those holding at least 5% of the intervals (and 2)
    struct Level { double sum = 0.0; int n = 0; double mean() const { return sum / n; } };
    std::vector<Level> levels;
    Level cur;
    auto flush = [&]() {
        if (cur.n >= std::max(2, int(iv.size() / 20)))
            levels.push_back(cur);
        cur = Level();
    };
    for (size_t i = 0; i < iv.size(); ++i) {
        if (cur.n > 0 && iv[i] - iv[i - 1] > 0.6)
            flush();
        cur.sum += iv[i];
        ++cur.n;
    }
    flush();
    if (levels.size() < 2)
        return 0.0;
    double q = 1e9;
    for (size_t i = 1; i < levels.size(); ++i)
        q = std::min(q, levels[i].mean() - levels[i - 1].mean());
    if (q < kMinQuantumMs || q > kMaxQuantumMs)
        return 0.0;
    // every level must be a whole number of quanta from the first, and the spacing is what they say on average
    double spacingSum = 0.0;
    int spacingN = 0;
    for (size_t i = 1; i < levels.size(); ++i) {
        const double d = levels[i].mean() - levels[0].mean();
        const double k = std::round(d / q);
        if (k < 1 || std::fabs(d - k * q) > 0.35)
            return 0.0;
        spacingSum += d / k;
        ++spacingN;
    }
    q = spacingSum / spacingN;
    // the deck's tick is a timer period, a round number: snap when close
    const double snapped = std::round(q * 2.0) / 2.0;
    if (std::fabs(q - snapped) <= 0.12)
        q = snapped;
    return (q >= kMinQuantumMs && q <= kMaxQuantumMs) ? q : 0.0;
}

BeatPhase::Estimate BeatPhase::estimate() const
{
    Estimate e;
    e.beats = int(times_.size());
    if (e.beats >= 6) {
        const double learned = learnQuantum(times_);
        if (learned > 0.0)
            quantum_ = learned;
    }
    e.quantumMs = quantum_;
    if (e.beats < kMinBeats || quantum_ <= 0.0)
        return e;

    const int n = e.beats;
    const double q = quantum_;
    const double t0 = times_.front();
    std::vector<double> a(n);
    for (int i = 0; i < n; ++i)
        a[i] = times_[i] - t0;
    const double p0 = (a[n - 1] - a[0]) / (n - 1);

    for (double tol : kTolerances) {
        const double span = (q + 2.0 * tol) / (n - 1);          // the period error is below this: the end points are off by at most q + 2 tol
        double pMin = 1e300, pMax = -1e300, lMin = 1e300, lMax = -1e300;
        for (int s = 0; s <= kScanSteps; ++s) {
            const double p = p0 - span + 2.0 * span * s / kScanSteps;
            double lo = -1e300, hi = 1e300;
            for (int i = 0; i < n; ++i) {
                lo = std::max(lo, a[i] - q - tol - i * p);
                hi = std::min(hi, a[i] + tol - i * p);
            }
            if (lo > hi)
                continue;
            pMin = std::min(pMin, p);
            pMax = std::max(pMax, p);
            lMin = std::min(lMin, lo + (n - 1) * p);
            lMax = std::max(lMax, hi + (n - 1) * p);
        }
        if (pMax < pMin)
            continue;
        e.valid = true;
        e.periodMs = (pMin + pMax) / 2.0;
        e.periodPpmUncertainty = (pMax - pMin) / 2.0 / e.periodMs * 1e6;
        e.lastBeatMs = t0 + (lMin + lMax) / 2.0;
        e.phaseUncertaintyMs = (lMax - lMin) / 2.0;
        e.resonant = e.phaseUncertaintyMs > kResonantFraction * q;
        e.toleranceMs = tol;
        return e;
    }
    return e;
}

} // namespace vdj
