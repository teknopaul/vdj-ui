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
    static constexpr int kMaxPlayers = 32;
    static constexpr double kMaxDiffMs = 250.0; // offsets beyond this are noise (vdj_time_diff)

    DiffLock() { reset(); }

    void reset();                               // adj_diff_reset

    // Lock to a deck (adj_vdj_difflock). Locking to a deck that is not the master ends "follow master".
    bool lock(int player);
    void off();                                 // adj_vdj_difflock_arff: unlock and forget the measurements
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
    int targetMs_ = 20;
    double last_[kMaxPlayers];
    double set_[kMaxPlayers][kAvgSize];
    int pos_[kMaxPlayers];
    int count_[kMaxPlayers];
};

} // namespace vdj

#endif // DIFF_LOCK_H
