#include "beat_clock.h"
#include <algorithm>
#include <cmath>

namespace vdj {

namespace {
int wrap4(int v)
{
    return ((v % 4) + 4) % 4;
}
}

void BeatClock::setBeats(const QVector<double>& ms, const QVector<quint8>& numbers)
{
    beats_ = ms;
    numbers_ = numbers;
}

void BeatClock::clear()
{
    beats_.clear();
    numbers_.clear();
}

BeatClock::Position BeatClock::locate(double posMs, int div) const
{
    Position p;
    const int n = beats_.size();
    if (n < 2) {
        return p;
    }
    if (div < 1) {
        div = 1;
    }

    double start;
    double len;
    int index;
    if (posMs < beats_[0]) {
        len = beats_[1] - beats_[0];
        const int k = int(std::floor((posMs - beats_[0]) / len));
        start = beats_[0] + k * len;
        index = k;
    } else if (posMs >= beats_[n - 1]) {
        len = beats_[n - 1] - beats_[n - 2];
        const int k = int(std::floor((posMs - beats_[n - 1]) / len));
        start = beats_[n - 1] + k * len;
        index = n - 1 + k;
    } else {
        const int i = int(std::upper_bound(beats_.begin(), beats_.end(), posMs) - beats_.begin()) - 1;
        start = beats_[i];
        len = beats_[i + 1] - beats_[i];
        index = i;
    }
    if (len <= 0) {
        return p;
    }

    const double step = len / div;
    const int j = int(std::floor((posMs - start) / step));
    double since = posMs - start - j * step;
    if (since < 0) {
        since = 0;
    }

    p.valid = true;
    p.index = index;
    p.sinceBeatMs = since;
    p.toNextBeatMs = step - since;
    p.beatLenMs = step;

    if (numbers_.size() == n) {
        if (index < 0) {
            p.beatNumber = wrap4(numbers_[0] - 1 + index) + 1;
        } else if (index >= n) {
            p.beatNumber = wrap4(numbers_[n - 1] - 1 + (index - (n - 1))) + 1;
        } else {
            p.beatNumber = numbers_[index];
        }
    } else {
        p.beatNumber = wrap4(index) + 1;
    }
    return p;
}

double BeatClock::nearestBeatMs(double posMs, int div) const
{
    const Position p = locate(posMs, div);
    if (!p.valid) {
        return posMs;
    }
    return p.sinceBeatMs <= p.toNextBeatMs ? posMs - p.sinceBeatMs : posMs + p.toNextBeatMs;
}

QuantizedJump planQuantizedJump(const BeatClock& clock, int div, double posMs, double targetMs, double lengthMs)
{
    QuantizedJump j;
    j.targetMs = targetMs;
    const BeatClock::Position p = clock.locate(posMs, div);
    if (!p.valid)
        return j;
    if (p.sinceBeatMs <= p.toNextBeatMs) {
        j.targetMs = targetMs + p.sinceBeatMs;
        if (lengthMs > 0 && j.targetMs > lengthMs)
            j.targetMs = lengthMs;
    } else {
        j.wait = true;
        j.triggerMs = posMs + p.toNextBeatMs;
    }
    return j;
}

double beatJumpMs(double originalBpm, int beats)
{
    return originalBpm > 0 ? 60000.0 / originalBpm * beats : 0.0;
}

} // namespace vdj
