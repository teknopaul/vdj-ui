#include <QtTest>
#include <random>
#include "network/beat_tempo.h"

using namespace vdj;

class TestBeatTempo : public QObject {
    Q_OBJECT

private slots:
    void noTempoUntilEnoughBeats()
    {
        BeatTempo t;
        for (int i = 0; i < BeatTempo::kMinBeats - 1; ++i)
            t.addBeat(i % 4 + 1, int64_t(i) * 468750000);
        QVERIFY(!t.valid());
        QCOMPARE(t.bpm(), 0.0);
    }

    void exactBeatsGiveExactTempo()
    {
        BeatTempo t;
        for (int i = 0; i < 20; ++i)
            t.addBeat(i % 4 + 1, int64_t(i) * 468750000);   // 128 BPM
        QVERIFY(t.valid());
        QVERIFY(std::fabs(t.bpm() - 128.0) < 1e-6);
    }

    void aDeckThatRunsSlowIsMeasuredSlow()
    {
        BeatTempo t;
        for (int i = 0; i < 40; ++i)
            t.addBeat(i % 4 + 1, int64_t(std::llround(i * 60e9 / 127.93)));   // reports 128.00, really 127.93
        QVERIFY(std::fabs(t.bpm() - 127.93) < 0.001);
    }

    void jitterAveragesOut()
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> jitter(-3e6, 3e6);   // +-3 ms per packet
        BeatTempo t;
        for (int i = 0; i < 64; ++i)
            t.addBeat(i % 4 + 1, int64_t(i * 60e9 / 126.5 + jitter(rng)));
        QVERIFY(std::fabs(t.bpm() - 126.5) < 0.05);
    }

    void aGapRestartsTheWindow()
    {
        BeatTempo t;
        for (int i = 0; i < 20; ++i)
            t.addBeat(i % 4 + 1, int64_t(i) * 468750000);
        t.addBeat(1, int64_t(60) * 1000000000);              // the deck stopped for a while
        QVERIFY(!t.valid());
        QCOMPARE(t.beats(), 1);
    }

    void aSkippedBarPositionRestartsTheWindow()
    {
        BeatTempo t;
        for (int i = 0; i < 10; ++i)
            t.addBeat(i % 4 + 1, int64_t(i) * 468750000);
        t.addBeat(4, int64_t(10) * 468750000);               // 3 followed by 4 was expected at i=10 -> 3
        QCOMPARE(t.beats(), 1);
    }
};

QTEST_APPLESS_MAIN(TestBeatTempo)
#include "test_beat_tempo.moc"
