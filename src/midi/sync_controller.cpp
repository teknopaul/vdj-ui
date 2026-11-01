#include "sync_controller.h"
#include "diff_lock.h"
#include <algorithm>
#include <cmath>

namespace vdj {

void SyncController::press(int pid, bool isLocalRow)
{
    if (isLocalRow) {
        if (mode_ == Mode::Master) {
            off();
            return;
        }
        mode_ = Mode::Master;
        deck_ = 0;
    } else {
        if (pid < 1)
            return;
        if (mode_ == Mode::Deck && deck_ == pid) {
            off();
            return;
        }
        mode_ = Mode::Deck;
        deck_ = pid;
    }
    resolve();
}

void SyncController::off()
{
    mode_ = Mode::Off;
    deck_ = 0;
    engaged_ = false;
    lock_.setHold(false);
    lock_.off();
    ref_ = 0;
    glide_ = false;
    phase_ = Phase::Idle;
}

void SyncController::masterChanged(int pid, int localId)
{
    master_ = pid > 0 ? pid : 0;
    localId_ = localId;
    if (mode_ != Mode::Off)
        resolve();                      // a chosen deck is followed too, except while we are the master ourselves
}

bool SyncController::deckLeft(int pid)
{
    const bool wasMaster = master_ == pid;
    if (wasMaster)
        master_ = 0;
    if (mode_ == Mode::Deck && deck_ == pid) {
        off();
        return true;
    }
    if (mode_ == Mode::Master && (wasMaster || ref_ == pid))
        resolve();
    return false;
}

void SyncController::resolve()
{
    int ref = 0;
    Phase idle = Phase::Idle;
    if (mode_ == Mode::Deck) {
        if (master_ != 0 && master_ == localId_)
            idle = Phase::Suspended;     // we are the master: nobody to follow, the chosen deck again when we stop being it
        else
            ref = deck_;
    } else if (mode_ == Mode::Master) {
        if (master_ == 0)
            ref = 0;
        else if (master_ == localId_) {
            ref = 0;
            idle = Phase::Suspended;
        } else
            ref = master_;
    }
    if (ref == 0) {
        if (ref_ != 0 || phase_ != idle) {
            lock_.setHold(false);
            lock_.suspend();                 // the target and the follow state stay
            ref_ = 0;
            glide_ = false;
            phase_ = idle;
        }
        return;
    }
    if (ref == ref_)
        return;
    setReference(ref);
}

void SyncController::setReference(int ref)
{
    ref_ = ref;
    glide_ = false;
    if (!engaged_) {
        // first engagement: the DJ's target stands and DiffLock brings the sequencer to it
        engaged_ = true;
        lock_.setHold(false);
        lock_.lock(ref);
        phase_ = Phase::Aligning;
    } else {
        // moving to another reference: hold everything, measure where we are
        lock_.rebase(ref);
        phase_ = Phase::Rebasing;
    }
}

void SyncController::onBeat(int pid)
{
    if (pid != ref_ || ref_ == 0 || !lock_.hasAverage(pid))
        return;
    if (phase_ == Phase::Rebasing) {
        lock_.setTargetMs(int(std::lround(lock_.avgDiff(pid))));   // the sequencer stays where it is: that is the new alignment
        lock_.setHold(false);
        phase_ = Phase::Locked;
        glide_ = true;                                              // the BPM may differ from the new reference's: no step
    } else if (phase_ == Phase::Aligning) {
        if (!lock_.holding() && std::fabs(lock_.avgDiff(pid) - lock_.targetMs()) < DiffLock::kLimitMs)
            phase_ = Phase::Locked;
    }
}

double SyncController::glide(double current, double target, double maxStep)
{
    return current + std::clamp(target - current, -maxStep, maxStep);
}

} // namespace vdj
