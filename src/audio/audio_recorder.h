#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

#include <QObject>
#include <QStringList>

class QProcess;

namespace vdj {

class RecordThread;

class AudioRecorder : public QObject {
    Q_OBJECT
public:
    explicit AudioRecorder(QObject* parent = nullptr);
    ~AudioRecorder() override;

    // Enumerate ALSA capture devices (PCM, input-capable).
    static QStringList availableDevices();

    bool start(const QString& alsaDevice, const QString& outputPath);
    void stop();
    // Close current MP3, start a new one (same ALSA device, new path).
    void split(const QString& newOutputPath);

    // Level metering without recording: opens `alsaDevice` for capture and only reports levels() (nothing is written). Stopped by
    // stopMonitor(), and by start() because most sound cards cannot be opened twice; the monitor does not resume by itself.
    void startMonitor(const QString& alsaDevice);
    void stopMonitor();
    bool isMonitoring() const { return monitor_ != nullptr; }

    bool isRecording() const { return recording_; }
    QString currentFile() const { return currentFile_; }

signals:
    void levelChanged(float rms);
    // linear 0..1 per channel for one block of about 23 ms, while recording or monitoring
    void levels(float peakL, float peakR, float rmsL, float rmsR);
    void monitorFailed(const QString& msg);       // the device could not be opened for metering (no pop up: it is only a meter)
    void recordingStarted(const QString& path);
    void recordingStopped(const QString& path);
    void errorOccurred(const QString& msg);

private slots:
    void onChunkReady(const QByteArray& data, float rms);
    void onMonitorLevels(float peakL, float peakR, float rmsL, float rmsR);

private:
    void startLame(const QString& outputPath);
    void stopLame();
    void stopThread();

    RecordThread* thread_      = nullptr;
    RecordThread* monitor_     = nullptr;
    QProcess*     lame_        = nullptr;
    bool          recording_   = false;
    QString       currentFile_;
    QString       currentDevice_;
};

} // namespace vdj
#endif
