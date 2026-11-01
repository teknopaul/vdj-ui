#ifndef BEAT_PHASE_H
#define BEAT_PHASE_H

#include <cstdint>
#include <deque>

namespace vdj {

// Where a deck's beats really are, from the arrival times of its beat packets, knowing that the deck sends them on a timer tick
// (measured: 3.000 ms on the player we tested, see doc/sync-timing.md). A beat that happened at time b leaves at the next tick, so
//     arrival - quantum < b <= arrival      (widened by a tolerance for network jitter)
// for every beat of the window, with b = phase + i * period. The set of (phase, period) that satisfies all of them shrinks with every
// beat; its centre is the estimate, its extent the uncertainty. Averaging does not do this: the quantisation error is a fixed
// sawtooth, not noise, and when the period is close to a multiple of the quantum every packet is equally late and nothing improves
// until the deck's own drift moves the pattern (`resonant`).
//
// The quantum is learned from the data (the spacing of the levels the beat intervals take), or given with setQuantumHint(), and kept
// across reset(). Phase and period are for the beat just added, in the steady clock's milliseconds (the arrival time unit). The
// constant 0..quantum lateness of the packet is not removed: equivalentArrivalMs() adds half a quantum back, which is where a raw
// arrival time sits on average, so the difflock target does not move when this estimate replaces the raw arrival.
class BeatPhase {
public:
    static constexpr int kMaxBeats = 64;
    static constexpr int kMinBeats = 4;               // beats needed before an estimate is made
    static constexpr double kMaxDeviation = 0.10;     // an interval this far from the average restarts the window (as BeatTempo)
    static constexpr double kMinQuantumMs = 1.5;      // smaller quanta cannot be told apart from jitter
    static constexpr double kMaxQuantumMs = 10.0;
    static constexpr double kResonantFraction = 0.25; // phase uncertainty above this fraction of the quantum = resonant

    struct Estimate {
        bool valid = false;               // quantum known and a (phase, period) found
        double quantumMs = 0.0;           // 0 = not known yet
        double periodMs = 0.0;
        double periodPpmUncertainty = -1; // half width, parts per million of the period; -1 when unknown
        double lastBeatMs = 0.0;          // estimated time of the last beat added, steady clock ms
        double phaseUncertaintyMs = 0.0;  // half width of the above
        bool resonant = false;
        double toleranceMs = 0.0;         // the jitter tolerance that was needed (0.25 normally; more means odd packets in the window)
        int beats = 0;
        double equivalentArrivalMs() const { return lastBeatMs + quantumMs / 2.0; }
    };

    void reset();                                     // forgets the beats, keeps the learned quantum
    void setQuantumHint(double ms);                   // use this quantum until another is learned (0 = none)
    void setReportedBpm(double bpm);                  // a change of the deck's reported BPM restarts the window
    // barPos 1..4, steadyNs: a monotonic arrival time in nanoseconds
    void addBeat(int barPos, int64_t steadyNs);
    int beats() const { return int(times_.size()); }
    double quantumMs() const { return quantum_; }

    Estimate estimate() const;

    // The quantum the intervals of `arrivalsMs` point to (the spacing of their levels, snapped to 0.5 ms when close); 0 if they do not
    // show one. Public for tests.
    static double learnQuantum(const std::deque<double>& arrivalsMs);

private:
    std::deque<double> times_;                        // ms
    int lastBarPos_ = 0;
    double reportedBpm_ = 0.0;
    mutable double quantum_ = 0.0;
};

} // namespace vdj

#endif // BEAT_PHASE_H
