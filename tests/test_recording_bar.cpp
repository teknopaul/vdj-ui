#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>

#include "audio/audio_recorder.h"
#include "config/config_manager.h"
#include "ui/level_meter_widget.h"
#include "ui/recording_bar.h"

using namespace vdj;

class TestRecordingBar : public QObject {
    Q_OBJECT
private slots:
    void idleStateGreyDotAndOnlyRecordEnabled()
    {
        AudioRecorder rec;
        RecordingBar bar(&rec, nullptr);
        QVERIFY(bar.recordButton()->isEnabled());
        QVERIFY(!bar.splitButton()->isEnabled());
        QVERIFY(!bar.stopButton()->isEnabled());
        QVERIFY(bar.dot()->styleSheet().contains("8a8a8a"));
        QVERIFY(bar.deviceCombo()->count() > 0);
        QVERIFY(bar.meter()->compact());
        QVERIFY(bar.recordButton()->text().contains("Record"));
        QVERIFY(bar.stopButton()->text().contains("Stop"));
    }
    void dotTurnsRedWhileRecording()
    {
        AudioRecorder rec;
        RecordingBar bar(&rec, nullptr);
        emit rec.recordingStarted("/tmp/x.mp3");
        QVERIFY(bar.dot()->styleSheet().contains("e53935"));
        QVERIFY(!bar.recordButton()->isEnabled());
        QVERIFY(bar.splitButton()->isEnabled());
        QVERIFY(bar.stopButton()->isEnabled());
        QVERIFY(!bar.deviceCombo()->isEnabled());
        emit rec.recordingStopped("/tmp/x.mp3");
        QVERIFY(bar.dot()->styleSheet().contains("8a8a8a"));
        QVERIFY(bar.recordButton()->isEnabled());
    }
    void compressorBoxFollowsThePlugin()
    {
        AudioRecorder rec;
        RecordingBar bar(&rec, nullptr);
        QCOMPARE(bar.compressorBox()->isEnabled(), AudioRecorder::compressorAvailable());
        if (!AudioRecorder::compressorAvailable()) QVERIFY(bar.compressorBox()->toolTip().contains("caps"));
        else { bar.compressorBox()->setChecked(true); QVERIFY(rec.compressor()); }
    }
    void folderChoiceIsKeptAndFilesAreNumbered()
    {
        QTemporaryDir dir;
        AudioRecorder rec;
        RecordingBar bar(&rec, nullptr);
        bar.setFolder(dir.path() + "/sub/folder");
        QCOMPARE(bar.folder(), QDir::cleanPath(dir.path() + "/sub/folder"));
        const QString a = RecordingBar::makeFilename(bar.folder());
        QVERIFY(a.startsWith(bar.folder() + "/session_"));
        QVERIFY(QDir(bar.folder()).exists());                      // created
        QFile f(a);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        const QString b = RecordingBar::makeFilename(bar.folder());
        QVERIFY(b != a);
        QVERIFY(b.endsWith("_2.mp3"));
    }
    void settingsRoundTripThroughTheConfig()
    {
        ConfigManager cfg;
        const QString old = cfg.loadValue("recording", "dir");
        const QString oldRecent = cfg.loadValue("recording", "recent_dirs");
        QTemporaryDir dir;
        {
            AudioRecorder rec;
            RecordingBar bar(&rec, &cfg);
            bar.setFolder(dir.path());
        }
        {
            AudioRecorder rec;
            RecordingBar bar(&rec, &cfg);
            QCOMPARE(bar.folder(), QDir::cleanPath(dir.path()));
        }
        cfg.saveValue("recording", "dir", old);
        cfg.saveValue("recording", "recent_dirs", oldRecent);
    }
};

QTEST_MAIN(TestRecordingBar)
#include "test_recording_bar.moc"
