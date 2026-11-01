#ifndef SEQ_PHASE_MODEL_H
#define SEQ_PHASE_MODEL_H

#include <cstdint>

namespace vdj {

// The sequencer's beat position as a continuous function of the ALSA queue's real time. The queue's tick counter only moves in whole
// ticks (96 per beat: 5.08 ms at 123 BPM), but its real time is exact; this integrates the tempo over real time instead, so the beat phase
// is known to microseconds. Pure: callers pass the queue's real time in ns.
//
// The model is anchored (reset) when the queue starts, and the tempo must be fed to setTempo() at the real time of every tempo change.
// consistentWith() compares it with the tick counter, which is the floor of the exact position in ticks, so a missed anchor shows up.
class SeqPhaseModel {
public:
    static constexpr double kPpq = 96.0;

    // Anchor: at queue real time `realNs` the position is `beats`, the tempo `usPerBeat` microseconds per beat.
    void reset(int64_t realNs, double beats, double usPerBeat);
    void invalidate() { valid_ = false; }
    bool valid() const { return valid_; }

    // The tempo changes at `realNs`: the old tempo is integrated up to there.
    void setTempo(int64_t realNs, double usPerBeat);

    double usPerBeat() const { return usPerBeat_; }
    double beatsAt(int64_t realNs) const;            // -1 when not valid

    // The tick counter (tickBeats = ticks / kPpq) is the floor of the exact position: is the model within tolTicks of the middle
    // of that tick? False when invalid.
    bool consistentWith(double tickBeats, int64_t realNs, double tolTicks = 1.5) const;

private:
    bool valid_ = false;
    int64_t refNs_ = 0;
    double refBeats_ = 0.0;
    double usPerBeat_ = 500000.0;
};

} // namespace vdj

#endif // SEQ_PHASE_MODEL_H
