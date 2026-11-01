#include <QtTest>
#include <random>
#include <QTemporaryDir>
#include "network/beat_tempo.h"
#include "midi/sync_telemetry.h"

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

    void millisecondQuantisedArrivalsDoNotBiasTheTempo()
    {
        // packets stamped to whole milliseconds (rounding of ns/ms) still give the real tempo
        for (double bpm : { 120.0, 127.93, 133.37, 98.01 }) {
            BeatTempo t;
            for (int i = 0; i < 64; ++i)
                t.addBeat(i % 4 + 1, int64_t(std::llround(i * 60e3 / bpm)) * 1000000);
            QVERIFY2(std::fabs(t.bpm() - bpm) < 0.02, qPrintable(QString::number(t.bpm(), 'f', 4)));
        }
    }

    void slidingWindowIsStableAndFollowsADrift()
    {
        // 0.1% faster than reported over a long run: the reading never wobbles by more than 0.02 BPM between beats
        BeatTempo t;
        double prev = 0.0, maxStep = 0.0;
        const double real = 128.128;
        for (int i = 0; i < 200; ++i) {
            t.addBeat(i % 4 + 1, int64_t(std::llround(i * 60e9 / real)));
            if (t.valid()) {
                if (prev > 0.0) maxStep = std::max(maxStep, std::fabs(t.bpm() - prev));
                prev = t.bpm();
            }
        }
        QVERIFY(t.beats() == BeatTempo::kMaxBeats);
        QVERIFY(std::fabs(t.bpm() - real) < 1e-4);
        QVERIFY(maxStep < 0.02);
    }

    void telemetryLineFormat()
    {
        SyncTelemetry::Sample s;
        s.steadyMs = 1234.5;
        s.pid = 2; s.barPos = 3;
        s.reportedBpm = 128.0; s.measuredBpm = 127.93; s.alsaBpm = 128.0;
        s.diffMs = -4.25; s.avgDiffMs = -3.5;
        s.targetMs = 5; s.nudgeMs = -2; s.masterPid = 2;
        s.syncState = QStringLiteral("locked");
        s.trackBpmRaw = 13300; s.pitchRaw = 1045954; s.nextBeatMs = 451; s.beat8Ms = 3609; s.diffTickMs = -5.08; s.estPeriodMs = 487.1234; s.estPhaseUncMs = 0.456; s.estResonant = 1; s.diffUsedMs = -4.123;
        QCOMPARE(SyncTelemetry::formatBeat(s),
                 QStringLiteral("1234.500,beat,2,3,128.000,127.930,128.000,-4.25,-3.50,5,-2,2,locked,\"\",13300,1045954,-0.2501,451,3609,-5.08,487.1234,0.456,1,-4.123"));
        QVERIFY(SyncTelemetry::formatEvent(10.0, QStringLiteral("press \"sync\""), s).contains(QStringLiteral(",locked,\"press 'sync'\",")));
        // the header has one column name per field of a line
        QCOMPARE(SyncTelemetry::header().count(QLatin1Char(',')), 23);
    }

    void telemetryIsSilentUntilStarted()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SyncTelemetry t;
        SyncTelemetry::Sample s;
        t.beat(s);                                    // not started: nothing opened or written
        QVERIFY(!t.active());
        QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
        QVERIFY(t.start(dir.path()));
        t.beat(s);
        t.event(1.0, QStringLiteral("x"), s);
        t.stop();
        t.beat(s);                                    // stopped: not written
        QFile f(t.filePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll().split('\n').size(), 4);   // header, beat, event, trailing empty
    }
};

QTEST_APPLESS_MAIN(TestBeatTempo)
#include "test_beat_tempo.moc"
