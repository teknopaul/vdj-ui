#ifndef BEAT_CLOCK_H
#define BEAT_CLOCK_H

#include <QVector>
#include <QtGlobal>

namespace vdj {

// Pure beat-grid lookup in source-track time (ms). No GUI or audio dependencies.
//
// The grid is the sorted beat times of an analysed track. Before the first beat and after the last beat the grid is
// extrapolated from the neighbouring interval (first interval backwards, last interval forwards), so locate() answers
// everywhere as long as there are at least two beats. `beatNumber` (1..4) counts back / forward modulo 4 there.
//
// `div` subdivides every beat (1 = beat, 2 = half beat, 4 = quarter beat): since/toNext/beatLen then describe the grid
// step (beatLen / div). `index` and `beatNumber` always refer to the beat that contains the position.
class BeatClock {
public:
    struct Position {
        bool valid = false;
        int index = 0;             // beat at or before the position (negative before the first beat, >= size after the last)
        double sinceBeatMs = 0;    // from the grid point at or before the position
        double toNextBeatMs = 0;   // to the next grid point
        double beatLenMs = 0;      // grid step length
        int beatNumber = 1;        // 1..4 within the bar
    };

    void setBeats(const QVector<double>& ms, const QVector<quint8>& numbers);
    void clear();
    bool isValid() const { return beats_.size() >= 2; }

    Position locate(double posMs, int div = 1) const;
    double nearestBeatMs(double posMs, int div = 1) const;

private:
    QVector<double> beats_;
    QVector<quint8> numbers_;
};

// What to do for a quantized jump: seek to `targetMs` now (wait == false; the target already includes the lateness), or
// wait until the source position reaches `triggerMs` and then seek to `targetMs`.
struct QuantizedJump {
    bool wait = false;
    double triggerMs = 0.0;
    double targetMs = 0.0;
};

// Phase-preserving jump: pressed `d` ms after a grid point land `d` after the target; pressed `d` ms before the next grid
// point wait and land exactly on the target. Nearest grid point decides (a tie counts as late). `div` is the global
// quantize resolution. lengthMs <= 0 means unknown (no clamp). Without a valid grid the jump is exact.
QuantizedJump planQuantizedJump(const BeatClock& clock, int div, double posMs, double targetMs, double lengthMs);

// Source-time distance of `beats` beats at the track's original BPM (pure BPM arithmetic, no beat grid). 0 without a BPM.
double beatJumpMs(double originalBpm, int beats);

} // namespace vdj

#endif // BEAT_CLOCK_H
