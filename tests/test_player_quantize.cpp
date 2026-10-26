#include <QtTest/QtTest>
#include "audio/audio_player.h"
#include <QPushButton>
#include <QTemporaryDir>
#include "audio/quantize_settings.h"
#include "ui/player_panel.h"
#include "ui/waveform_widget.h"
#include "ui/track_analysis.h"

using namespace vdj;

class TestPlayerQuantize : public QObject
{
    Q_OBJECT

private:
    static TrackAnalysis grid500()
    {
        TrackAnalysis a;
        a.valid = true;
        for (int i = 0; i < 40; ++i) {
            AnalysisBeat b;
            b.ms = i * 500.0;
            b.number = quint8(i % 4 + 1);
            a.beats.append(b);
        }
        a.beatCount = a.beats.size();
        return a;
    }

private slots:
    void noGridByDefault();
    void phaseFollowsPosition();
    void beatChangedOnlyOnNewBeat();
    void buttons();
    void resolutionSetting();
};

void TestPlayerQuantize::noGridByDefault()
{
    PlayerPanel panel;
    QVERIFY(!panel.hasBeatGrid());
    QVERIFY(!panel.beatPhase().valid);
}

void TestPlayerQuantize::phaseFollowsPosition()
{
    PlayerPanel panel;
    panel.setAnalysisForTest(grid500());
    QVERIFY(panel.hasBeatGrid());
    emit panel.player()->positionChanged(1040);
    const auto p = panel.beatPhase();
    QVERIFY(p.valid);
    QCOMPARE(p.beatIndex, 2);
    QCOMPARE(p.beatNumber, 3);
    QCOMPARE(p.sinceBeatMs, 40.0);
    QCOMPARE(p.fraction, 0.08);
}

void TestPlayerQuantize::beatChangedOnlyOnNewBeat()
{
    PlayerPanel panel;
    panel.setAnalysisForTest(grid500());
    QSignalSpy spy(&panel, &PlayerPanel::beatChanged);
    emit panel.player()->positionChanged(1040);
    emit panel.player()->positionChanged(1100);
    QCOMPARE(spy.count(), 1);
    emit panel.player()->positionChanged(1510);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.last().at(0).toInt(), 4);
}

void TestPlayerQuantize::buttons()
{
    PlayerPanel panel;
    QCOMPARE(panel.quantizeButton()->text(), QStringLiteral("Q"));
    QCOMPARE(panel.quantizeButton()->toolTip(), QStringLiteral("Quantize"));
    QVERIFY(panel.quantizeButton()->isCheckable());
    QVERIFY(!panel.quantizeButton()->isChecked());
    QVERIFY(!panel.backButton()->isEnabled());       // nothing loaded
    QVERIFY(!panel.forwardButton()->isEnabled());
    QVERIFY(panel.playButton()->text().contains(QString::fromUtf8("\u25b6")));
}

void TestPlayerQuantize::resolutionSetting()
{
    QuantizeSettings s;
    QCOMPARE(s.subdivisions(), 1);
    QSignalSpy spy(&s, &QuantizeSettings::changed);
    s.setResolution(QuantizeSettings::Quarter);
    QCOMPARE(s.subdivisions(), 4);
    s.setResolution(QuantizeSettings::Half);
    QCOMPARE(s.subdivisions(), 2);
    s.setResolution(QuantizeSettings::Half);
    QCOMPARE(spy.count(), 2);
}

QTEST_MAIN(TestPlayerQuantize)
#include "test_player_quantize.moc"
