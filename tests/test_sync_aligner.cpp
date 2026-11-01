#include <QtTest>
#include <cmath>
#include "midi/sync_aligner.h"

using namespace vdj;

class TestSyncAligner : public QObject {
    Q_OBJECT
private slots:
    void wrapsToTheNearestBeat()
    {
        QCOMPARE(SyncAligner::wrap(300.0, 500.0), -200.0);
        QCOMPARE(SyncAligner::wrap(-300.0, 500.0), 200.0);
        QCOMPARE(SyncAligner::wrap(120.0, 500.0), 120.0);
        QCOMPARE(SyncAligner::wrap(1100.0, 500.0), 100.0);
    }

    void planTakesTheShortWay()
    {
        const auto p = SyncAligner::plan(300.0, 0.0, 500.0);
        QCOMPARE(p.shiftMs, -200.0);
        QVERIFY(p.needed());
    }

    void thetargetIsWhereWeWantToEndUp()
    {
        const auto p = SyncAligner::plan(30.0, 20.0, 500.0);
        QCOMPARE(p.shiftMs, 10.0);
        QCOMPARE(SyncAligner::plan(20.0, 20.0, 500.0).beats, 0);
    }

    void nothingToDoWhenWithinAMillisecond()
    {
        QVERIFY(!SyncAligner::plan(0.6, 0.0, 500.0).needed());
        QVERIFY(!SyncAligner::plan(-0.9, 0.0, 500.0).needed());
        QVERIFY(SyncAligner::plan(1.2, 0.0, 500.0).needed());
    }

    void largeShiftsAreSplitIntoBeatsOfAtMostTheStepCap()
    {
        const auto p = SyncAligner::plan(-200.0, 0.0, 500.0);
        QCOMPARE(p.beats, 5);                       // 200 / 40
        int sum = 0;
        for (int i = 0; i < p.beats; ++i) {
            QVERIFY(std::abs(SyncAligner::step(p, i)) <= int(SyncAligner::kStepMs));
            sum += SyncAligner::step(p, i);
        }
        QCOMPARE(sum, -200);
        QCOMPARE(SyncAligner::step(p, p.beats), 0);
    }

    void stepsAddUpWhenNotDivisible()
    {
        const auto p = SyncAligner::plan(101.0, 0.0, 500.0);
        QCOMPARE(p.beats, 3);
        int sum = 0;
        for (int i = 0; i < p.beats; ++i) {
            QVERIFY(std::abs(SyncAligner::step(p, i)) <= 41);
            sum += SyncAligner::step(p, i);
        }
        QCOMPARE(sum, 101);
    }

    void theGridWaitIsPassedThrough()
    {
        QCOMPARE(SyncAligner::plan(50.0, 0.0, 500.0, 125.0).waitMs, 125.0);
        QCOMPARE(SyncAligner::plan(50.0, 0.0, 500.0).waitMs, 0.0);
    }
};

QTEST_APPLESS_MAIN(TestSyncAligner)
#include "test_sync_aligner.moc"
