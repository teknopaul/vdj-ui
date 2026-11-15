#include <QtTest/QtTest>
#include <chrono>
#include <mutex>
#include <vector>
#include "midi/alsa_clock.h"

using namespace vdj;
using Clock = std::chrono::steady_clock;

// Timing test against the real ALSA sequencer: skipped where there is none. Tolerances are loose on purpose (CI jitter).
class TestAlsaClockBeats : public QObject
{
    Q_OBJECT

private slots:
    void beatsEveryBeat();
    void offsetMovesPackets();
    void restartAndStop();

private:
    struct Beat { double ms; int bar; };
    // Run the clock at `bpm` for `runMs`; the beats' times in ms since start().
    std::vector<Beat> collect(AlsaClock& clock, int offsetMs, int runMs, bool* ok);
};

std::vector<TestAlsaClockBeats::Beat> TestAlsaClockBeats::collect(AlsaClock& clock, int offsetMs, int runMs, bool* ok)
{
    std::vector<Beat> beats;
    std::mutex m;
    Clock::time_point t0;
    clock.setAsMasterOffsetMs(offsetMs);
    clock.setBeatCallback([&](double, int bar) {
        std::lock_guard<std::mutex> lock(m);
        beats.push_back({ std::chrono::duration<double, std::milli>(Clock::now() - t0).count(), bar });
    });
    t0 = Clock::now();
    clock.start();
    QTest::qWait(runMs);
    clock.stop();
    clock.setBeatCallback(nullptr);
    *ok = true;
    return beats;
}

void TestAlsaClockBeats::beatsEveryBeat()
{
    AlsaClock clock;
    if (!clock.open(QStringLiteral("vdj-ui-test")))
        QSKIP("no ALSA sequencer");
    QVERIFY(clock.setBpm(150.0));   // 400 ms per beat
    bool ok = false;
    const auto beats = collect(clock, 0, 2600, &ok);
    // the first echo is for beat 1: beats at 400, 800, ... 2400 ms
    QVERIFY2(beats.size() >= 5 && beats.size() <= 7, qPrintable(QString::number(beats.size())));
    for (size_t i = 1; i < beats.size(); ++i)
        QVERIFY2(std::abs((beats[i].ms - beats[i - 1].ms) - 400.0) < 5.0, qPrintable(QString::number(beats[i].ms - beats[i - 1].ms)));
    for (size_t i = 1; i < beats.size(); ++i)
        QCOMPARE(beats[i].bar, (beats[i - 1].bar % 4) + 1);
    QCOMPARE(beats[0].bar, 2);   // beat 1 of the timeline is bar position 2
    QVERIFY(clock.beatsSent() >= beats.size());
}

void TestAlsaClockBeats::offsetMovesPackets()
{
    AlsaClock clock;
    if (!clock.open(QStringLiteral("vdj-ui-test")))
        QSKIP("no ALSA sequencer");
    QVERIFY(clock.setBpm(150.0));
    bool ok = false;
    const auto plain = collect(clock, 0, 1500, &ok);
    const auto late = collect(clock, 60, 1500, &ok);
    const auto early = collect(clock, -60, 1500, &ok);
    QVERIFY(plain.size() >= 2 && late.size() >= 2 && early.size() >= 2);
    // the same beat of each run (the queue restarts from 0 at every start()), a few ms of start-up jitter allowed
    QVERIFY2(std::abs((late[1].ms - plain[1].ms) - 60.0) < 8.0, qPrintable(QString::number(late[1].ms - plain[1].ms)));
    QVERIFY2(std::abs((early[1].ms - plain[1].ms) + 60.0) < 8.0, qPrintable(QString::number(early[1].ms - plain[1].ms)));
}

void TestAlsaClockBeats::restartAndStop()
{
    AlsaClock clock;
    if (!clock.open(QStringLiteral("vdj-ui-test")))
        QSKIP("no ALSA sequencer");
    QVERIFY(clock.setBpm(150.0));
    std::mutex m;
    std::vector<int> bars;
    clock.setBeatCallback([&](double, int bar) {
        std::lock_guard<std::mutex> lock(m);
        bars.push_back(bar);
    });
    clock.start();
    QTest::qWait(1000);
    clock.restart();
    size_t before;
    {
        std::lock_guard<std::mutex> lock(m);
        before = bars.size();
        bars.clear();
    }
    QVERIFY(before >= 1);
    QTest::qWait(1000);
    {
        std::lock_guard<std::mutex> lock(m);
        // after the restart the first beat is beat 1 again: bar position 2, then 3, 4, 1: no repeat of a position
        QVERIFY(bars.size() >= 1);
        QCOMPARE(bars[0], 2);
        for (size_t i = 1; i < bars.size(); ++i)
            QCOMPARE(bars[i], (bars[i - 1] % 4) + 1);
    }
    clock.stop();
    QTest::qWait(50);
    size_t after;
    {
        std::lock_guard<std::mutex> lock(m);
        after = bars.size();
    }
    QTest::qWait(900);
    std::lock_guard<std::mutex> lock(m);
    QCOMPARE(bars.size(), after);   // nothing after stop()
    clock.setBeatCallback(nullptr);
}

QTEST_MAIN(TestAlsaClockBeats)
#include "test_alsa_clock_beats.moc"
