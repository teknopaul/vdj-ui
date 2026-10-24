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

    bool isRecording() const { return recording_; }
    QString currentFile() const { return currentFile_; }

signals:
    void levelChanged(float rms);
    void recordingStarted(const QString& path);
    void recordingStopped(const QString& path);
    void errorOccurred(const QString& msg);

private slots:
    void onChunkReady(const QByteArray& data, float rms);

private:
    void startLame(const QString& outputPath);
    void stopLame();
    void stopThread();

    RecordThread* thread_      = nullptr;
    QProcess*     lame_        = nullptr;
    bool          recording_   = false;
    QString       currentFile_;
    QString       currentDevice_;
};

} // namespace vdj
#endif
