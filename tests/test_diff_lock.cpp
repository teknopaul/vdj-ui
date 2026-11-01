#include <QtTest/QtTest>
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>
#include <chrono>
#include "midi/alsa_clock.h"
#include "midi/diff_lock.h"
#include "ui/bpm_ramp_widget.h"
#include "ui/deck_area.h"
#include "ui/deck_controls.h"
#include "ui/deck_widget.h"
#include "ui/diff_lock_bar.h"
#include "ui/main_window.h"
#include "network/network_manager.h"

using namespace vdj;

// A beat packet as the network layer delivers it: the offset to the sequencer, then the raw numbers, then the arrival time.
static void deliverBeat(NetworkManager* net, int pid, double bpm, int barPos, double offsetMs)
{
    const qint64 nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    emit net->beatReceived(pid, bpm, barPos, offsetMs);
    emit net->beatTimed(pid, barPos, nowNs);
}

class TestDiffLock : public QObject
{
    Q_OBJECT

private slots:
    void noNudgeUntilMeasured();
    void nudgeIsAverageMinusTargetLimited();
    void onlyTheLockedDeckOnTheLastBeat();
    void noiseIsIgnored();
    void followMaster();
    void explicitLockStopsFollowingMaster();
    void offForgetsMeasurements();
    void convergesToTheTarget();
    void alsaNudgeLastsOneBeat();
    void panelAndSyncButtons();
    void nudgeButtonsMoveTheTargetWhileLocked();
    void deckBeatsNudgeTheSequencer();
    void syncIsAStickyModeAcrossMasterChanges();
    void bpmRampMovesTheClock();
    void calculatedBpmOptionToggles();
    void phaseShiftMovesThePositionExactly();
};

static int feedBar(DiffLock& d, int player, double diff)
{
    int nudge = 0;
    for (int beat = 1; beat <= 4; ++beat)
        nudge = d.onBeat(player, beat, diff);
    return nudge;                                 // what the bar's last beat asked for
}

void TestDiffLock::noNudgeUntilMeasured()
{
    DiffLock d;
    d.setTargetMs(20);
    QVERIFY(d.lock(1));
    QCOMPARE(d.onBeat(1, 1, 5.0), 0);
    QCOMPARE(d.onBeat(1, 2, 5.0), 0);
    QCOMPARE(d.onBeat(1, 3, 5.0), 0);
    QVERIFY(!d.hasAverage(1));
    QCOMPARE(d.onBeat(1, 4, 5.0), -15);           // 4 beats seen: average 5, target 20
    QVERIFY(d.hasAverage(1));
}

void TestDiffLock::nudgeIsAverageMinusTargetLimited()
{
    DiffLock d;
    d.setTargetMs(20);
    d.lock(2);
    QCOMPARE(feedBar(d, 2, 30.0), 10);            // 30 - 20
    DiffLock far;
    far.setTargetMs(20);
    far.lock(2);
    QCOMPARE(feedBar(far, 2, 200.0), DiffLock::kLimitMs);     // limited to +20
    DiffLock early;
    early.setTargetMs(0);
    early.lock(2);
    QCOMPARE(feedBar(early, 2, -200.0), -DiffLock::kLimitMs);
}

void TestDiffLock::onlyTheLockedDeckOnTheLastBeat()
{
    DiffLock d;
    d.setTargetMs(0);
    d.lock(1);
    for (int i = 0; i < 8; ++i)
        QCOMPARE(d.onBeat(2, 4, 100.0), 0);       // another deck: measured, never acted on
    QVERIFY(d.hasAverage(2));
    QCOMPARE(d.onBeat(1, 1, 50.0), 0);
    QCOMPARE(d.onBeat(1, 2, 50.0), 0);
    QCOMPARE(d.onBeat(1, 3, 50.0), 0);            // only bar position 4 nudges
    QCOMPARE(d.onBeat(1, 4, 50.0), 20);
    DiffLock unlocked;
    QCOMPARE(feedBar(unlocked, 1, 50.0), 0);      // not locked: no nudge
}

void TestDiffLock::noiseIsIgnored()
{
    DiffLock d;
    d.setTargetMs(0);
    d.lock(1);
    QCOMPARE(feedBar(d, 1, 10.0), 10);
    QCOMPARE(d.onBeat(1, 4, 400.0), 0);           // beyond +-250 ms: not a measurement
    QCOMPARE(d.lastDiff(1), 10.0);
}

void TestDiffLock::followMaster()
{
    DiffLock d;
    d.setFollowMaster(true);
    QVERIFY(!d.locked());                         // no master known yet
    d.masterChanged(2);
    QCOMPARE(d.player(), 2);
    d.masterChanged(3);
    QCOMPARE(d.player(), 3);                      // follows the change
    d.setFollowMaster(false);
    d.masterChanged(1);
    QCOMPARE(d.player(), 3);                      // no longer follows
    d.setFollowMaster(true);
    QCOMPARE(d.player(), 1);                      // switching it on locks to the current master at once
}

void TestDiffLock::explicitLockStopsFollowingMaster()
{
    DiffLock d;
    d.masterChanged(2);
    d.setFollowMaster(true);
    QVERIFY(d.lock(2));                           // the master itself: keeps following
    QVERIFY(d.followMaster());
    QVERIFY(d.lock(1));                           // adj: an explicit lock on another deck ends follow master
    QVERIFY(!d.followMaster());
    QVERIFY(!d.lock(0));
    QVERIFY(!d.lock(DiffLock::kMaxPlayers + 1));
}

void TestDiffLock::offForgetsMeasurements()
{
    DiffLock d;
    d.lock(1);
    feedBar(d, 1, 12.0);
    QVERIFY(d.hasAverage(1));
    d.off();
    QVERIFY(!d.locked());
    QVERIFY(!d.hasAverage(1));
    QCOMPARE(d.lastDiff(1), 0.0);
}

void TestDiffLock::convergesToTheTarget()
{
    // the sequencer starts 50 ms away from where it should be; every bar's nudge moves it by that much
    DiffLock d;
    d.setTargetMs(20);
    d.lock(1);
    double offset = 50.0;
    for (int bar = 0; bar < 8; ++bar)
        offset -= feedBar(d, 1, offset);          // +n delays our beat, which shrinks the offset by n
    QVERIFY2(std::abs(offset - 20.0) < 1.0, qPrintable(QString::number(offset)));

    DiffLock early;
    early.setTargetMs(20);
    early.lock(1);
    offset = -60.0;                               // we are late: nudges are negative
    for (int bar = 0; bar < 10; ++bar)
        offset -= feedBar(early, 1, offset);
    QVERIFY2(std::abs(offset - 20.0) < 1.0, qPrintable(QString::number(offset)));
}

void TestDiffLock::alsaNudgeLastsOneBeat()
{
    AlsaClock c;
    if (!c.open(QStringLiteral("vdj-ui-test-nudge"), nullptr))
        QSKIP("no ALSA sequencer available");
    QVERIFY(c.setBpm(120.0));                     // a beat is 500 ms
    c.nudgeMillis(20);                            // not running: ignored
    QVERIFY(std::abs(c.queueBpm() - 120.0) < 0.01);

    c.start();
    c.nudgeMillis(20);                            // 520 ms for one beat
    QVERIFY2(std::abs(c.queueBpm() - 60000.0 / 520.0) < 0.05, qPrintable(QString::number(c.queueBpm())));
    QTest::qWait(700);
    QVERIFY2(std::abs(c.queueBpm() - 120.0) < 0.01, qPrintable(QString::number(c.queueBpm())));   // restored

    c.nudgeMillis(-10);
    QVERIFY(c.queueBpm() > 120.0);
    QVERIFY(c.setBpm(120.0));                     // setting the tempo cancels a nudge in progress
    QTest::qWait(600);
    QVERIFY(std::abs(c.queueBpm() - 120.0) < 0.01);

    double ms = 0.0;
    QVERIFY(c.beatOffsetMs(&ms));                 // where we are in the beat
    QVERIFY(std::abs(ms) <= 250.0);
    c.stop();
    QVERIFY(!c.beatOffsetMs(&ms));
}

void TestDiffLock::panelAndSyncButtons()
{
    MainWindow window;
    window.show();
    window.addDemoDecks();
    QTest::qWait(20);
    auto* area = window.findChild<DeckArea*>();
    DeckWidget *vdjDeck = nullptr, *remote = nullptr;
    for (int i = 0; i < area->deckCount(); ++i) {
        if (area->deckAt(i)->playerId() == 5) vdjDeck = area->deckAt(i);
        if (area->deckAt(i)->playerId() == 1) remote = area->deckAt(i);
    }
    QVERIFY(vdjDeck && remote);

    // the panel is the VDJ's only; every deck has a visible Sync button
    QVERIFY(vdjDeck->diffLockBar()->isVisibleTo(vdjDeck));
    QVERIFY(!remote->diffLockBar()->isVisibleTo(remote));
    QVERIFY(remote->controls()->syncVisible());
    auto* bar = vdjDeck->diffLockBar();
    QCOMPARE(bar->targetMs(), 0);                 // default 0 ms
    QCOMPARE(bar->nudgeButtons().size(), 4);
    QStringList labels;
    for (auto* b : bar->nudgeButtons()) labels << b->text();
    QCOMPARE(labels, QStringList() << QStringLiteral("-10") << QStringLiteral("-1") << QStringLiteral("+1") << QStringLiteral("+10"));
    QCOMPARE(bar->status(), QStringLiteral("off"));

    bar->targetSpin()->setValue(35);
    QPushButton* sync = nullptr;
    for (auto* b : remote->findChildren<QPushButton*>())
        if (b->text() == QLatin1String("Sync")) sync = b;
    QVERIFY(sync);
    sync->click();                                // Sync on a remote deck: difflock to it
    QVERIFY2(bar->status().contains(QStringLiteral("deck 1")), qPrintable(bar->status()));
    QVERIFY(remote->controls()->syncLocked());
    QCOMPARE(sync->text(), QStringLiteral("Syncing..."));      // no beats have been measured yet: not "Locked"
    // the VDJ row shows the sync too, and its button stops it in one press
    QVERIFY(vdjDeck->controls()->syncState() == DeckControls::SyncState::Syncing);

    bar->offButton()->click();
    QCOMPARE(bar->status(), QStringLiteral("off"));
    QVERIFY(!remote->controls()->syncLocked());
    QCOMPARE(sync->text(), QStringLiteral("Sync"));

    bar->followMasterBox()->setChecked(true);     // no master known yet
    QVERIFY(bar->status().contains(QStringLiteral("waiting")));
    bar->followMasterBox()->setChecked(false);
    window.close();
}

void TestDiffLock::nudgeButtonsMoveTheTargetWhileLocked()
{
    MainWindow window;
    window.show();
    window.addDemoDecks();
    auto* clock = window.alsaClock();
    if (!clock || !clock->isOpen())
        QSKIP("no ALSA sequencer available");
    auto* area = window.findChild<DeckArea*>();
    DeckWidget *vdjDeck = nullptr, *remote = nullptr;
    for (int i = 0; i < area->deckCount(); ++i) {
        if (area->deckAt(i)->playerId() == 5) vdjDeck = area->deckAt(i);
        if (area->deckAt(i)->playerId() == 1) remote = area->deckAt(i);
    }
    auto* bar = vdjDeck->diffLockBar();
    clock->setBpm(120.0);
    clock->start();

    bar->nudgeButtons()[3]->click();              // +10 while not locked: the playhead goes forward (a shorter beat), the target is not nudged
    QVERIFY(clock->queueBpm() > 120.0);
    QCOMPARE(bar->targetMs(), 0);
    QTest::qWait(600);

    for (auto* b : remote->findChildren<QPushButton*>())
        if (b->text() == QLatin1String("Sync")) b->click();
    bar->nudgeButtons()[3]->click();              // locked: +10 delays us by 10, so the offset to keep drops by 10
    QCOMPARE(bar->targetMs(), -10);
    bar->nudgeButtons()[1]->click();              // -1
    QCOMPARE(bar->targetMs(), -9);
    clock->stop();
    window.close();
}

void TestDiffLock::phaseShiftMovesThePositionExactly()
{
    AlsaClock c;
    if (!c.open(QStringLiteral("vdj-ui-test-shift"), nullptr))
        QSKIP("no ALSA sequencer available");
    QVERIFY(c.setBpm(120.0));                     // a beat is 500 ms
    QVERIFY(!c.phaseShiftMs(50));                 // not running
    c.start();
    QVERIFY(!c.phaseShiftMs(400));                // beyond 0.75 of a beat
    for (int shift : { 120, -120 }) {
        int64_t t1 = 0, t2 = 0;
        const double p1 = c.positionBeatsExact(&t1);
        QVERIFY(c.phaseShiftMs(shift));
        QVERIFY(std::abs(c.bpm() - 120.0) < 1e-9);            // the tempo setting is untouched
        QTest::qWait(700);
        QVERIFY2(std::abs(c.queueBpm() - 120.0) < 0.01, qPrintable(QString::number(c.queueBpm())));   // restored
        const double p2 = c.positionBeatsExact(&t2);
        const double expected = double(t2 - t1) / 1e6 / 500.0 - shift / 500.0;
        QVERIFY2(std::abs((p2 - p1) - expected) < 0.01, qPrintable(QStringLiteral("%1 vs %2").arg(p2 - p1).arg(expected)));
    }
}

void TestDiffLock::calculatedBpmOptionToggles()
{
    MainWindow window;
    window.show();
    window.addDemoDecks();
    auto* area = window.findChild<DeckArea*>();
    DiffLockBar* bar = nullptr;
    for (int i = 0; i < area->deckCount(); ++i)
        if (area->deckAt(i)->playerId() == 5) bar = area->deckAt(i)->diffLockBar();
    QVERIFY(bar);
    const bool before = window.calculatedBpm();
    bar->calculatedBpmBox()->setChecked(!before);
    QCOMPARE(window.calculatedBpm(), !before);
    bar->calculatedBpmBox()->setChecked(before);           // leave the user's config as it was
    QCOMPARE(window.calculatedBpm(), before);
}

void TestDiffLock::bpmRampMovesTheClock()
{
    MainWindow window;
    window.show();
    window.addDemoDecks();
    auto* clock = window.alsaClock();
    if (!clock || !clock->isOpen())
        QSKIP("no ALSA sequencer available");
    auto* ramp = window.findChild<BpmRampWidget*>();
    QVERIFY(ramp);
    clock->setBpm(120.0);
    clock->start();
    emit ramp->rampRequested(130.0, 1);
    QTest::qWait(1200);
    QVERIFY2(clock->bpm() > 121.0, qPrintable(QString::number(clock->bpm())));
}

void TestDiffLock::deckBeatsNudgeTheSequencer()
{
    MainWindow window;
    window.show();
    window.addDemoDecks();
    auto* clock = window.alsaClock();
    auto* net = window.findChild<NetworkManager*>();
    if (!clock || !clock->isOpen())
        QSKIP("no ALSA sequencer available");
    QVERIFY(net);
    auto* area = window.findChild<DeckArea*>();
    DeckWidget *vdjDeck = nullptr, *remote = nullptr;
    for (int i = 0; i < area->deckCount(); ++i) {
        if (area->deckAt(i)->playerId() == 5) vdjDeck = area->deckAt(i);
        if (area->deckAt(i)->playerId() == 1) remote = area->deckAt(i);
    }
    clock->setBpm(120.0);
    clock->start();
    for (auto* b : remote->findChildren<QPushButton*>())
        if (b->text() == QLatin1String("Sync")) b->click();      // lock to deck 1, target 0 ms

    // the deck's beats arrive 50 ms after ours: 50 ms off. The first Sync press aligns: measure 4 beats, then slew
    // onto the beat in steps of at most 40 ms (here 25 + 25), a step per beat, instead of the difflock's 20 ms bar nudges.
    for (int beat = 1; beat <= 3; ++beat)
        deliverBeat(net, 1, 120.0, beat, 50.0);
    QVERIFY(std::abs(clock->queueBpm() - 120.0) < 0.01);         // measuring
    deliverBeat(net, 1, 120.0, 4, 50.0);
    const double step25 = 60000.0 / (500.0 + 500.0 * 25.0 / 475.0);    // a beat stretched so the position ends up 25 ms later
    QVERIFY2(std::abs(clock->queueBpm() - step25) < 0.05, qPrintable(QString::number(clock->queueBpm())));
    QTest::qWait(600);
    QVERIFY(std::abs(clock->queueBpm() - 120.0) < 0.01);
    deliverBeat(net, 1, 120.0, 1, 50.0);                          // second step
    QVERIFY2(std::abs(clock->queueBpm() - step25) < 0.05, qPrintable(QString::number(clock->queueBpm())));

    QTest::qWait(600);
    clock->stop();
    deliverBeat(net, 1, 120.0, 4, 50.0);                          // stopped sequencer: beats are ignored
    window.close();
}

QTEST_MAIN(TestDiffLock)
#include "test_diff_lock.moc"

void TestDiffLock::syncIsAStickyModeAcrossMasterChanges()
{
    MainWindow window;
    window.show();
    window.addDemoDecks();
    auto* clock = window.alsaClock();
    auto* net = window.findChild<NetworkManager*>();
    if (!clock || !clock->isOpen())
        QSKIP("no ALSA sequencer available");
    QVERIFY(net);
    auto* area = window.findChild<DeckArea*>();
    DeckWidget *vdjDeck = nullptr, *remote = nullptr;
    for (int i = 0; i < area->deckCount(); ++i) {
        if (area->deckAt(i)->playerId() == 5) vdjDeck = area->deckAt(i);
        if (area->deckAt(i)->playerId() == 1) remote = area->deckAt(i);
    }
    QVERIFY(vdjDeck && remote);
    using S = DeckControls::SyncState;
    auto* bar = vdjDeck->diffLockBar();
    clock->setBpm(120.0);
    clock->start();
    auto vdjSync = [&]() { for (auto* b : vdjDeck->controls()->findChildren<QPushButton*>()) if (b->toolTip().contains(QLatin1String("Sync"))) return b; return (QPushButton*)nullptr; };
    QPushButton* vdjButton = vdjSync();
    QVERIFY(vdjButton);

    // the VDJ row's Sync: on for "the master", but there is none yet
    vdjButton->click();
    QVERIFY(vdjDeck->controls()->syncState() == S::Waiting);
    QVERIFY(bar->status().contains(QStringLiteral("waiting")));
    QVERIFY(vdjDeck->controls()->bpmEditable());

    // deck 1 becomes the master: it is followed, and its BPM copied at once (first engagement)
    emit net->masterChanged(1);
    QVERIFY(remote->controls()->syncState() == S::Syncing);
    QVERIFY(vdjDeck->controls()->syncState() == S::Syncing);
    for (int bar4 = 0; bar4 < 2; ++bar4)
        for (int b = 1; b <= 4; ++b)
            deliverBeat(net, 1, 124.0, b, 0.0);
    QVERIFY(remote->controls()->syncState() == S::Locked);
    QVERIFY(vdjDeck->controls()->syncState() == S::Locked);
    QVERIFY2(std::abs(clock->bpm() - 124.0) < 0.01, qPrintable(QString::number(clock->bpm())));
    QVERIFY(!vdjDeck->controls()->bpmEditable());             // the BPM is the master's

    // we become the master: nothing is followed, the mode stays on, the BPM is ours again
    emit net->masterChanged(5);
    QVERIFY(vdjDeck->controls()->syncState() == S::Suspended);
    QVERIFY(remote->controls()->syncState() == S::Off);
    QVERIFY(vdjDeck->controls()->bpmEditable());
    const double ours = clock->bpm();
    deliverBeat(net, 1, 130.0, 1, 0.0);                       // deck 1 plays on at another tempo: not followed
    QVERIFY2(std::abs(clock->bpm() - ours) < 0.001, qPrintable(QStringLiteral("ours %1 now %2").arg(ours).arg(clock->bpm())));

    // deck 1 is master again: the position and the BPM are held while the new offset is measured, then the BPM glides
    emit net->masterChanged(1);
    QVERIFY(remote->controls()->syncState() == S::Syncing);
    QVERIFY(bar->status().contains(QStringLiteral("holding")));
    QVERIFY(std::abs(clock->bpm() - ours) < 0.001);
    for (int b = 1; b <= 3; ++b)
        deliverBeat(net, 1, 130.0, b, -42.0);                  // far from the old target: still no nudge
    QVERIFY(std::abs(clock->queueBpm() - 60000.0 / (60000.0 / ours)) < 0.05);
    deliverBeat(net, 1, 130.0, 4, -42.0);                      // the average is complete: the offset becomes the target
    QVERIFY(remote->controls()->syncState() == S::Locked);
    QCOMPARE(bar->targetMs(), -42);
    QVERIFY(std::abs(clock->bpm() - ours) < 0.2);              // at most one glide step so far (0.1 per beat), not a jump to 130
    const double before = clock->bpm();
    deliverBeat(net, 1, 130.0, 1, -42.0);
    QVERIFY(clock->bpm() > before && clock->bpm() - before < 0.11);

    // pressing Sync again stops everything
    vdjButton->click();
    QVERIFY(vdjDeck->controls()->syncState() == S::Off);
    QVERIFY(remote->controls()->syncState() == S::Off);
    QVERIFY(vdjDeck->controls()->bpmEditable());
    QCOMPARE(bar->status(), QStringLiteral("off"));
    clock->stop();
    window.close();
}
