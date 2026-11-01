#ifndef SYNC_ALIGNER_H
#define SYNC_ALIGNER_H

namespace vdj {

// Brings the sequencer's beat onto the reference deck's beat (plus the DJ's target offset) when Sync is pressed.
// Pure arithmetic: the caller measures the offset, applies the steps (AlsaClock::phaseShiftMs), one per beat.
//
// The shift is wrapped to the nearest beat (+/- half a beat), so we always take the short way round, and cut into steps of at
// most kStepMs so the move is a slew and not a jump.
class SyncAligner {
public:
    static constexpr double kStepMs = 40.0;      // largest single move (kAlignStepMs): to be set from listening tests
    static constexpr double kDoneMs = 1.0;       // already this close: nothing to do

    struct Plan {
        double shiftMs = 0.0;      // total move of the sequencer, positive = later (the beat gets longer), like nudgeMillis
        int beats = 0;             // steps; 0 = nothing to do
        double waitMs = 0.0;       // from now to the first step (a quantize grid point), 0 = the next beat
        bool needed() const { return beats > 0; }
    };

    // avgDiffMs: the reference's measured offset from our beat (DiffLock's convention, positive = we are early);
    // targetMs: where the DJ wants it. waitToGridMs: time to the next grid point of the Quantize division, 0 for none.
    static Plan plan(double avgDiffMs, double targetMs, double beatLenMs, double waitToGridMs = 0.0);

    // The i-th step in whole milliseconds; the steps add up to the rounded shift.
    static int step(const Plan& plan, int i);

    // x wrapped into [-beatLen/2, +beatLen/2)
    static double wrap(double x, double beatLenMs);
};

} // namespace vdj

#endif // SYNC_ALIGNER_H
