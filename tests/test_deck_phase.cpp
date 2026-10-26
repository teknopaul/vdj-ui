#include <QtTest/QtTest>
#include "audio/deck_phase.h"

using vdj::DeckPhase;

class TestDeckPhase : public QObject
{
    Q_OBJECT

private slots:
    void nextBeatAndDownbeat();
    void grid();
    void onTheBeat();
    void barWraps();
    void staleAndUnknown();
    void newestPacketWins();
    void forgetDeck();
    void restartWait();
    void playWait();
};

static DeckPhase deck3At0()
{
    DeckPhase p;
    p.setBpm(1, 120.0);          // 500 ms beats
    p.onBeat(1, 0.0, 3);         // beat 3 of the bar just happened
    return p;
}

void TestDeckPhase::nextBeatAndDownbeat()
{
    const DeckPhase p = deck3At0();
    QVERIFY(std::abs(p.msToNextBeat(1, 100.0) - 400.0) < 1e-6);
    // beat 4 at 500, beat 1 at 1000
    QVERIFY(std::abs(p.msToNextDownbeat(1, 100.0) - 900.0) < 1e-6);
}

void TestDeckPhase::grid()
{
    const DeckPhase p = deck3At0();
    QVERIFY(std::abs(p.msToNextGrid(1, 100.0, 2) - 150.0) < 1e-6);
    QVERIFY(std::abs(p.msToNextGrid(1, 100.0, 4) - 25.0) < 1e-6);
    QVERIFY(std::abs(p.msToNextGrid(1, 100.0, 1) - 400.0) < 1e-6);
}

void TestDeckPhase::onTheBeat()
{
    DeckPhase p;
    p.setBpm(2, 120.0);
    p.onBeat(2, 1000.0, 1);      // a downbeat right now
    QCOMPARE(p.msToNextBeat(2, 1000.0), 0.0);
    QCOMPARE(p.msToNextDownbeat(2, 1000.0), 0.0);
    // a bit later the next downbeat is a whole bar (less the elapsed time) away
    QVERIFY(std::abs(p.msToNextDownbeat(2, 1010.0) - 1990.0) < 1e-6);
}

void TestDeckPhase::barWraps()
{
    DeckPhase p;
    p.setBpm(1, 120.0);
    p.onBeat(1, 0.0, 4);         // beat 4 just happened: the next beat is the downbeat
    QVERIFY(std::abs(p.msToNextDownbeat(1, 100.0) - 400.0) < 1e-6);
    p.onBeat(1, 500.0, 1);
    QVERIFY(std::abs(p.msToNextDownbeat(1, 600.0) - 1900.0) < 1e-6);
}

void TestDeckPhase::staleAndUnknown()
{
    const DeckPhase p = deck3At0();
    QVERIFY(p.msToNextBeat(1, 1000.0) >= 0.0);          // exactly 2 beats: still usable
    QCOMPARE(p.msToNextBeat(1, 1100.0), -1.0);          // older than 2 beats
    QCOMPARE(p.msToNextDownbeat(1, 1100.0), -1.0);
    QCOMPARE(p.msToNextGrid(1, 1100.0, 2), -1.0);
    QCOMPARE(p.msToNextBeat(9, 0.0), -1.0);             // unknown deck
    DeckPhase q;
    q.onBeat(1, 0.0, 1);                                // a beat but no BPM yet
    QCOMPARE(q.msToNextBeat(1, 10.0), -1.0);
}

void TestDeckPhase::newestPacketWins()
{
    DeckPhase p = deck3At0();
    p.setBpm(1, 150.0);          // 400 ms beats
    p.onBeat(1, 2000.0, 2);
    QVERIFY(std::abs(p.msToNextBeat(1, 2100.0) - 300.0) < 1e-6);
    QVERIFY(std::abs(p.msToNextDownbeat(1, 2100.0) - 1100.0) < 1e-6);   // 1.25 beats into the bar: 2.75 beats * 400 ms
}

void TestDeckPhase::forgetDeck()
{
    DeckPhase p = deck3At0();
    p.forget(1);
    QCOMPARE(p.msToNextBeat(1, 10.0), -1.0);
}

void TestDeckPhase::restartWait()
{
    const DeckPhase p = deck3At0();
    bool fb = true;
    // synced to deck 1: its next downbeat (900 ms at t=100), whatever our own clock says
    QVERIFY(std::abs(vdj::restartWaitMs(1, p, 100.0, 7.25, 120.0, &fb) - 900.0) < 1e-6);
    QVERIFY(!fb);
    // we are the master (no reference deck): our own next whole beat. 7.25 -> 0.75 beats at 120 BPM = 375 ms
    QVERIFY(std::abs(vdj::restartWaitMs(0, p, 100.0, 7.25, 120.0, &fb) - 375.0) < 1e-6);
    QVERIFY(!fb);
    QCOMPARE(vdj::restartWaitMs(0, p, 100.0, 8.0, 120.0), 0.0);          // exactly on a beat: now
    // the sync deck is stale: fall back to our own beat and say so
    QVERIFY(std::abs(vdj::restartWaitMs(1, p, 5000.0, 7.25, 120.0, &fb) - 375.0) < 1e-6);
    QVERIFY(fb);
    // an unknown deck too
    QVERIFY(std::abs(vdj::restartWaitMs(9, p, 100.0, 7.5, 120.0, &fb) - 250.0) < 1e-6);
    QVERIFY(fb);
}

void TestDeckPhase::playWait()
{
    const DeckPhase p = deck3At0();
    bool stale = true;
    QCOMPARE(vdj::playWaitMs(false, 1, p, 100.0, 1, &stale), 0.0);               // Q off
    QVERIFY(!stale);
    QCOMPARE(vdj::playWaitMs(true, 0, p, 100.0, 1, &stale), 0.0);                // no sync deck: immediate, silently
    QVERIFY(!stale);
    QVERIFY(std::abs(vdj::playWaitMs(true, 1, p, 100.0, 1, &stale) - 400.0) < 1e-6);
    QVERIFY(std::abs(vdj::playWaitMs(true, 1, p, 100.0, 2, &stale) - 150.0) < 1e-6);   // the global resolution is used
    QVERIFY(std::abs(vdj::playWaitMs(true, 1, p, 100.0, 4, &stale) - 25.0) < 1e-6);
    QCOMPARE(vdj::playWaitMs(true, 1, p, 5000.0, 1, &stale), 0.0);               // stale deck: start now, flagged
    QVERIFY(stale);
}

QTEST_APPLESS_MAIN(TestDeckPhase)
#include "test_deck_phase.moc"
