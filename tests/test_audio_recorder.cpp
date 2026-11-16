#include <QtTest>

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "audio/audio_recorder.h"

#include <sys/stat.h>

using namespace vdj;

// Capture devices that exist only in a private ALSA configuration: `silent` (the null plugin: zeros at once) and `stuck` (the file plugin
// reading a FIFO nobody writes to: the device opens and never delivers, the case that used to abort the program when it was stopped).
class TestAudioRecorder : public QObject {
    Q_OBJECT

    QTemporaryDir dir_;
    bool haveFilePlugin_ = false;

private slots:
    void initTestCase()
    {
        const QString fifo = dir_.filePath("never");
        ::mkfifo(fifo.toLocal8Bit().constData(), 0600);
        QFile f(dir_.filePath("asound.conf"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral("pcm.silent { type null }\n"
                               "pcm.stuck { type file\n slave.pcm \"silent\"\n file \"/dev/null\"\n infile \"%1\"\n format \"raw\" }\n").arg(fifo).toUtf8());
        f.close();
        qputenv("ALSA_CONFIG_PATH", dir_.filePath("asound.conf").toLocal8Bit());
        haveFilePlugin_ = true;
    }

    void silentDeviceDeliversLevelsAndStops()
    {
        AudioRecorder rec;
        QSignalSpy levels(&rec, &AudioRecorder::levels);
        QSignalSpy failed(&rec, &AudioRecorder::monitorFailed);
        rec.startMonitor("silent");
        QVERIFY(QTest::qWaitFor([&] { return levels.count() > 0 || failed.count() > 0; }, 3000));
        if (failed.count() > 0) QSKIP(qPrintable("null plugin not usable here: " + failed.at(0).at(0).toString()));
        rec.stopMonitor();
        QVERIFY(!rec.isMonitoring());
    }

    void deviceThatNeverDeliversCanBeStoppedAndDoesNotAbort()
    {
        if (!haveFilePlugin_) QSKIP("no file plugin");
        AudioRecorder rec;
        QSignalSpy failed(&rec, &AudioRecorder::monitorFailed);
        rec.startMonitor("stuck");
        QTest::qWait(300);
        if (failed.count() > 0) QSKIP(qPrintable("file plugin not usable here: " + failed.at(0).at(0).toString()));
        QElapsedTimer t;
        t.start();
        rec.stopMonitor();                                  // used to wait 3 s, then delete a running QThread = abort()
        QVERIFY2(t.elapsed() < 3000, qPrintable(QString::number(t.elapsed())));
        QVERIFY(!rec.isMonitoring());
        // and again, with the recorder destroyed while a monitor runs
        {
            AudioRecorder rec2;
            rec2.startMonitor("stuck");
            QTest::qWait(150);
        }
    }

    void startWithoutLameReportsAndStartsNothing()
    {
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", QByteArray("/nonexistent"));
        AudioRecorder rec;
        QSignalSpy err(&rec, &AudioRecorder::errorOccurred);
        const bool started = rec.start("silent", dir_.filePath("x.mp3"));
        qputenv("PATH", oldPath);
        QVERIFY(!started);
        QCOMPARE(err.count(), 1);
        QVERIFY(err.at(0).at(0).toString().contains("lame"));
        QVERIFY(!rec.isRecording());
    }

    void missingDeviceReportsInsteadOfCrashing()
    {
        AudioRecorder rec;
        QSignalSpy failed(&rec, &AudioRecorder::monitorFailed);
        rec.startMonitor("no_such_device_at_all");
        QVERIFY(QTest::qWaitFor([&] { return failed.count() > 0; }, 3000));
        QVERIFY(failed.at(0).at(0).toString().contains("no_such_device_at_all"));
        rec.stopMonitor();
    }
};

QTEST_MAIN(TestAudioRecorder)
#include "test_audio_recorder.moc"
