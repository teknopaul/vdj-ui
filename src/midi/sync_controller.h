#ifndef SYNC_CONTROLLER_H
#define SYNC_CONTROLLER_H

namespace vdj {

class DiffLock;

// What the Sync buttons mean. DiffLock measures and nudges; this decides *who* the sequencer follows and when it may move.
//
//  - Sync is a mode, not an action. A remote deck's Sync button selects that deck, the VDJ row's Sync button selects "the master". Pressing
//    the selected button again turns Sync off; the other button moves it.
//  - It is sticky. With "master" selected and the master being us, nothing is followed (Suspended) but the mode stays on, and it follows
//    the next master when there is one. A deck that leaves ends a deck selection; a master that leaves leaves the mode waiting.
//  - A change of what we follow while Sync is on (another deck, a new master) must not move anything the DJ hears: the sequencer's BPM and
//    position are held (DiffLock::setHold), the offset to the new reference is measured over DiffLock::kAvgSize beats and then *becomes* the
//    target (the ear-set alignment was against the old reference), and following resumes (Locked).
//  - The first engagement keeps the DJ's target and lets DiffLock nudge towards it (Aligning) until the offset is within one nudge.
//
// Pure: no widgets, no ALSA. The caller reports presses, masters and leaving decks, and calls onBeat() after DiffLock::onBeat for the
// referenced deck.
class SyncController {
public:
    enum class Mode { Off, Deck, Master };
    enum class Phase {
        Idle,        // nothing followed: Off, or Sync is on for "master" and there is no master yet
        Aligning,    // first engagement: the sequencer is being brought to the target
        Rebasing,    // the reference changed: everything is held while the new offset is measured
        Locked,      // following
        Suspended    // Sync is on for "master" and we are the master
    };

    // How fast the sequencer's BPM may move towards a new reference BPM, per beat. Provisional: tune with real decks.
    static constexpr double kGlideBpmPerBeat = 0.1;

    explicit SyncController(DiffLock& lock) : lock_(lock) {}

    // A Sync button was pressed: `pid` is that row's deck, `isLocalRow` the VDJ row.
    void press(int pid, bool isLocalRow);
    void off();
    // `pid` is the master now; `localId` is our own player id on the link.
    void masterChanged(int pid, int localId);
    // Returns true when this ended the mode (the synced deck left).
    bool deckLeft(int pid);
    // Call after DiffLock::onBeat(pid, ...): measures the rebase and finishes Aligning / Rebasing.
    void onBeat(int pid);

    Mode mode() const { return mode_; }
    Phase phase() const { return phase_; }
    bool on() const { return mode_ != Mode::Off; }
    int reference() const { return ref_; }               // the deck the sequencer follows now, 0 for none
    int selectedDeck() const { return mode_ == Mode::Deck ? deck_ : 0; }
    int master() const { return master_; }
    // The sequencer's BPM follows the reference's: engaged and neither held nor suspended.
    bool bpmFollowActive() const { return ref_ != 0 && (phase_ == Phase::Aligning || phase_ == Phase::Locked); }

    // After a rebase the sequencer's BPM may differ from the new reference's: it is brought there with glide(), one step per beat,
    // while glideActive(). The caller calls glideDone() when it has arrived.
    bool glideActive() const { return glide_; }
    void glideDone() { glide_ = false; }

    // One step of a BPM change towards `target` that moves no more than kGlideBpmPerBeat.
    static double glide(double current, double target, double maxStep = kGlideBpmPerBeat);

private:
    void resolve();                                      // work out the reference from the mode and the master, and move DiffLock
    void setReference(int ref);

    DiffLock& lock_;
    Mode mode_ = Mode::Off;
    Phase phase_ = Phase::Idle;
    int deck_ = 0;
    int master_ = 0;
    int localId_ = 0;
    int ref_ = 0;
    bool glide_ = false;
    bool engaged_ = false;           // the first reference of this mode has been reached: later changes rebase
};

} // namespace vdj

#endif // SYNC_CONTROLLER_H
