#include <QtTest>
#include <cmath>
#include "midi/diff_lock.h"
#include "midi/sync_controller.h"

using namespace vdj;
using Mode = SyncController::Mode;
using Phase = SyncController::Phase;

class TestSyncController : public QObject {
    Q_OBJECT

    static constexpr int kLocal = 5;

    // one bar of beats from `pid` at a fixed offset; the controller sees each beat. Returns the sum of the nudges DiffLock asked for.
    static int feedBar(DiffLock& lock, SyncController& sc, int pid, double offsetMs)
    {
        int sum = 0;
        for (int bar = 1; bar <= 4; ++bar) {
            sum += lock.onBeat(pid, bar, offsetMs);
            sc.onBeat(pid);
        }
        return sum;
    }

private slots:
    void aChosenDeckIsSuspendedWhileWeAreMasterAndResumed()
    {
        DiffLock lock;
        SyncController sc(lock);
        sc.masterChanged(1, kLocal);
        sc.press(1, false);
        QCOMPARE(sc.reference(), 1);
        QVERIFY(sc.bpmFollowActive());
        sc.masterChanged(kLocal, kLocal);          // we take master: the BPM is ours (ramps, sliders), Sync stays on
        QVERIFY(sc.on());
        QCOMPARE(sc.phase(), Phase::Suspended);
        QCOMPARE(sc.reference(), 0);
        QVERIFY(!sc.bpmFollowActive());
        sc.masterChanged(2, kLocal);                // someone else takes it: the chosen deck is followed again
        QCOMPARE(sc.reference(), 1);
        QCOMPARE(sc.selectedDeck(), 1);
    }

    void startsOff()
    {
        DiffLock lock;
        SyncController sc(lock);
        QCOMPARE(sc.mode(), Mode::Off);
        QCOMPARE(sc.phase(), Phase::Idle);
        QVERIFY(!sc.on());
        QCOMPARE(sc.reference(), 0);
        QVERIFY(!sc.bpmFollowActive());
    }

    void pressingTheSameDeckAgainTurnsSyncOff()
    {
        DiffLock lock;
        SyncController sc(lock);
        sc.press(1, false);
        QCOMPARE(sc.mode(), Mode::Deck);
        QCOMPARE(sc.reference(), 1);
        QCOMPARE(sc.phase(), Phase::Aligning);
        QVERIFY(sc.bpmFollowActive());
        QVERIFY(lock.locked() && lock.player() == 1);
        sc.press(1, false);
        QCOMPARE(sc.mode(), Mode::Off);
        QCOMPARE(sc.reference(), 0);
        QVERIFY(!lock.locked());
        QVERIFY(!sc.bpmFollowActive());
    }

    void anotherDeckRetargetsAndRebases()
    {
        DiffLock lock;
        SyncController sc(lock);
        lock.setTargetMs(7);
        sc.press(1, false);
        sc.press(2, false);
        QCOMPARE(sc.reference(), 2);
        QCOMPARE(sc.phase(), Phase::Rebasing);
        QVERIFY(lock.holding());
        QVERIFY(!sc.bpmFollowActive());                      // the BPM is held
        // while rebasing nothing is nudged, however far off the new deck is
        QCOMPARE(feedBar(lock, sc, 2, -60.0) , 0 + lock.limit(0));   // the bar of 4 beats is measured; the last beat completes the average
        QCOMPARE(sc.phase(), Phase::Locked);
        QVERIFY(!lock.holding());
        QCOMPARE(lock.targetMs(), -60);                      // where the sequencer is *is* the alignment now
        QVERIFY(sc.glideActive());                           // the BPM is brought to the new deck's in steps
        sc.glideDone();
        QVERIFY(!sc.glideActive());
        QVERIFY(sc.bpmFollowActive());
        // and now it is following: no nudge, the offset equals the target
        QCOMPARE(feedBar(lock, sc, 2, -60.0), 0);
    }

    void rebasingNeverNudges()
    {
        DiffLock lock;
        SyncController sc(lock);
        sc.press(1, false);
        sc.press(2, false);
        for (int bar = 0; bar < 3; ++bar) {                  // 3 beats: not measured yet
            QCOMPARE(lock.onBeat(2, bar % 4 + 1, 90.0), 0);
            sc.onBeat(2);
            QCOMPARE(sc.phase(), Phase::Rebasing);
        }
    }

    void masterButtonWaitsForAMaster()
    {
        DiffLock lock;
        SyncController sc(lock);
        sc.press(kLocal, true);
        QCOMPARE(sc.mode(), Mode::Master);
        QCOMPARE(sc.reference(), 0);
        QCOMPARE(sc.phase(), Phase::Idle);
        QVERIFY(sc.on());
        QVERIFY(!sc.bpmFollowActive());
        sc.masterChanged(3, kLocal);
        QCOMPARE(sc.reference(), 3);
        QCOMPARE(sc.phase(), Phase::Aligning);               // first engagement keeps the DJ's target
        sc.press(kLocal, true);
        QCOMPARE(sc.mode(), Mode::Off);
    }

    void becomingMasterSuspendsAndTheModeStaysOn()
    {
        DiffLock lock;
        SyncController sc(lock);
        lock.setTargetMs(12);
        sc.masterChanged(3, kLocal);
        sc.press(kLocal, true);
        QCOMPARE(sc.reference(), 3);
        sc.masterChanged(kLocal, kLocal);                    // we are the master now
        QCOMPARE(sc.mode(), Mode::Master);
        QVERIFY(sc.on());
        QCOMPARE(sc.phase(), Phase::Suspended);
        QCOMPARE(sc.reference(), 0);
        QVERIFY(!sc.bpmFollowActive());                      // we are in charge of the BPM
        QVERIFY(!lock.locked());
        QCOMPARE(lock.targetMs(), 12);                       // the target survives
        // and a new master is followed, from where we are
        sc.masterChanged(4, kLocal);
        QCOMPARE(sc.reference(), 4);
        QCOMPARE(sc.phase(), Phase::Rebasing);               // we had followed deck 3 before: nothing may move, the offset is measured
        QVERIFY(lock.holding());
        QVERIFY(!sc.bpmFollowActive());
    }

    void syncPressedWhileWeAreMasterAlignsOnTheFirstMaster()
    {
        DiffLock lock;
        SyncController sc(lock);
        sc.masterChanged(kLocal, kLocal);
        sc.press(kLocal, true);
        QCOMPARE(sc.phase(), Phase::Suspended);
        QVERIFY(sc.on());
        sc.masterChanged(4, kLocal);                         // never followed anyone yet: the DJ's target applies (first engagement)
        QCOMPARE(sc.phase(), Phase::Aligning);
    }

    void aNewMasterWhileLockedRebases()
    {
        DiffLock lock;
        SyncController sc(lock);
        sc.masterChanged(3, kLocal);
        sc.press(kLocal, true);
        for (int i = 0; i < 2; ++i) feedBar(lock, sc, 3, 4.0);
        QCOMPARE(sc.phase(), Phase::Locked);
        sc.masterChanged(4, kLocal);
        QCOMPARE(sc.reference(), 4);
        QCOMPARE(sc.phase(), Phase::Rebasing);
        QVERIFY(lock.holding() && lock.player() == 4);
        QVERIFY(!sc.bpmFollowActive());
        QCOMPARE(feedBar(lock, sc, 4, -33.0), 0);
        QCOMPARE(sc.phase(), Phase::Locked);
        QCOMPARE(lock.targetMs(), -33);
    }

    void aDeckThatLeaves()
    {
        DiffLock lock;
        SyncController sc(lock);
        sc.press(2, false);
        QVERIFY(!sc.deckLeft(7));                            // some other deck
        QVERIFY(sc.on());
        QVERIFY(sc.deckLeft(2));                             // the synced deck: the mode ends
        QCOMPARE(sc.mode(), Mode::Off);
        QVERIFY(!lock.locked());

        sc.masterChanged(3, kLocal);
        sc.press(kLocal, true);
        QCOMPARE(sc.reference(), 3);
        sc.masterChanged(-1, kLocal);                        // "no master" from the link
        QCOMPARE(sc.master(), 0);
        sc.masterChanged(3, kLocal);
        QVERIFY(!sc.deckLeft(3));                            // the master left: Sync stays on and waits
        QVERIFY(sc.on());
        QCOMPARE(sc.reference(), 0);
        QCOMPARE(sc.phase(), Phase::Idle);
        sc.masterChanged(4, kLocal);
        QCOMPARE(sc.reference(), 4);
    }

    void deckButtonWhileMasterModeMovesTheMode()
    {
        DiffLock lock;
        SyncController sc(lock);
        sc.masterChanged(3, kLocal);
        sc.press(kLocal, true);
        sc.press(2, false);
        QCOMPARE(sc.mode(), Mode::Deck);
        QCOMPARE(sc.selectedDeck(), 2);
        QCOMPARE(sc.reference(), 2);
        sc.masterChanged(4, kLocal);                         // a master change does not move a deck selection
        QCOMPARE(sc.reference(), 2);
        sc.press(kLocal, true);                              // and the VDJ button goes back to the master
        QCOMPARE(sc.mode(), Mode::Master);
        QCOMPARE(sc.reference(), 4);
    }

    void firstEngagementConvergesToTheDjsTarget()
    {
        DiffLock lock;
        SyncController sc(lock);
        lock.setTargetMs(10);
        sc.press(1, false);
        // 30 ms off the target: Aligning nudges (limited), not Locked
        for (int i = 0; i < 4; ++i) lock.onBeat(1, i % 4 + 1, 40.0);
        sc.onBeat(1);
        QCOMPARE(sc.phase(), Phase::Aligning);
        QCOMPARE(lock.onBeat(1, 4, 40.0), DiffLock::kLimitMs);
        // within one nudge of the target: Locked
        DiffLock lock2;
        SyncController sc2(lock2);
        lock2.setTargetMs(10);
        sc2.press(1, false);
        feedBar(lock2, sc2, 1, 15.0);
        QCOMPARE(sc2.phase(), Phase::Locked);
        QVERIFY(!sc2.glideActive());                         // the first engagement copies the BPM at once
        QCOMPARE(lock2.targetMs(), 10);                      // the target was not touched
    }

    void glideMovesInBoundedSteps()
    {
        QCOMPARE(SyncController::glide(120.0, 120.05), 120.05);
        QVERIFY(std::fabs(SyncController::glide(120.0, 125.0) - 120.1) < 1e-12);
        QVERIFY(std::fabs(SyncController::glide(120.0, 110.0) - 119.9) < 1e-12);
        double bpm = 120.0;
        int beats = 0;
        while (std::fabs(bpm - 121.0) > 1e-9 && beats < 100) { bpm = SyncController::glide(bpm, 121.0); ++beats; }
        QCOMPARE(beats, 10);
    }

    void diffLockBehaviourIsUnchangedWithoutAController()
    {
        DiffLock lock;
        lock.lock(1);
        for (int i = 0; i < 3; ++i) QCOMPARE(lock.onBeat(1, i + 1, 12.0), 0);
        QCOMPARE(lock.onBeat(1, 4, 12.0), 12);               // average 12 - target 0, as before
        lock.setHold(true);
        QCOMPARE(lock.onBeat(1, 4, 12.0), 0);                // held: no nudge
        lock.setHold(false);
        lock.suspend();
        QVERIFY(!lock.locked());
        QCOMPARE(lock.onBeat(1, 4, 12.0), 0);
    }
};

QTEST_APPLESS_MAIN(TestSyncController)
#include "test_sync_controller.moc"
