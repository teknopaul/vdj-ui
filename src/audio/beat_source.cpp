#include "beat_source.h"
#include "midi/alsa_clock.h"

namespace vdj {

bool AlsaClockBeatSource::running() const
{
    return clock_ && clock_->running();
}

double AlsaClockBeatSource::bpm() const
{
    return running() ? clock_->effectiveBpm() : 0.0;
}

double AlsaClockBeatSource::beatsAtSteadyNs(qint64 ns) const
{
    if (!running())
        return -1.0;
    int64_t now = steadyNowNs();
    const double pos = clock_->positionBeatsExact(&now);     // the position and its steady time come from one read
    if (pos < 0.0)
        return -1.0;
    return pos + double(ns - now) * clock_->effectiveBpm() / 60e9;
}

qint64 AlsaClockBeatSource::steadyNsAtBeat(double beat) const
{
    if (!running())
        return 0;
    int64_t now = steadyNowNs();
    const double pos = clock_->positionBeatsExact(&now);
    if (pos < 0.0)
        return 0;
    return now + qint64((beat - pos) * 60e9 / clock_->effectiveBpm());
}

} // namespace vdj
