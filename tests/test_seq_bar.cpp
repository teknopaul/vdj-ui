#include <QtTest/QtTest>
#include "midi/seq_bar.h"

using namespace vdj;

class TestSeqBar : public QObject
{
    Q_OBJECT

private slots:
    void barPosWraps();
    void barPosNegative();
    void nextBeat();
    void offsets();
    void wallMs();
};

void TestSeqBar::barPosWraps()
{
    QCOMPARE(barPos(0.0), 1);
    QCOMPARE(barPos(0.99), 1);
    QCOMPARE(barPos(1.0), 2);
    QCOMPARE(barPos(3.5), 4);
    QCOMPARE(barPos(4.0), 1);
    QCOMPARE(barPos(9.2), 2);
}

void TestSeqBar::barPosNegative()
{
    QCOMPARE(barPos(-0.5), 4);
    QCOMPARE(barPos(-1.0), 4);
    QCOMPARE(barPos(-4.0), 1);
    QCOMPARE(barPos(-4.5), 4);
}

void TestSeqBar::nextBeat()
{
    QCOMPARE(beatsToNextBeat(3.0), 0.0);
    QCOMPARE(beatsToNextBeat(3.25), 0.75);
    QVERIFY(std::abs(beatsToNextBeat(3.999) - 0.001) < 1e-9);
}

void TestSeqBar::offsets()
{
    QCOMPARE(wrapOffset(1), 1);
    QCOMPARE(wrapOffset(-1), 3);
    QCOMPARE(wrapOffset(5), 1);
    QCOMPARE(wrapOffset(-5), 3);
    QCOMPARE(wrapOffset(4), 0);
    // a +1 beat jump moves the label by one
    QCOMPARE(barPos(2.3 + wrapOffset(1)), 4);
    QCOMPARE(barPos(2.3 + wrapOffset(-1)), 2);
}

void TestSeqBar::wallMs()
{
    QCOMPARE(beatsToWallMs(1.0, 120.0), 500.0);
    QCOMPARE(beatsToWallMs(0.5, 120.0), 250.0);
    QCOMPARE(beatsToWallMs(1.0, 0.0), 0.0);
}

QTEST_APPLESS_MAIN(TestSeqBar)
#include "test_seq_bar.moc"
