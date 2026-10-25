#ifndef BEAT_TEMPO_H
#define BEAT_TEMPO_H

#include <cstdint>
#include <deque>

namespace vdj {

// A deck's real tempo from when its beat packets arrive, not from the BPM it reports (real players are not exact
// timekeepers). The arrival times of the last kMaxBeats consecutive beats are fitted with a straight line (time
// against beat number), so network jitter on individual packets averages out. A stopped deck, a jump or a gap
// (the bar position not advancing by one, or an interval far from the running average) starts the window again.
class BeatTempo {
public:
    static constexpr int kMaxBeats = 32;
    static constexpr int kMinBeats = 8;           // beats needed before a tempo is reported
    static constexpr double kMaxDeviation = 0.10; // an interval this far from the average restarts the window

    void reset();
    // barPos 1..4, steadyNs: a monotonic arrival time in nanoseconds
    void addBeat(int barPos, int64_t steadyNs);
    bool valid() const { return int(times_.size()) >= kMinBeats; }
    double bpm() const;                           // 0 when !valid()
    int beats() const { return int(times_.size()); }

private:
    std::deque<int64_t> times_;
    int lastBarPos_ = 0;
};

} // namespace vdj

#endif // BEAT_TEMPO_H
