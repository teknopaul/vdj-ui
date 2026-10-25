#include "beat_tempo.h"
#include <cmath>

namespace vdj {

void BeatTempo::reset()
{
    times_.clear();
    lastBarPos_ = 0;
}

void BeatTempo::addBeat(int barPos, int64_t steadyNs)
{
    if (barPos < 1 || barPos > 4) {
        reset();
        return;
    }
    if (!times_.empty()) {
        const bool inSequence = barPos == lastBarPos_ % 4 + 1;
        bool plausible = steadyNs > times_.back();
        if (inSequence && plausible && times_.size() >= 2) {
            const double avg = double(times_.back() - times_.front()) / double(times_.size() - 1);
            const double interval = double(steadyNs - times_.back());
            plausible = std::fabs(interval - avg) <= avg * kMaxDeviation;
        } else if (inSequence && plausible) {
            const double interval = double(steadyNs - times_.back());
            plausible = interval > 150e6 && interval < 2000e6;   // 30 .. 400 BPM
        }
        if (!inSequence || !plausible)
            times_.clear();
    }
    times_.push_back(steadyNs);
    lastBarPos_ = barPos;
    while (int(times_.size()) > kMaxBeats)
        times_.pop_front();
}

double BeatTempo::bpm() const
{
    const int n = int(times_.size());
    if (n < kMinBeats)
        return 0.0;
    // least squares slope of time (ns, relative to the first beat) against beat index
    const double t0 = double(times_.front());
    const double meanI = (n - 1) / 2.0;
    double meanT = 0.0;
    for (int i = 0; i < n; ++i)
        meanT += double(times_[i]) - t0;
    meanT /= n;
    double cov = 0.0, var = 0.0;
    for (int i = 0; i < n; ++i) {
        cov += (i - meanI) * ((double(times_[i]) - t0) - meanT);
        var += (i - meanI) * (i - meanI);
    }
    const double nsPerBeat = cov / var;
    return nsPerBeat > 0.0 ? 60e9 / nsPerBeat : 0.0;
}

} // namespace vdj
