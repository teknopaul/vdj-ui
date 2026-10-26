#include <QtTest/QtTest>
#include "audio/beat_clock.h"

using vdj::BeatClock;

class TestBeatClock : public QObject
{
    Q_OBJECT

private:
    static BeatClock grid(int count, double len, double first = 0)
    {
        QVector<double> ms;
        QVector<quint8> nums;
        for (int i = 0; i < count; ++i) {
            ms.append(first + i * len);
            nums.append(quint8(i % 4 + 1));
        }
        BeatClock c;
        c.setBeats(ms, nums);
        return c;
    }

private slots:
    void onBeat();
    void after40();
    void before10();
    void halfwayTie();
    void beforeFirst();
    void afterLast();
    void twoBeatsOnly();
    void emptyAndSingle();
    void tempoChange();
    void manyBeats();
    void subdivisions();
    void nearest();
    void planLate();
    void planWait();
    void planOffAndClamp();
    void planResolutions();
    void beatJump();
};

void TestBeatClock::onBeat()
{
    const auto p = grid(8, 500).locate(1000);
    QVERIFY(p.valid);
    QCOMPARE(p.index, 2);
    QCOMPARE(p.sinceBeatMs, 0.0);
    QCOMPARE(p.toNextBeatMs, 500.0);
    QCOMPARE(p.beatNumber, 3);
}

void TestBeatClock::after40()
{
    const auto p = grid(8, 500).locate(1040);
    QCOMPARE(p.index, 2);
    QCOMPARE(p.sinceBeatMs, 40.0);
    QCOMPARE(p.toNextBeatMs, 460.0);
    QCOMPARE(p.beatLenMs, 500.0);
}

void TestBeatClock::before10()
{
    const auto p = grid(8, 500).locate(1490);
    QCOMPARE(p.index, 2);
    QCOMPARE(p.sinceBeatMs, 490.0);
    QCOMPARE(p.toNextBeatMs, 10.0);
}

void TestBeatClock::halfwayTie()
{
    const auto p = grid(8, 500).locate(1250);
    QCOMPARE(p.sinceBeatMs, 250.0);
    QCOMPARE(p.toNextBeatMs, 250.0);
    QVERIFY(p.sinceBeatMs <= p.toNextBeatMs);   // tie counts as late (after the beat)
    QCOMPARE(grid(8, 500).nearestBeatMs(1250), 1000.0);
}

void TestBeatClock::beforeFirst()
{
    const BeatClock c = grid(8, 500, 1000);
    auto p = c.locate(900);
    QVERIFY(p.valid);
    QCOMPARE(p.index, -1);
    QCOMPARE(p.sinceBeatMs, 400.0);
    QCOMPARE(p.toNextBeatMs, 100.0);
    QCOMPARE(p.beatNumber, 4);
    p = c.locate(0);
    QCOMPARE(p.index, -2);
    QCOMPARE(p.sinceBeatMs, 0.0);
    QCOMPARE(p.beatNumber, 3);
}

void TestBeatClock::afterLast()
{
    const BeatClock c = grid(4, 500);   // last beat at 1500, number 4
    auto p = c.locate(1700);
    QCOMPARE(p.index, 3);
    QCOMPARE(p.sinceBeatMs, 200.0);
    p = c.locate(2100);
    QCOMPARE(p.index, 4);
    QCOMPARE(p.sinceBeatMs, 100.0);
    QCOMPARE(p.beatNumber, 1);
}

void TestBeatClock::twoBeatsOnly()
{
    const BeatClock c = grid(2, 400);
    QVERIFY(c.isValid());
    auto p = c.locate(500);
    QCOMPARE(p.sinceBeatMs, 100.0);
    QCOMPARE(p.beatLenMs, 400.0);
}

void TestBeatClock::emptyAndSingle()
{
    BeatClock c;
    QVERIFY(!c.isValid());
    QVERIFY(!c.locate(100).valid);
    QCOMPARE(c.nearestBeatMs(100), 100.0);
    c = grid(1, 500);
    QVERIFY(!c.isValid());
    QVERIFY(!c.locate(100).valid);
    c = grid(8, 500);
    c.clear();
    QVERIFY(!c.isValid());
}

void TestBeatClock::tempoChange()
{
    QVector<double> ms{0, 500, 1000, 1400, 1800, 2200};
    BeatClock c;
    c.setBeats(ms, QVector<quint8>());
    auto p = c.locate(1100);
    QCOMPARE(p.index, 2);
    QCOMPARE(p.sinceBeatMs, 100.0);
    QCOMPARE(p.beatLenMs, 400.0);
    QCOMPARE(p.toNextBeatMs, 300.0);
    p = c.locate(750);
    QCOMPARE(p.beatLenMs, 500.0);
    QCOMPARE(p.beatNumber, 2);   // no numbers given: index % 4 + 1
}

void TestBeatClock::manyBeats()
{
    const BeatClock c = grid(5000, 480);
    for (int i : {0, 1, 2499, 4998}) {
        const auto p = c.locate(i * 480.0 + 37);
        QCOMPARE(p.index, i);
        QCOMPARE(p.sinceBeatMs, 37.0);
        QCOMPARE(p.beatNumber, i % 4 + 1);
    }
}

void TestBeatClock::subdivisions()
{
    const BeatClock c = grid(8, 500);
    auto p = c.locate(1130, 4);
    QCOMPARE(p.index, 2);
    QCOMPARE(p.beatLenMs, 125.0);
    QCOMPARE(p.sinceBeatMs, 5.0);
    QCOMPARE(p.toNextBeatMs, 120.0);
    p = c.locate(1230, 2);
    QCOMPARE(p.beatLenMs, 250.0);
    QCOMPARE(p.sinceBeatMs, 230.0);
    QCOMPARE(p.toNextBeatMs, 20.0);
    QCOMPARE(c.locate(1100, 4).toNextBeatMs, 25.0);
    QCOMPARE(c.locate(1100, 2).sinceBeatMs, 100.0);
}

void TestBeatClock::nearest()
{
    const BeatClock c = grid(8, 500);
    QCOMPARE(c.nearestBeatMs(1040), 1000.0);
    QCOMPARE(c.nearestBeatMs(1480), 1500.0);
    QCOMPARE(c.nearestBeatMs(1230, 2), 1250.0);
}

void TestBeatClock::planLate()
{
    const auto j = vdj::planQuantizedJump(grid(40, 500), 1, 1040, 10000, 20000);
    QVERIFY(!j.wait);
    QCOMPARE(j.targetMs, 10040.0);
    const auto onBeat = vdj::planQuantizedJump(grid(40, 500), 1, 1000, 10000, 20000);
    QVERIFY(!onBeat.wait);
    QCOMPARE(onBeat.targetMs, 10000.0);
}

void TestBeatClock::planWait()
{
    const auto j = vdj::planQuantizedJump(grid(40, 500), 1, 1480, 10000, 20000);
    QVERIFY(j.wait);
    QCOMPARE(j.triggerMs, 1500.0);
    QCOMPARE(j.targetMs, 10000.0);
}

void TestBeatClock::planOffAndClamp()
{
    const auto none = vdj::planQuantizedJump(BeatClock(), 1, 1480, 10000, 20000);
    QVERIFY(!none.wait);
    QCOMPARE(none.targetMs, 10000.0);
    const auto clamped = vdj::planQuantizedJump(grid(40, 500), 1, 1200, 19900, 20000);
    QCOMPARE(clamped.targetMs, 20000.0);
}

void TestBeatClock::planResolutions()
{
    const BeatClock c = grid(40, 500);
    auto j = vdj::planQuantizedJump(c, 2, 1040, 10000, 20000);
    QVERIFY(!j.wait);
    QCOMPARE(j.targetMs, 10040.0);
    j = vdj::planQuantizedJump(c, 2, 1230, 10000, 20000);
    QVERIFY(j.wait);
    QCOMPARE(j.triggerMs, 1250.0);
    j = vdj::planQuantizedJump(c, 2, 1100, 10000, 20000);
    QVERIFY(!j.wait);
    QCOMPARE(j.targetMs, 10100.0);
    j = vdj::planQuantizedJump(c, 4, 1100, 10000, 20000);
    QVERIFY(j.wait);
    QCOMPARE(j.triggerMs, 1125.0);
    // a trigger is in source ms whatever the tempo: the grid never sees the tempo
    j = vdj::planQuantizedJump(c, 1, 1480, 10000, 20000);
    QCOMPARE(j.triggerMs, 1500.0);
}

void TestBeatClock::beatJump()
{
    QCOMPARE(vdj::beatJumpMs(120, 1), 500.0);
    QCOMPARE(vdj::beatJumpMs(120, 4), 2000.0);
    QCOMPARE(vdj::beatJumpMs(120, -4), -2000.0);
    QCOMPARE(vdj::beatJumpMs(0, 1), 0.0);
}

QTEST_APPLESS_MAIN(TestBeatClock)
#include "test_beat_clock.moc"
