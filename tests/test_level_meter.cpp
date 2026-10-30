#include <QtTest/QtTest>

#include "ui/level_meter_widget.h"

using vdj::LevelMeterWidget;

class TestLevelMeter : public QObject {
    Q_OBJECT
private slots:
    void decibels()
    {
        QCOMPARE(LevelMeterWidget::toDb(1.0f), 0.0f);
        QVERIFY(qAbs(LevelMeterWidget::toDb(0.5f) - (-6.0206f)) < 0.01f);
        QCOMPARE(LevelMeterWidget::toDb(0.0f), LevelMeterWidget::kFloorDb);
        QCOMPARE(LevelMeterWidget::toDb(1e-9f), LevelMeterWidget::kFloorDb);      // floored, never -inf
    }

    void silenceIsNoSignal()
    {
        LevelMeterWidget m;
        m.setLevels(0, 0, 0, 0);
        QVERIFY(!m.hasSignal());
        QCOMPARE(m.zone(), QStringLiteral("low"));
        m.setLevels(0.01f, 0, 0.005f, 0);                                         // -40 dB on the left only
        QVERIFY(m.hasSignal());
    }

    void zonesFollowThePeak()
    {
        LevelMeterWidget m;
        m.setLevels(0.05f, 0.05f, 0.02f, 0.02f);                                  // about -26 dB
        QCOMPARE(m.zone(), QStringLiteral("low"));
        m.reset();
        m.setLevels(0.25f, 0.1f, 0.1f, 0.05f);                                    // -12 dB loudest
        QCOMPARE(m.zone(), QStringLiteral("good"));
        m.reset();
        m.setLevels(0.7f, 0.1f, 0.3f, 0.05f);                                     // -3 dB
        QCOMPARE(m.zone(), QStringLiteral("hot"));
        QVERIFY(!m.clipped());
    }

    void clipLatchesUntilCleared()
    {
        LevelMeterWidget m;
        m.setLevels(1.0f, 0.1f, 0.5f, 0.05f);
        QVERIFY(m.clipped());
        m.setLevels(0.01f, 0.01f, 0.005f, 0.005f);                                // quiet again, the light stays on
        QVERIFY(m.clipped());
        QCOMPARE(m.zone(), QStringLiteral("clip"));
        m.clearClip();
        QVERIFY(!m.clipped());
        m.reset();
        QVERIFY(!m.hasSignal());
    }

    void peakHoldAndBarFall()
    {
        LevelMeterWidget m;
        m.setLevels(0.5f, 0.5f, 0.4f, 0.4f);
        const float hold = m.peakHoldDb(0);
        m.setLevels(0.01f, 0.01f, 0.005f, 0.005f);
        QCOMPARE(m.peakHoldDb(0), hold);                                          // held for a moment
        QVERIFY(m.rmsDb(0) > LevelMeterWidget::toDb(0.005f));                      // the bar falls gradually
        QCOMPARE(m.peakDb(0), LevelMeterWidget::toDb(0.01f));
    }
};

QTEST_MAIN(TestLevelMeter)
#include "test_level_meter.moc"
