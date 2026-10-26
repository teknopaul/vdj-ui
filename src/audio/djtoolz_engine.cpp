#include "djtoolz_engine.h"
#include "beat_source.h"

#include <pulse/error.h>
#include <pulse/simple.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace vdj {

DjtoolzEngine::DjtoolzEngine(QObject* parent)
    : QObject(parent)
{
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, &DjtoolzEngine::tick);
    timer_.start();
    thread_ = std::thread([this] { run(); });
}

DjtoolzEngine::~DjtoolzEngine()
{
    quit_ = true;
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void DjtoolzEngine::start()
{
    running_ = true;
    wake_.notify_all();
}

void DjtoolzEngine::stop()
{
    running_ = false;
    mixer_.stopAll();
    wake_.notify_all();
}

void DjtoolzEngine::setDevice(const QString& sinkName)
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

QString DjtoolzEngine::device() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return device_;
}

void DjtoolzEngine::setLatencyMs(double ms)
{
    ms = std::clamp(ms, 5.0, 500.0);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (latencyMs_.load() == ms)
            return;
        latencyMs_ = ms;
        deviceChanged_ = true;                               // the stream is re-opened with the new buffer size
    }
    wake_.notify_all();
}

void DjtoolzEngine::tick()
{
    if (failed_.exchange(false)) {
        QString msg;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            msg = failure_;
        }
        emit deviceError(msg);
    }
}

// Worker thread. The timeline of "heard" time advances by exactly the frames written, anchored to the steady clock
// when the stream opens; a slow correction keeps it from drifting against the clock the beat source uses.
void DjtoolzEngine::run()
{
    pa_simple* pa = nullptr;
    QString openDevice;
    std::vector<qint16> block(size_t(kBlockFrames) * DecodedAudio::kChannels);
    qint64 heardNs = 0;                                      // when the next block will be heard
    bool anchored = false;

    auto closeStream = [&] {
        if (pa) {
            pa_simple_free(pa);
            pa = nullptr;
        }
        anchored = false;
    };
    auto fail = [&](const QString& what, int err) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            failure_ = QStringLiteral("%1: %2").arg(what, QString::fromUtf8(pa_strerror(err)));
        }
        failed_ = true;
        running_ = false;
        closeStream();
    };

    while (!quit_) {
        QString wanted;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(200), [&] { return quit_ || running_ || deviceChanged_; });
            if (quit_)
                break;
            if (deviceChanged_) {
                deviceChanged_ = false;
                closeStream();                               // reopened below if still running
            }
            wanted = device_;
            if (!running_) {
                if (pa)
                    closeStream();                           // release the device while idle
                continue;
            }
        }

        if (!pa || openDevice != wanted) {
            closeStream();
            pa_sample_spec spec;
            spec.format = PA_SAMPLE_S16LE;
            spec.rate = DecodedAudio::kSampleRate;
            spec.channels = DecodedAudio::kChannels;
            pa_buffer_attr attr;
            attr.maxlength = uint32_t(-1);
            attr.tlength = pa_usec_to_bytes(uint32_t(latencyMs_.load() * 1000.0), &spec);
            attr.prebuf = uint32_t(-1);
            attr.minreq = uint32_t(-1);
            attr.fragsize = uint32_t(-1);
            int err = 0;
            const QByteArray dev = wanted.toUtf8();
            pa = pa_simple_new(nullptr, "vdj-ui", PA_STREAM_PLAYBACK, dev.isEmpty() ? nullptr : dev.constData(),
                               "djtoolz", &spec, nullptr, &attr, &err);
            if (!pa) {
                fail(QStringLiteral("cannot open DJ Tools audio output"), err);
                continue;
            }
            openDevice = wanted;
        }

        const qint64 now = BeatSource::steadyNowNs();
        const qint64 wantNs = now + qint64(latencyMs_.load() * 1e6);
        if (!anchored) {
            heardNs = wantNs;
            anchored = true;
        } else {
            const qint64 err = wantNs - heardNs;
            if (err > 20000000 || err < -20000000)
                heardNs = wantNs;                            // an underrun or a stall: re-anchor
            else
                heardNs += err / 100;                        // drift between the sound clock and the steady clock
        }
        mixer_.render(block.data(), kBlockFrames, heardNs);
        if (fx_)
            fx_->process(block.data(), kBlockFrames);
        heardNs += qint64(double(kBlockFrames) * 1e9 / DecodedAudio::kSampleRate);

        int err = 0;
        if (pa_simple_write(pa, block.data(), block.size() * sizeof(qint16), &err) < 0)
            fail(QStringLiteral("DJ Tools audio output error"), err);
    }
    if (pa) {
        pa_simple_drain(pa, nullptr);
        closeStream();
    }
}

} // namespace vdj
