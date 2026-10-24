#include "audio_recorder.h"

#include <QAtomicInt>
#include <QDebug>
#include <QProcess>
#include <QThread>

#include <alsa/asoundlib.h>
#include <cmath>
#include <cstring>

namespace vdj {

// -------------------------------------------------------------------------
// RecordThread: blocking ALSA PCM capture on a dedicated thread.
// Emits chunkReady(data, rms) for each period read.
// -------------------------------------------------------------------------
class RecordThread : public QThread {
    Q_OBJECT
public:
    explicit RecordThread(const QString& device, QObject* parent = nullptr)
        : QThread(parent), device_(device) {}

    void requestStop() { stop_.storeRelaxed(1); }

signals:
    void chunkReady(QByteArray data, float rms);
    void errorOccurred(QString msg);

protected:
    void run() override
    {
        snd_pcm_t* pcm = nullptr;
        int err = snd_pcm_open(&pcm, device_.toLatin1().constData(),
                               SND_PCM_STREAM_CAPTURE, 0);
        if (err < 0) {
            emit errorOccurred(
                QStringLiteral("Cannot open PCM '%1': %2")
                    .arg(device_, QString::fromLatin1(snd_strerror(err))));
            return;
        }

        snd_pcm_hw_params_t* hwp;
        snd_pcm_hw_params_alloca(&hwp);
        snd_pcm_hw_params_any(pcm, hwp);
        snd_pcm_hw_params_set_access(pcm, hwp, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm, hwp, SND_PCM_FORMAT_S16_LE);
        snd_pcm_hw_params_set_channels(pcm, hwp, 2);
        unsigned int rate = 44100;
        snd_pcm_hw_params_set_rate_near(pcm, hwp, &rate, nullptr);
        snd_pcm_uframes_t period = 1024;
        snd_pcm_hw_params_set_period_size_near(pcm, hwp, &period, nullptr);
        if ((err = snd_pcm_hw_params(pcm, hwp)) < 0) {
            emit errorOccurred(
                QStringLiteral("PCM hw_params failed: %1")
                    .arg(QString::fromLatin1(snd_strerror(err))));
            snd_pcm_close(pcm);
            return;
        }

        const int frameBytes = 4;  // 2 channels * 2 bytes / sample
        QByteArray buf(static_cast<int>(period) * frameBytes, '\0');

        while (!stop_.loadRelaxed()) {
            snd_pcm_sframes_t n = snd_pcm_readi(
                pcm, buf.data(), static_cast<snd_pcm_uframes_t>(period));
            if (n < 0) {
                if (stop_.loadRelaxed()) break;
                snd_pcm_recover(pcm, static_cast<int>(n), 1);
                continue;
            }
            if (n == 0) continue;

            const auto* s = reinterpret_cast<const int16_t*>(buf.constData());
            int count = static_cast<int>(n) * 2;
            double sumSq = 0.0;
            for (int i = 0; i < count; ++i) {
                double v = s[i] / 32768.0;
                sumSq += v * v;
            }
            float rms = static_cast<float>(std::sqrt(sumSq / count));
            emit chunkReady(buf.left(static_cast<int>(n) * frameBytes), rms);
        }

        snd_pcm_drop(pcm);
        snd_pcm_close(pcm);
    }

private:
    QString    device_;
    QAtomicInt stop_{0};
};

// -------------------------------------------------------------------------
// AudioRecorder
// -------------------------------------------------------------------------

AudioRecorder::AudioRecorder(QObject* parent) : QObject(parent) {}

AudioRecorder::~AudioRecorder() { stop(); }

QStringList AudioRecorder::availableDevices()
{
    QStringList result;
    void** hints;
    if (snd_device_name_hint(-1, "pcm", &hints) < 0)
        return result;
    for (void** p = hints; *p; ++p) {
        char* io = snd_device_name_get_hint(*p, "IOID");
        if (io) {
            bool isOut = (std::strcmp(io, "Output") == 0);
            free(io);
            if (isOut) continue;
        }
        char* name = snd_device_name_get_hint(*p, "NAME");
        if (name) { result << QString::fromLatin1(name); free(name); }
    }
    snd_device_name_free_hint(hints);
    return result;
}

bool AudioRecorder::start(const QString& alsaDevice, const QString& outputPath)
{
    if (recording_) stop();

    startLame(outputPath);
    if (!lame_ || lame_->state() != QProcess::Running) {
        emit errorOccurred(
            QStringLiteral("lame failed to start — install the 'lame' package."));
        stopLame();
        return false;
    }

    currentDevice_ = alsaDevice;
    thread_ = new RecordThread(alsaDevice, this);
    connect(thread_, &RecordThread::chunkReady,
            this, &AudioRecorder::onChunkReady, Qt::QueuedConnection);
    connect(thread_, &RecordThread::errorOccurred,
            this, &AudioRecorder::errorOccurred);
    thread_->start();

    recording_   = true;
    currentFile_ = outputPath;
    emit recordingStarted(outputPath);
    return true;
}

void AudioRecorder::stop()
{
    if (!recording_) return;
    recording_ = false;
    stopThread();
    stopLame();
    emit recordingStopped(currentFile_);
}

void AudioRecorder::split(const QString& newOutputPath)
{
    if (!recording_) { start(currentDevice_, newOutputPath); return; }

    // Stop capture thread (fast — at most one PCM period ≈ 23 ms).
    recording_ = false;
    stopThread();
    // Close lame stdin: lame will flush and finalise the current file.
    stopLame();
    emit recordingStopped(currentFile_);
    // Start fresh for the new file.
    start(currentDevice_, newOutputPath);
}

void AudioRecorder::onChunkReady(const QByteArray& data, float rms)
{
    if (lame_ && lame_->state() == QProcess::Running)
        lame_->write(data);
    emit levelChanged(rms);
}

void AudioRecorder::startLame(const QString& outputPath)
{
    lame_ = new QProcess(this);
    lame_->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    // lame: read raw signed 16-bit stereo PCM from stdin, write MP3 to outputPath.
    lame_->start(QStringLiteral("lame"),
                 {QStringLiteral("--silent"),
                  QStringLiteral("-r"),
                  QStringLiteral("--signed"),
                  QStringLiteral("-s"), QStringLiteral("44100"),
                  QStringLiteral("--bitwidth"), QStringLiteral("16"),
                  QStringLiteral("-m"), QStringLiteral("s"),
                  QStringLiteral("-B"), QStringLiteral("320"),
                  QStringLiteral("-"), outputPath});
    lame_->waitForStarted(3000);
}

void AudioRecorder::stopLame()
{
    if (!lame_) return;
    lame_->closeWriteChannel();   // send EOF → lame finishes encoding
    lame_->waitForFinished(8000); // wait up to 8 s for final flush
    lame_->deleteLater();
    lame_ = nullptr;
}

void AudioRecorder::stopThread()
{
    if (!thread_) return;
    thread_->requestStop();
    thread_->wait(3000);
    thread_->deleteLater();
    thread_ = nullptr;
}

} // namespace vdj

#include "audio_recorder.moc"
