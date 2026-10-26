#include <QtTest/QtTest>
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>
#include "midi/alsa_clock.h"
#include "midi/diff_lock.h"
#include "ui/deck_area.h"
#include "ui/deck_controls.h"
#include "ui/deck_widget.h"
#include "ui/diff_lock_bar.h"
#include "ui/main_window.h"
#include "network/network_manager.h"

using namespace vdj;

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
    QCOMPARE(sync->text(), QStringLiteral("Locked"));

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

    // the deck's beats arrive 50 ms after ours: 50 ms off, the limit is 20
    for (int beat = 1; beat <= 3; ++beat)
        emit net->beatReceived(1, 120.0, beat, 50.0);
    QVERIFY(std::abs(clock->queueBpm() - 120.0) < 0.01);         // nothing until the last beat of the bar
    emit net->beatReceived(1, 120.0, 4, 50.0);
    QVERIFY2(std::abs(clock->queueBpm() - 60000.0 / 520.0) < 0.05, qPrintable(QString::number(clock->queueBpm())));

    QTest::qWait(600);
    clock->stop();
    emit net->beatReceived(1, 120.0, 4, 50.0);                    // stopped sequencer: beats are ignored
    window.close();
}

QTEST_MAIN(TestDiffLock)
#include "test_diff_lock.moc"
