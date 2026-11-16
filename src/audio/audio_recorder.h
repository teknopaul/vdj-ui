#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

#include <QObject>
#include <QStringList>
#include <memory>

class QProcess;

namespace vdj {

class RecordThread;
class LadspaEffect;

class AudioRecorder : public QObject {
    Q_OBJECT
public:
    explicit AudioRecorder(QObject* parent = nullptr);
    ~AudioRecorder() override;

    // Enumerate ALSA capture devices (PCM, input-capable).
    static QStringList availableDevices();
    // The mp3 encoder (`lame`, a separate package) is on the PATH; *path gets its location.
    static bool lameAvailable(QString* path = nullptr);

    bool start(const QString& alsaDevice, const QString& outputPath);
    void stop();
    // Close current MP3, start a new one (same ALSA device, new path).
    void split(const QString& newOutputPath);

    // Level metering without recording: opens `alsaDevice` for capture and only reports levels() (nothing is written). Stopped by
    // stopMonitor(), and by start() because most sound cards cannot be opened twice; the monitor does not resume by itself.
    void startMonitor(const QString& alsaDevice);
    void stopMonitor();
    bool isMonitoring() const { return monitor_ != nullptr; }

    // Run the audio through a LADSPA compressor before it is encoded (takes effect at the next start / split). The meter still shows the input.
    // When no plugin is found recording goes on uncompressed and errorOccurred() says why.
    void setCompressor(bool on) { compressor_ = on; }
    bool compressor() const { return compressor_; }
    static bool compressorAvailable();

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
    bool          compressor_  = false;
    std::shared_ptr<LadspaEffect> effect_;
    QString       currentFile_;
    QString       currentDevice_;
};

} // namespace vdj
#endif
