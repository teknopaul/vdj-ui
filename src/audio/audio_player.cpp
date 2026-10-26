#include "audio_player.h"
#include <pulse/simple.h>
#include <pulse/error.h>
#include <algorithm>

namespace vdj {

namespace {
constexpr int kChunkFrames = 1024;
}

int splitAtTrigger(double posFrame, double tempo, double triggerFrame, int chunkFrames)
{
    if (tempo <= 0.0)
        return -1;
    const double n = (triggerFrame - posFrame) / tempo;
    if (n <= 0.0)
        return 0;
    if (n >= chunkFrames)
        return -1;
    return int(n);
}

AudioPlayer::AudioPlayer(QObject* parent)
    : QObject(parent)
{
    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, &AudioPlayer::tick);
    timer_.start();
    thread_ = std::thread([this] { run(); });
}

AudioPlayer::~AudioPlayer()
{
    quit_ = true;
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

bool AudioPlayer::load(const QString& path, QString* error)
{
    stop();
    DecodedAudio audio = decodeAudioFile(path, error);
    if (audio.isEmpty())
        return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // the worker never touches the source while stopped, but make the swap atomic with the stretcher anyway
        pendingJump_ = false;
        decoded_ = std::move(audio);
        frames_ = decoded_.frames();
        stretcher_.setSource(decoded_.samples.constData(), frames_);
    }
    emit positionChanged(0.0);
    return true;
}

void AudioPlayer::unload()
{
    stop();
    std::lock_guard<std::mutex> lock(mutex_);
    pendingJump_ = false;
    stretcher_.setSource(nullptr, 0);
    decoded_ = DecodedAudio();
    frames_ = 0;
}

double AudioPlayer::positionMs() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stretcher_.position() * 1000.0 / DecodedAudio::kSampleRate;
}

double AudioPlayer::tempo() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stretcher_.tempo();
}

void AudioPlayer::setDevice(const QString& sinkName)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (device_ == sinkName)
            return;
        device_ = sinkName;
        deviceChanged_ = true;
    }
    wake_.notify_all();
}

QString AudioPlayer::device() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return device_;
}

void AudioPlayer::play()
{
    if (!isLoaded())
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ended_ || stretcher_.atEnd())
            stretcher_.seek(0);
        ended_ = false;
        state_ = Playing;
    }
    wake_.notify_all();
    tick();
}

bool AudioPlayer::hasPendingJump() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pendingJump_;
}

void AudioPlayer::cancelJump()
{
    std::lock_guard<std::mutex> lock(mutex_);
    pendingJump_ = false;
}

void AudioPlayer::seekAtMs(double triggerSourceMs, double targetMs)
{
    bool now = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const double toFrames = DecodedAudio::kSampleRate / 1000.0;
        const double trigger = triggerSourceMs * toFrames;
        now = state_ != Playing || trigger <= stretcher_.position();
        if (!now) {
            pendingJump_ = true;
            jumpTriggerFrame_ = trigger;
            jumpTargetFrame_ = targetMs * toFrames;
        }
    }
    if (now)
        seekMs(targetMs);
    else
        wake_.notify_all();
}

void AudioPlayer::pause()
{
    cancelJump();
    if (state_ == Playing)
        state_ = Paused;
    tick();
}

void AudioPlayer::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = Stopped;
        ended_ = false;
        pendingJump_ = false;
        stretcher_.seek(0);
    }
    tick();
    emit positionChanged(0.0);
}

void AudioPlayer::seekMs(double ms)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stretcher_.seek(ms * DecodedAudio::kSampleRate / 1000.0);
        ended_ = false;
        pendingJump_ = false;
    }
    emit positionChanged(positionMs());
}

void AudioPlayer::setTempo(double ratio)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pendingJump_ = false;
    stretcher_.setTempo(ratio);
}

void AudioPlayer::tick()
{
    const State s = state_.load();
    if (s != reported_) {
        reported_ = s;
        emit stateChanged(int(s));
    }
    if (failed_.exchange(false)) {
        QString msg;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            msg = failure_;
        }
        emit deviceError(msg);
    }
    if (jumped_.exchange(false)) {
        double target;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            target = jumpTargetFrame_ * 1000.0 / DecodedAudio::kSampleRate;
        }
        emit jumped(target);
    }
    if (ended_.exchange(false)) {
        state_ = Stopped;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stretcher_.seek(0);
        }
        reported_ = Stopped;
        emit stateChanged(int(Stopped));
        emit positionChanged(0.0);
        emit finished();
        return;
    }
    if (s == Playing)
        emit positionChanged(positionMs());
}

// Worker thread: while Playing render a chunk and push it to the sound server (the blocking write paces us).
void AudioPlayer::run()
{
    pa_simple* pa = nullptr;
    QString openDevice;
    std::vector<qint16> chunk(size_t(kChunkFrames) * DecodedAudio::kChannels);

    auto closeStream = [&] {
        if (pa) {
            pa_simple_free(pa);
            pa = nullptr;
        }
    };

    while (!quit_) {
        QString wanted;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(200), [&] {
                return quit_ || state_ == Playing || deviceChanged_;
            });
            if (quit_)
                break;
            if (deviceChanged_) {
                deviceChanged_ = false;
                closeStream();               // reopened below if still playing
            }
            wanted = device_;
            if (state_ != Playing) {
                if (pa && state_ == Stopped)
                    closeStream();           // release the device while idle
                continue;
            }
            int head = pendingJump_
                ? splitAtTrigger(stretcher_.position(), stretcher_.tempo(), jumpTriggerFrame_, kChunkFrames)
                : -1;
            if (head < 0) {
                stretcher_.render(chunk.data(), kChunkFrames);
            } else {
                if (head > 0)
                    stretcher_.render(chunk.data(), head);
                stretcher_.seek(jumpTargetFrame_);
                stretcher_.render(chunk.data() + size_t(head) * DecodedAudio::kChannels, kChunkFrames - head);
                pendingJump_ = false;
                jumped_ = true;
            }
            if (stretcher_.atEnd())
                ended_ = true;
        }

        if (fx_)
            fx_->process(chunk.data(), int(chunk.size() / DecodedAudio::kChannels));

        if (!pa || openDevice != wanted) {
            closeStream();
            pa_sample_spec spec;
            spec.format = PA_SAMPLE_S16LE;
            spec.rate = DecodedAudio::kSampleRate;
            spec.channels = DecodedAudio::kChannels;
            pa_buffer_attr attr;
            attr.maxlength = uint32_t(-1);
            attr.tlength = pa_usec_to_bytes(60000, &spec);
            attr.prebuf = uint32_t(-1);
            attr.minreq = uint32_t(-1);
            attr.fragsize = uint32_t(-1);
            int err = 0;
            const QByteArray dev = wanted.toUtf8();
            pa = pa_simple_new(nullptr, "vdj-ui", PA_STREAM_PLAYBACK, dev.isEmpty() ? nullptr : dev.constData(),
                               "track preview", &spec, nullptr, &attr, &err);
            if (!pa) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    failure_ = QStringLiteral("cannot open audio output: %1").arg(QString::fromUtf8(pa_strerror(err)));
                    state_ = Paused;
                }
                failed_ = true;
                continue;
            }
            openDevice = wanted;
        }
        int err = 0;
        if (pa_simple_write(pa, chunk.data(), chunk.size() * sizeof(qint16), &err) < 0) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                failure_ = QStringLiteral("audio output error: %1").arg(QString::fromUtf8(pa_strerror(err)));
                state_ = Paused;
            }
            failed_ = true;
            closeStream();
        }
    }
    if (pa) {
        pa_simple_drain(pa, nullptr);
        closeStream();
    }
}

} // namespace vdj
