#include <QtTest/QtTest>
#include "midi/master_beat_math.h"
#include "midi/seq_bar.h"

using namespace vdj;

class TestMasterBeatMath : public QObject
{
    Q_OBJECT

private slots:
    void echoTick();
    void realNsOffsets();
    void realNsLaterBeat();
    void clamp();
    void invalid();
    void packRoundTrip();
    void barPosWithShift();
};

void TestMasterBeatMath::echoTick()
{
    QCOMPARE(beatEchoTick(0), int64_t(-48));
    QCOMPARE(beatEchoTick(1), int64_t(48));
    QCOMPARE(beatEchoTick(100), int64_t(9552));
}

void TestMasterBeatMath::realNsOffsets()
{
    int64_t ns = 0;
    // 120 BPM: 500 ms per beat; position 3.75, so beat 4 is 125 ms away
    QVERIFY(sendEchoRealNs(3.75, 1000000000, 120.0, 0, 4, &ns));
    QCOMPARE(ns, int64_t(1125000000));
    QVERIFY(sendEchoRealNs(3.75, 1000000000, 120.0, 40, 4, &ns));
    QCOMPARE(ns, int64_t(1165000000));
    QVERIFY(sendEchoRealNs(3.75, 1000000000, 120.0, -40, 4, &ns));
    QCOMPARE(ns, int64_t(1085000000));
}

void TestMasterBeatMath::realNsLaterBeat()
{
    int64_t ns = 0;
    QVERIFY(sendEchoRealNs(3.5, 0, 120.0, 0, 5, &ns));   // 1.5 beats of 500 ms
    QCOMPARE(ns, int64_t(750000000));
}

void TestMasterBeatMath::clamp()
{
    QCOMPARE(clampAsMasterOffsetMs(100, 120.0), 100);
    QCOMPARE(clampAsMasterOffsetMs(500, 120.0), 225);     // 0.45 * 500 ms
    QCOMPARE(clampAsMasterOffsetMs(-500, 120.0), -225);
    QCOMPARE(clampAsMasterOffsetMs(500, 60.0), 450);
    QCOMPARE(clampAsMasterOffsetMs(500, 300.0), 90);      // 0.45 * 200 ms
    QCOMPARE(clampAsMasterOffsetMs(-500, 20.0), -500);    // 0.45 * 3000 ms = 1350
    QCOMPARE(clampAsMasterOffsetMs(50, 0.0), 0);
}

void TestMasterBeatMath::invalid()
{
    int64_t ns = 0;
    QVERIFY(!sendEchoRealNs(1.0, 0, 0.0, 0, 2, &ns));
    QVERIFY(!sendEchoRealNs(1.0, 0, -5.0, 0, 2, &ns));
    QVERIFY(!sendEchoRealNs(-1.0, 0, 120.0, 0, 2, &ns));
    QVERIFY(!sendEchoRealNs(1.0, 0, 120.0, 0, 2, nullptr));
}

void TestMasterBeatMath::packRoundTrip()
{
    const int64_t beats[] = { 0, 1, 7, 100, 2147483647LL, 2147483648LL, 5000000000LL, (1LL << 61) + 12345 };
    for (int64_t b : beats) {
        for (int bar = 1; bar <= 4; ++bar) {
            uint32_t w[2];
            packBeatEcho(b, bar, w);
            int64_t b2 = -1;
            int bar2 = 0;
            unpackBeatEcho(w, &b2, &bar2);
            QCOMPARE(b2, b);
            QCOMPARE(bar2, bar);
        }
    }
}

void TestMasterBeatMath::barPosWithShift()
{
    // the packet carries the bar position of the beat, from barBeats (position + whole-beat offset)
    const double barBeatsNow = 2.75 + wrapOffset(1);   // a +1 beat jump
    const double beatsNow = 2.75;
    QCOMPARE(barPos(barBeatsNow + (3 - beatsNow)), 1); // beat 3 would be bar pos 4 -> shifted to 1
    QCOMPARE(barPos(barBeatsNow + (2 - beatsNow)), 4);
    QCOMPARE(barPos(0.0 + (4 - 0.0)), 1);              // wraps 4 -> 1
}

QTEST_APPLESS_MAIN(TestMasterBeatMath)
#include "test_master_beat_math.moc"
