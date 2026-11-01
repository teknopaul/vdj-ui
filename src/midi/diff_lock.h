#ifndef DIFF_LOCK_H
#define DIFF_LOCK_H

namespace vdj {

// "difflock" as in adj (adj_vdj.c / adj_diff.c): keep the VDJ's sequencer at a fixed offset (targetMs) from the
// beat of one chosen deck. Every beat of every deck the offset between its beat and the sequencer's beat is
// recorded; for the deck we are locked to, the rolling average of the last kAvgSize beats is compared with the
// target on the last beat of the bar and the sequencer is nudged by the (limited) difference.
//
// Offsets are signed milliseconds: positive when the deck's beat comes after ours (we are early, so the nudge is
// positive too: make our next beat that much longer).
class DiffLock {
public:
    static constexpr int kAvgSize = 4;          // beats in the rolling average (AVG_SIZE)
    static constexpr int kLimitMs = 20;         // largest single nudge (DIFF_LIMIT)
    // While the followed deck's BPM is changing (BpmChangeDetector) the offset is corrected on every beat in small steps: the mean of the
    // last two beats, half of it, at most kFastLimitMs. The numbers come from the fader runs of 2026-11-01 (doc/sync.md): the offset sat
    // +3 to +4 ms high for several beats in a rise when only the last beat of the bar corrected; half a step on every beat keeps a
    // drift of 2 ms per beat under about 4 ms in a simulation (doc test), and a 5 ms step is not audible.
    static constexpr int kFastLimitMs = 5;
    static constexpr double kFastGain = 0.5;
    static constexpr int kMaxPlayers = 32;
    static constexpr double kMaxDiffMs = 250.0; // offsets beyond this are noise (vdj_time_diff)

    DiffLock() { reset(); }

    void reset();                               // adj_diff_reset

    // Lock to a deck (adj_vdj_difflock). Locking to a deck that is not the master ends "follow master".
    bool lock(int player);
    void off();                                 // adj_vdj_difflock_arff: unlock and forget the measurements
    // Sync controller support. rebase: follow `player` from now, forgetting its measurements, and hold (no nudges) until setHold(false);
    // suspend: follow nobody but keep the target and the follow-master flag. A hold only stops the nudges, the measuring goes on.
    void rebase(int player);
    void suspend() { player_ = 0; hold_ = false; }
    void setHold(bool hold) { hold_ = hold; }
    void setFast(bool fast) { fast_ = fast; }   // the followed deck's BPM is changing: correct every beat, in small steps
    bool fast() const { return fast_; }
    bool holding() const { return hold_; }
    int player() const { return player_; }      // 0 = not locked
    bool locked() const { return player_ != 0; }

    // Follow master (adj_vdj_difflock_master): lock to whichever deck is master, now and when it changes.
    void setFollowMaster(bool on);
    bool followMaster() const { return followMaster_; }
    void masterChanged(int player);             // a deck reports itself as master
    int master() const { return master_; }

    void setTargetMs(int ms) { targetMs_ = ms; }
    int targetMs() const { return targetMs_; }
    void nudgeTarget(int delta) { targetMs_ += delta; }   // adj_vdj_difflock_nudge

    // A deck's beat. diffMs: its offset from our beat. Returns the nudge (ms) to apply to the sequencer, 0 for none.
    int onBeat(int player, int barPos, double diffMs);

    double lastDiff(int player) const;          // adj_diff_get
    double avgDiff(int player) const;           // adj_diff_avg, 0 until kAvgSize beats were seen
    bool hasAverage(int player) const;

    static int limit(int ms);

private:
    int player_ = 0;
    int master_ = 0;
    bool followMaster_ = false;
    bool hold_ = false;
    bool fast_ = false;
    int targetMs_ = 0;
    double last_[kMaxPlayers];
    double set_[kMaxPlayers][kAvgSize];
    int pos_[kMaxPlayers];
    int count_[kMaxPlayers];
};

} // namespace vdj

#endif // DIFF_LOCK_H
