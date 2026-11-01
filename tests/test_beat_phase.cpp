#include <QtTest>
#include <QFile>
#include <cmath>
#include <algorithm>
#include <deque>
#include <random>
#include <vector>
#include "network/beat_phase.h"

using namespace vdj;

namespace {

struct Run {
    QString file;
    double nominalMs = 0.0;            // from the packet's track BPM and pitch (or the reported BPM in old logs)
    std::vector<double> arrivalsMs;
};

// the beat lines of a sync telemetry CSV, split into runs of consecutive beats with the same tempo
std::vector<Run> loadRuns(const QString& name, int minBeats)
{
    std::vector<Run> runs;
    QFile f(QStringLiteral(VDJ_SYNC_FIXTURES) + QLatin1Char('/') + name);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return runs;
    const QStringList header = QString::fromUtf8(f.readLine()).trimmed().split(QLatin1Char(','));
    const int cT = header.indexOf(QStringLiteral("steady_ms")), cK = header.indexOf(QStringLiteral("kind")), cBar = header.indexOf(QStringLiteral("bar_pos"));
    const int cRep = header.indexOf(QStringLiteral("reported_bpm")), cTrk = header.indexOf(QStringLiteral("track_bpm_raw")), cPit = header.indexOf(QStringLiteral("pitch_raw"));
    Run cur;
    QString curKey;
    int lastBar = 0;
    double lastT = 0.0;
    auto flush = [&]() {
        if (int(cur.arrivalsMs.size()) >= minBeats) {
            cur.file = name;
            runs.push_back(cur);
        }
        cur = Run();
    };
    while (!f.atEnd()) {
        const QStringList c = QString::fromUtf8(f.readLine()).trimmed().split(QLatin1Char(','));
        if (c.size() <= cBar || c[cK] != QLatin1String("beat"))
            continue;
        const double t = c[cT].toDouble();
        const int bar = c[cBar].toInt();
        // new logs: the raw track BPM and pitch say whether the tempo changed; old logs only have the reported BPM
        const QString key = (cPit >= 0 && c.size() > cPit) ? c[cTrk] + QLatin1Char('/') + c[cPit] : c[cRep];
        if (!cur.arrivalsMs.empty() && !(key == curKey && bar == lastBar % 4 + 1 && t - lastT > 0 && t - lastT < 1500))
            flush();
        if (cur.arrivalsMs.empty()) {
            curKey = key;
            if (cPit >= 0 && c.size() > cPit && c[cPit].toDouble() > 0)
                cur.nominalMs = 60000.0 / (c[cTrk].toDouble() / 100.0 * c[cPit].toDouble() / 1048576.0);
            else
                cur.nominalMs = 60000.0 / c[cRep].toDouble();
        }
        cur.arrivalsMs.push_back(t);
        lastBar = bar;
        lastT = t;
    }
    flush();
    return runs;
}

void feed(BeatPhase& p, const std::vector<double>& ms, size_t from, size_t count)
{
    for (size_t i = from; i < from + count; ++i)
        p.addBeat(int(i % 4) + 1, int64_t(std::llround(ms[i] * 1e6)));
}

} // namespace

class TestBeatPhase : public QObject {
    Q_OBJECT

private slots:
    void quantumIsLearnedFromTheIntervalLevels()
    {
        // 540.0 and 543.0 ms intervals with a few 0.5 ms odd ones, like the real deck
        std::deque<double> t;
        double now = 1000.0;
        std::mt19937 rng(1);
        for (int i = 0; i < 60; ++i) {
            t.push_back(now);
            now += (rng() % 3 == 0) ? 540.0 : 543.0;
            if (i % 17 == 5) now += 0.5;
        }
        QCOMPARE(BeatPhase::learnQuantum(t), 3.0);
    }

    void noQuantumFromOneLevel()
    {
        BeatPhase p;
        std::vector<double> ms;
        for (int i = 0; i < 20; ++i) ms.push_back(1000.0 + 540.0 * i);
        feed(p, ms, 0, ms.size());
        const BeatPhase::Estimate e = p.estimate();
        QVERIFY(!e.valid);
        QCOMPARE(e.quantumMs, 0.0);
        QCOMPARE(e.periodPpmUncertainty, -1.0);
    }

    void resonantWhenEveryPacketIsEquallyLate()
    {
        BeatPhase p;
        p.setQuantumHint(3.0);
        std::vector<double> ms;
        for (int i = 0; i < 16; ++i) ms.push_back(1000.0 + 540.0 * i);   // period a multiple of the tick: no beat moves through it
        feed(p, ms, 0, ms.size());
        const BeatPhase::Estimate e = p.estimate();
        QVERIFY(e.valid);
        QVERIFY(e.resonant);
        QVERIFY2(e.phaseUncertaintyMs > 0.8 * 3.0 / 2.0, qPrintable(QString::number(e.phaseUncertaintyMs)));
        QVERIFY2(e.periodPpmUncertainty > 300.0, qPrintable(QString::number(e.periodPpmUncertainty)));
    }

    void syntheticTruthIsInsideTheReportedUncertainty()
    {
        std::mt19937 rng(20261101);
        std::uniform_real_distribution<double> uP(400.0, 700.0), uPhi(0.0, 3.0), uJ(-0.15, 0.15), u01(0.0, 1.0), uOut(0.25, 0.75);
        for (int N : { 8, 16, 32, 64 }) {
            int ok = 0, valid = 0;
            std::vector<double> unc;
            for (int trial = 0; trial < 400; ++trial) {
                const double P = uP(rng), phi = uPhi(rng), g = uPhi(rng), q = 3.0;
                BeatPhase p;
                p.setQuantumHint(q);
                double truthLast = 0.0;
                for (int i = 0; i < N; ++i) {
                    const double b = phi + i * P;
                    double a = g + q * std::ceil((b - g) / q) + uJ(rng) + 100000.0;
                    if (u01(rng) < 0.03) a += (u01(rng) < 0.5 ? -1 : 1) * uOut(rng);   // an odd packet now and then
                    p.addBeat(i % 4 + 1, int64_t(std::llround(a * 1e6)));
                    truthLast = b + 100000.0;
                }
                const BeatPhase::Estimate e = p.estimate();
                if (!e.valid) continue;
                ++valid;
                if (std::fabs(e.lastBeatMs - truthLast) <= e.phaseUncertaintyMs + 1e-6) ++ok;
                if (!e.resonant) unc.push_back(e.phaseUncertaintyMs);
            }
            QVERIFY2(valid > 380, qPrintable(QStringLiteral("N=%1 valid %2").arg(N).arg(valid)));
            const double contained = double(ok) / valid;
            QVERIFY2(contained >= 0.95, qPrintable(QStringLiteral("N=%1 contained %2").arg(N).arg(contained)));
            if (N == 32) {
                std::sort(unc.begin(), unc.end());
                QVERIFY(unc.size() > 200);
                QVERIFY2(unc[unc.size() / 2] < 1.0, qPrintable(QString::number(unc[unc.size() / 2])));   // sub-millisecond, as promised
            }
        }
    }

    void realDecksQuantumAndIntervals()
    {
        const QStringList files = { QStringLiteral("pitch-sweep-114-82bpm-old-schema.csv"), QStringLiteral("steady-126-30bpm-plus10pct.csv"),
                                    QStringLiteral("pitch-sweep-minus10-to-plus3.csv") };
        int checked = 0;
        for (const QString& name : files) {
            for (const Run& r : loadRuns(name, 40)) {
                BeatPhase p;
                feed(p, r.arrivalsMs, 0, r.arrivalsMs.size());
                const BeatPhase::Estimate e = p.estimate();
                QVERIFY2(e.quantumMs == 3.0, qPrintable(QStringLiteral("%1: quantum %2").arg(name).arg(e.quantumMs)));
                // intervals are whole numbers of ticks, give or take the odd 0.5 ms packet
                int onGrid = 0;
                for (size_t i = 1; i < r.arrivalsMs.size(); ++i) {
                    const double d = (r.arrivalsMs[i] - r.arrivalsMs[i - 1]) / 3.0;
                    if (std::fabs(d - std::round(d)) * 3.0 < 0.4) ++onGrid;
                }
                QVERIFY2(double(onGrid) / (r.arrivalsMs.size() - 1) > 0.92, qPrintable(name));
                ++checked;
            }
        }
        QVERIFY2(checked >= 4, qPrintable(QString::number(checked)));   // fixtures found and parsed
    }

    void realDecksPeriodIsInsideItsUncertainty()
    {
        // the long runs say the deck is about 20 ppm slow; in 32 beat windows the nominal period (+20 ppm) must be inside the band
        int windows = 0, inside = 0, phaseOk = 0;
        for (const QString& name : { QStringLiteral("pitch-sweep-114-82bpm-old-schema.csv"), QStringLiteral("steady-126-30bpm-plus10pct.csv"),
                                     QStringLiteral("pitch-sweep-minus10-to-plus3.csv") }) {
            for (const Run& r : loadRuns(name, 40)) {
                const double truth = r.nominalMs * (1.0 + 20e-6);
                for (size_t s = 0; s + 32 <= r.arrivalsMs.size(); s += 8) {
                    BeatPhase p;
                    p.setQuantumHint(3.0);
                    feed(p, r.arrivalsMs, s, 32);
                    const BeatPhase::Estimate e = p.estimate();
                    if (!e.valid) continue;
                    ++windows;
                    const double bound = e.periodMs * e.periodPpmUncertainty * 1e-6;
                    if (std::fabs(e.periodMs - truth) <= bound + 0.02) ++inside;
                    // the last beat is no later than its packet, and no more than a tick and the tolerance earlier
                    const double arrival = r.arrivalsMs[s + 31];
                    if (e.lastBeatMs - e.phaseUncertaintyMs <= arrival + 0.01 && e.lastBeatMs + e.phaseUncertaintyMs >= arrival - 3.0 - 1.0) ++phaseOk;
                }
            }
        }
        QVERIFY2(windows > 20, qPrintable(QString::number(windows)));
        QVERIFY2(double(inside) / windows > 0.9, qPrintable(QStringLiteral("%1 of %2").arg(inside).arg(windows)));
        QCOMPARE(phaseOk, windows);
    }

    void shortSweepRunsAreNotATempo()
    {
        // 6 to 10 beats at one fader position: the "errors" there were the quantisation pattern. The estimate must not claim a rate
        // better than +-100 ppm (the deck's real error is about 20 ppm, so such a run cannot see it)
        int shortRuns = 0;
        for (const Run& r : loadRuns(QStringLiteral("pitch-sweep-minus10-to-plus3.csv"), 6)) {
            if (r.arrivalsMs.size() > 12) continue;
            BeatPhase p;
            p.setQuantumHint(3.0);
            feed(p, r.arrivalsMs, 0, r.arrivalsMs.size());
            const BeatPhase::Estimate e = p.estimate();
            if (!e.valid) continue;
            QVERIFY2(e.periodPpmUncertainty > 100.0, qPrintable(QString::number(e.periodPpmUncertainty)));
            ++shortRuns;
        }
        QVERIFY(shortRuns >= 5);
    }

    void aBpmChangeOrGapRestartsTheWindow()
    {
        BeatPhase p;
        p.setQuantumHint(3.0);
        p.setReportedBpm(123.03);
        for (int i = 0; i < 12; ++i)
            p.addBeat(i % 4 + 1, int64_t(i) * 487686000);
        QCOMPARE(p.beats(), 12);
        p.setReportedBpm(123.03);                         // unchanged
        QCOMPARE(p.beats(), 12);
        p.setReportedBpm(124.0);                          // the DJ moved the fader
        QCOMPARE(p.beats(), 0);
        QCOMPARE(p.quantumMs(), 3.0);                     // the quantum is kept
        for (int i = 0; i < 8; ++i)
            p.addBeat(i % 4 + 1, int64_t(i) * 483870000);
        p.addBeat(1, int64_t(90) * 1000000000);           // a long gap
        QCOMPARE(p.beats(), 1);
        p.addBeat(2, int64_t(90) * 1000000000 + 483000000);
        p.addBeat(4, int64_t(91) * 1000000000);           // a skipped bar position
        QCOMPARE(p.beats(), 1);
    }
};

QTEST_APPLESS_MAIN(TestBeatPhase)
#include "test_beat_phase.moc"
