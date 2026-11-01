#include <QtTest>
#include <QThread>
#include <chrono>
#include <cmath>
#include <set>
#include "midi/alsa_clock.h"
#include "midi/seq_phase_model.h"

using namespace vdj;

class TestSeqPhaseModel : public QObject {
    Q_OBJECT

private slots:
    void invalidUntilAnchored()
    {
        SeqPhaseModel m;
        QVERIFY(!m.valid());
        QCOMPARE(m.beatsAt(1000), -1.0);
        m.setTempo(1000, 400000.0);                     // ignored
        QVERIFY(!m.valid());
        QVERIFY(!m.consistentWith(0.0, 1000));
        m.reset(0, 0.0, 500000.0);
        QVERIFY(m.valid());
        m.invalidate();
        QVERIFY(!m.valid());
    }

    void constantTempoIsLinear()
    {
        SeqPhaseModel m;
        m.reset(1000000000, 0.0, 487686.0);
        QCOMPARE(m.beatsAt(1000000000), 0.0);
        QVERIFY(std::fabs(m.beatsAt(1000000000 + 487686000) - 1.0) < 1e-12);
        QVERIFY(std::fabs(m.beatsAt(1000000000 + 10 * 487686000LL) - 10.0) < 1e-9);
    }

    void tempoChangesAreIntegrated()
    {
        // 2 s at 500 ms per beat (4 beats), 1 s at 400 ms per beat (2.5 beats), then 600 ms per beat
        SeqPhaseModel m;
        m.reset(0, 0.0, 500000.0);
        m.setTempo(2000000000LL, 400000.0);
        m.setTempo(3000000000LL, 600000.0);
        // (a model is only asked about times at or after its last tempo change, so check each segment from a copy)
        SeqPhaseModel a = m;
        QVERIFY(std::fabs(a.beatsAt(3000000000LL) - 6.5) < 1e-12);
        QVERIFY(std::fabs(a.beatsAt(3600000000LL) - 7.5) < 1e-12);
        SeqPhaseModel b;
        b.reset(0, 0.0, 500000.0);
        b.setTempo(2000000000LL, 400000.0);
        QVERIFY(std::fabs(b.beatsAt(2000000000LL) - 4.0) < 1e-12);
        QVERIFY(std::fabs(b.beatsAt(3000000000LL) - 6.5) < 1e-12);
        // the position never goes backwards over a tempo change: feed the changes as time passes
        SeqPhaseModel c;
        c.reset(0, 0.0, 500000.0);
        double prev = -1.0;
        for (int64_t t = 0; t <= 4000000000LL; t += 1000000) {
            if (t == 2000000000LL) c.setTempo(t, 400000.0);
            if (t == 3000000000LL) c.setTempo(t, 600000.0);
            const double v = c.beatsAt(t);
            QVERIFY(v >= prev);
            prev = v;
        }
    }

    void aNudgeShiftsThePhaseByTheExtraTime()
    {
        // what AlsaClock::nudgeMillis(+10) does at 487.686 ms per beat: the tempo is 10 ms longer per beat for the next
        // 487.686 ms of wall time (its deadline), then it is restored. The beat is not 10 ms later, it is 10 * base / (base + 10) ms later.
        const double base = 487686.0, extra = 10000.0;
        SeqPhaseModel nudged, plain;
        nudged.reset(0, 0.0, base);
        plain.reset(0, 0.0, base);
        const int64_t t0 = 5000000000LL;
        nudged.setTempo(t0, base + extra);
        const int64_t t1 = t0 + int64_t(base * 1000.0);
        nudged.setTempo(t1, base);
        const int64_t later = t1 + 2000000000LL;
        const double lagBeats = plain.beatsAt(later) - nudged.beatsAt(later);
        const double lagMs = lagBeats * base / 1000.0;
        QVERIFY2(std::fabs(lagMs - 10.0 * base / (base + extra)) < 1e-6, qPrintable(QString::number(lagMs, 'f', 6)));
    }

    void consistencyWithTheTickCounter()
    {
        SeqPhaseModel m;
        m.reset(0, 0.0, 487686.0);
        for (int64_t t = 0; t < 3000000000LL; t += 777777) {
            const double exact = m.beatsAt(t);
            const double tickBeats = std::floor(exact * 96.0) / 96.0;    // the queue's counter: the floor
            QVERIFY(m.consistentWith(tickBeats, t));
        }
        // truth in the middle of tick 100 (the counter reads 100): a model that is 1.4 ticks off is still believed, 3 ticks off is not
        for (double e : { -1.4, 1.4 }) {
            SeqPhaseModel off;
            off.reset(0, (100.5 + e) / 96.0, 487686.0);
            QVERIFY(off.consistentWith(100.0 / 96.0, 0));
        }
        for (double e : { -3.0, 3.0 }) {
            SeqPhaseModel off;
            off.reset(0, (100.5 + e) / 96.0, 487686.0);
            QVERIFY(!off.consistentWith(100.0 / 96.0, 0));
        }
    }

    // ---- the real ALSA queue ----

    void queuePositionIsBetweenTicks()
    {
        AlsaClock c;
        if (!c.open(QStringLiteral("vdj-ui-test-phase"), nullptr))
            QSKIP("no ALSA sequencer available");
        QVERIFY(c.setBpm(123.03));
        QCOMPARE(c.positionBeatsExact(), -1.0);           // not running
        c.start();
        double prev = -1.0;
        double minD = 1e9, maxD = -1e9;
        std::set<long> distinctTicks;
        int distinctExact = 0;
        for (int i = 0; i < 300; ++i) {
            QThread::msleep(3);
            const double exact = c.positionBeatsExact();
            const double tick = c.positionBeats();
            QVERIFY(exact >= prev);                         // never backwards
            if (exact != prev) ++distinctExact;
            prev = exact;
            if (tick >= 0.0) {
                const double d = (exact - tick) * AlsaClock::kPpq;   // in ticks
                minD = std::min(minD, d);
                maxD = std::max(maxD, d);
                distinctTicks.insert(long(tick * AlsaClock::kPpq));
            }
        }
        QVERIFY2(minD > -0.5 && maxD < 1.6, qPrintable(QStringLiteral("exact - tick in [%1, %2] ticks").arg(minD).arg(maxD)));
        QVERIFY(distinctExact > 250);                       // a new value on (nearly) every read, not one per 5 ms
        QCOMPARE(c.modelResyncs(), 0);                      // the model anchored at start matched the tick counter throughout
        c.stop();
    }

    void aNudgeMovesTheExactPositionByTheRealExtraTime()
    {
        AlsaClock c;
        if (!c.open(QStringLiteral("vdj-ui-test-phase-nudge"), nullptr))
            QSKIP("no ALSA sequencer available");
        QVERIFY(c.setBpm(123.03));
        c.start();
        QThread::msleep(300);
        int64_t s0 = 0, s1 = 0;
        const double p0 = c.positionBeatsExact(&s0);
        c.nudgeMillis(10);
        QThread::msleep(900);                               // the nudged beat is over (about 488 ms)
        const double p1 = c.positionBeatsExact(&s1);
        const double base = 60000000.0 / 123.03;            // micros per beat (the queue rounds to whole micros)
        const double expected = p0 + double(s1 - s0) / (base * 1000.0);
        const double lagMs = (expected - p1) * base / 1000.0;
        // 10 ms longer for one beat's worth of wall time = 10 * base / (base + 10000) = 9.8 ms. The tick counter alone could not show this.
        QVERIFY2(std::fabs(lagMs - 9.8) < 0.7, qPrintable(QStringLiteral("lag %1 ms").arg(lagMs)));
        QCOMPARE(c.modelResyncs(), 0);
        c.stop();
    }

    void restartAndSetBpmKeepTheModelRight()
    {
        AlsaClock c;
        if (!c.open(QStringLiteral("vdj-ui-test-phase-restart"), nullptr))
            QSKIP("no ALSA sequencer available");
        QVERIFY(c.setBpm(120.0));
        c.start();
        QThread::msleep(700);
        c.restart();
        QThread::msleep(50);
        const double afterRestart = c.positionBeatsExact();
        QVERIFY2(afterRestart >= 0.0 && afterRestart < 0.3, qPrintable(QString::number(afterRestart)));
        QVERIFY(c.setBpm(140.0));
        QThread::msleep(200);
        QVERIFY(c.setBpm(100.0));
        QThread::msleep(200);
        const double exact = c.positionBeatsExact();
        const double tick = c.positionBeats();
        const double d = (exact - tick) * AlsaClock::kPpq;
        QVERIFY2(d > -0.5 && d < 1.6, qPrintable(QString::number(d)));
        QCOMPARE(c.modelResyncs(), 0);
        c.stop();
        QCOMPARE(c.positionBeatsExact(), -1.0);
    }

    void offsetsAgreeWithinATick()
    {
        AlsaClock c;
        if (!c.open(QStringLiteral("vdj-ui-test-phase-offset"), nullptr))
            QSKIP("no ALSA sequencer available");
        QVERIFY(c.setBpm(123.03));
        c.start();
        for (int i = 0; i < 50; ++i) {
            QThread::msleep(7);
            double exactMs = 0.0, tickMs = 0.0;
            QVERIFY(c.beatOffsetMs(&exactMs));
            QVERIFY(c.beatOffsetTickMs(&tickMs));
            QVERIFY2(std::fabs(exactMs - tickMs) < 9.0, qPrintable(QStringLiteral("%1 vs %2").arg(exactMs).arg(tickMs)));
        }
        c.stop();
    }
};

QTEST_APPLESS_MAIN(TestSeqPhaseModel)
#include "test_seq_phase_model.moc"
