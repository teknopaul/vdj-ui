#include <QtTest/QtTest>
#include "audio/audio_player.h"
#include <QPushButton>
#include <QTemporaryDir>
#include "audio/quantize_settings.h"
#include "ui/player_panel.h"
#include "ui/waveform_widget.h"
#include "ui/track_analysis.h"
#include <QDataStream>
#include <QFile>

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
    void sequencerGridWait();
    void bpmResetGoesBackToTheTracksBpm();
    void playerKeys();
    void cueKeys();
    void bpmLock();
    void playWaitsForTheSequencerBeat();
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
    QCOMPARE(panel.quantizeButton()->toolTip(), QStringLiteral("Quantize (q)"));
    QVERIFY(panel.quantizeButton()->isCheckable());
    QVERIFY(!panel.quantizeButton()->isChecked());
    QVERIFY(!panel.backButton()->isEnabled());       // nothing loaded
    QVERIFY(!panel.forwardButton()->isEnabled());
    QVERIFY(panel.playButton()->text().contains(QString::fromUtf8("\u25b6")));
    QVERIFY(panel.backButton()->toolTip().contains("Page Up"));
    QVERIFY(panel.playButton()->toolTip().contains("Space"));
    QVERIFY(panel.forwardButton()->toolTip().contains("Page Down"));
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

void TestPlayerQuantize::bpmResetGoesBackToTheTracksBpm()
{
    QTemporaryDir dir;
    QFile f(dir.filePath("a.wav"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    const int frames = 44100;
    QByteArray h;
    QDataStream s(&h, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData("RIFF", 4); s << quint32(36 + frames * 4); s.writeRawData("WAVEfmt ", 8);
    s << quint32(16) << quint16(1) << quint16(2) << quint32(44100) << quint32(44100 * 4) << quint16(4) << quint16(16);
    s.writeRawData("data", 4); s << quint32(frames * 4);
    f.write(h);
    f.write(QByteArray(frames * 4, 0));
    f.close();

    PlayerPanel panel;
    QVERIFY(!panel.bpmResetButton()->isEnabled());   // nothing loaded
    TrackInfo info;
    info.title = "t";
    info.audioPath = dir.filePath("a.wav");
    info.fields.append({QStringLiteral("BPM"), QStringLiteral("124")});
    if (!panel.loadTrack(info))
        QSKIP("cannot load a wav here");
    QVERIFY(panel.bpmResetButton()->isEnabled());
    QCOMPARE(panel.targetBpm(), 124.0);
    panel.setTargetBpm(131.3);
    QCOMPARE(panel.targetBpm(), 131.3);
    panel.bpmResetButton()->click();
    QCOMPARE(panel.targetBpm(), 124.0);
    QCOMPARE(panel.player()->tempo(), 1.0);
}

void TestPlayerQuantize::playerKeys()
{
    QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QKeyEvent ctrlSpace(QEvent::KeyPress, Qt::Key_Space, Qt::ControlModifier);
    QKeyEvent pgDn(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
    QKeyEvent letter(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QVERIFY(PlayerPanel::isPlayerKey(&space));
    QVERIFY(PlayerPanel::isPlayerKey(&pgDn));
    QVERIFY(!PlayerPanel::isPlayerKey(&ctrlSpace));
    QVERIFY(!PlayerPanel::isPlayerKey(&letter));

    PlayerPanel panel;
    QVERIFY(!panel.wantsPlayerKey(&space));          // nothing loaded: Space stays the VDJ's play / pause
    QVERIFY(!panel.handlePlayerKey(&space));

    // Up / Down while the panel has the focus ask the owner to focus the track list
    QSignalSpy focus(&panel, &PlayerPanel::focusTrackListRequested);
    QTest::keyClick(&panel, Qt::Key_Down);
    QTest::keyClick(&panel, Qt::Key_Up);
    QCOMPARE(focus.count(), 2);
    QKeyEvent over(QEvent::ShortcutOverride, Qt::Key_Up, Qt::NoModifier);
    over.setAccepted(false);
    QCoreApplication::sendEvent(&panel, &over);
    QVERIFY(over.isAccepted());                      // the pitch nudge shortcut must not take it

    QTemporaryDir dir;
    QFile f(dir.filePath("a.wav"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    const int frames = 44100;
    QByteArray h;
    QDataStream s(&h, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData("RIFF", 4); s << quint32(36 + frames * 4); s.writeRawData("WAVEfmt ", 8);
    s << quint32(16) << quint16(1) << quint16(2) << quint32(44100) << quint32(44100 * 4) << quint16(4) << quint16(16);
    s.writeRawData("data", 4); s << quint32(frames * 4);
    f.write(h);
    f.write(QByteArray(frames * 4, 0));
    f.close();
    TrackInfo info;
    info.title = "t";
    info.audioPath = dir.filePath("a.wav");
    info.fields.append({QStringLiteral("BPM"), QStringLiteral("124")});
    if (!panel.loadTrack(info))
        QSKIP("cannot load a wav here");
    QVERIFY(panel.wantsPlayerKey(&space));
    QVERIFY(!panel.wantsPlayerKey(&letter));
    QVERIFY(panel.handlePlayerKey(&pgDn));
    QVERIFY(pgDn.isAccepted());
    QVERIFY(!panel.quantizeButton()->isChecked());
    QTest::keyClick(&panel, Qt::Key_Q);
    QVERIFY(panel.quantizeButton()->isChecked());
}

void TestPlayerQuantize::cueKeys()
{
    QList<AnalysisCue> cues;
    for (int hot : {0, 1, 3}) {                      // a memory point, hot cue A and hot cue C
        AnalysisCue c;
        c.hotCue = hot;
        c.timeMs = quint32(1000 * (hot + 1));
        cues.append(c);
    }
    QVERIFY(PlayerPanel::hotCueForKey(cues, Qt::Key_A));
    QCOMPARE(PlayerPanel::hotCueForKey(cues, Qt::Key_A)->timeMs, quint32(2000));
    QVERIFY(!PlayerPanel::hotCueForKey(cues, Qt::Key_B));         // no hot cue B
    QCOMPARE(PlayerPanel::hotCueForKey(cues, Qt::Key_C)->timeMs, quint32(4000));
    QVERIFY(!PlayerPanel::hotCueForKey(cues, Qt::Key_D));
    QVERIFY(!PlayerPanel::hotCueForKey(cues, Qt::Key_Q));

    PlayerPanel panel;                               // nothing loaded: the keys stay with the application (B copies the BPM)
    QKeyEvent over(QEvent::ShortcutOverride, Qt::Key_B, Qt::NoModifier);
    over.setAccepted(false);
    QCoreApplication::sendEvent(&panel, &over);
    QVERIFY(!over.isAccepted());
}

void TestPlayerQuantize::bpmLock()
{
    QTemporaryDir dir;
    for (const char* name : {"a.wav", "b.wav"}) {
        QFile f(dir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        const int frames = 44100;
        QByteArray h;
        QDataStream s(&h, QIODevice::WriteOnly);
        s.setByteOrder(QDataStream::LittleEndian);
        s.writeRawData("RIFF", 4); s << quint32(36 + frames * 4); s.writeRawData("WAVEfmt ", 8);
        s << quint32(16) << quint16(1) << quint16(2) << quint32(44100) << quint32(44100 * 4) << quint16(4) << quint16(16);
        s.writeRawData("data", 4); s << quint32(frames * 4);
        f.write(h);
        f.write(QByteArray(frames * 4, 0));
    }
    auto info = [&](const char* file, const QString& bpm) {
        TrackInfo t;
        t.title = file;
        t.audioPath = dir.filePath(file);
        if (!bpm.isEmpty())
            t.fields.append({QStringLiteral("BPM"), bpm});
        return t;
    };
    PlayerPanel panel;
    QVERIFY(!panel.bpmLocked());                     // off by default
    if (!panel.loadTrack(info("a.wav", "124")))
        QSKIP("cannot load a wav here");

    panel.loadTrack(info("b.wav", "100"));           // unlocked: the new track plays at its own BPM
    QCOMPARE(panel.targetBpm(), 100.0);

    panel.setTargetBpm(104.0);
    panel.bpmLockButton()->click();
    QVERIFY(panel.bpmLocked());
    panel.loadTrack(info("a.wav", "124"));
    QCOMPARE(panel.originalBpm(), 124.0);
    QCOMPARE(panel.targetBpm(), 104.0);              // the current BPM is kept
    QCOMPARE(panel.player()->tempo(), 104.0 / 124.0);

    panel.loadTrack(info("b.wav", "40"));            // far outside the usual range: the panel does not clamp (the time stretcher itself stops at 2x)
    QCOMPARE(panel.targetBpm(), 104.0);
    QCOMPARE(panel.player()->tempo(), 2.0);

    panel.loadTrack(info("a.wav", QString()));       // no BPM: normal speed, the lock stays on and remembers 104
    QVERIFY(panel.bpmLocked());
    QCOMPARE(panel.player()->tempo(), 1.0);
    panel.loadTrack(info("b.wav", "100"));
    QCOMPARE(panel.targetBpm(), 104.0);

    panel.setTargetBpm(110.0);                       // other changes still work, and reset leaves the lock alone
    QCOMPARE(panel.targetBpm(), 110.0);
    panel.bpmResetButton()->click();
    QCOMPARE(panel.targetBpm(), 100.0);
    QVERIFY(panel.bpmLocked());
}

void TestPlayerQuantize::sequencerGridWait()
{
    PlayerPanel panel;
    QuantizeSettings settings;
    panel.setQuantizeSettings(&settings);
    FakeBeatSource seq;                              // 120 BPM: a beat is 500 ms
    seq.startNs = BeatSource::steadyNowNs() - qint64(130e6);       // 130 ms past a beat
    panel.setSequencerBeats(&seq);
    QCOMPARE(panel.sequencerGridWaitMs(), -1.0);     // Q is off
    panel.quantizeButton()->setEnabled(true);
    panel.quantizeButton()->setChecked(true);
    double w = panel.sequencerGridWaitMs();
    QVERIFY2(std::abs(w - 370.0) < 15.0, qPrintable(QString::number(w)));
    settings.setResolution(QuantizeSettings::Quarter);              // 125 ms steps: 130 ms past a beat, the next step is in 120 ms
    w = panel.sequencerGridWaitMs();
    QVERIFY2(std::abs(w - 120.0) < 15.0, qPrintable(QString::number(w)));
    seq.startNs = BeatSource::steadyNowNs() - qint64(2e6);          // a beat was 2 ms ago: go now
    settings.setResolution(QuantizeSettings::Beat);
    QCOMPARE(panel.sequencerGridWaitMs(), 0.0);
    seq.isRunning = false;
    QCOMPARE(panel.sequencerGridWaitMs(), -1.0);     // sequencer stopped: the player is not held
}

void TestPlayerQuantize::playWaitsForTheSequencerBeat()
{
    QTemporaryDir dir;
    QFile f(dir.filePath("a.wav"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    const int frames = 44100;                         // one second of silence
    QByteArray h;
    QDataStream s(&h, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData("RIFF", 4); s << quint32(36 + frames * 4); s.writeRawData("WAVEfmt ", 8);
    s << quint32(16) << quint16(1) << quint16(2) << quint32(44100) << quint32(44100 * 4) << quint16(4) << quint16(16);
    s.writeRawData("data", 4); s << quint32(frames * 4);
    f.write(h);
    f.write(QByteArray(frames * 4, 0));
    f.close();

    PlayerPanel panel;
    TrackInfo info;
    info.title = "t";
    info.audioPath = dir.filePath("a.wav");
    if (!panel.loadTrack(info))
        QSKIP("cannot load a wav here");
    QuantizeSettings settings;
    panel.setQuantizeSettings(&settings);
    FakeBeatSource seq;
    seq.startNs = BeatSource::steadyNowNs() - qint64(100e6);        // 100 ms past a beat, the next in 400 ms
    panel.setSequencerBeats(&seq);

    panel.playButton()->click();                      // Q off: starts at once
    QVERIFY(!panel.playPending());
    panel.playButton()->click();                      // pause
    panel.quantizeButton()->setChecked(true);
    panel.playButton()->click();
    QVERIFY(panel.playPending());                     // queued
    QVERIFY(panel.playButton()->text().contains(QString::fromUtf8("\u23f8")));   // shows pause: pressing cancels
    panel.playButton()->click();
    QVERIFY(!panel.playPending());
    panel.playButton()->click();
    QVERIFY(panel.playPending());
    QTRY_VERIFY_WITH_TIMEOUT(!panel.playPending(), 1000);       // started on the beat
}

QTEST_MAIN(TestPlayerQuantize)
#include "test_player_quantize.moc"
