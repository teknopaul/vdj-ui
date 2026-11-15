#include "stream_player.h"

#include <pulse/error.h>
#include <pulse/simple.h>

#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <vector>

namespace vdj {

namespace {

class PulseSink : public StreamSink {
public:
    explicit PulseSink(pa_simple* pa) : pa_(pa) {}
    ~PulseSink() override { if (pa_) pa_simple_free(pa_); }
    bool write(const float* stereo, int frames, QString* err) override
    {
        int e = 0;
        if (pa_simple_write(pa_, stereo, size_t(frames) * 2 * sizeof(float), &e) < 0) {
            if (err) *err = QStringLiteral("audio output error: %1").arg(QString::fromUtf8(pa_strerror(e)));
            return false;
        }
        return true;
    }
    void drain() override { int e = 0; pa_simple_drain(pa_, &e); }

private:
    pa_simple* pa_;
};

} // namespace

std::unique_ptr<StreamSink> openPulseSink(QString* err)
{
    pa_sample_spec spec;
    spec.format = PA_SAMPLE_FLOAT32LE;
    spec.rate = 44100;
    spec.channels = 2;
    pa_buffer_attr attr;
    attr.maxlength = uint32_t(-1);
    attr.tlength = pa_usec_to_bytes(120000, &spec);
    attr.prebuf = uint32_t(-1);
    attr.minreq = uint32_t(-1);
    attr.fragsize = uint32_t(-1);
    int e = 0;
    pa_simple* pa = pa_simple_new(nullptr, "vdj-ui", PA_STREAM_PLAYBACK, nullptr, "studio mix preview", &spec, nullptr, &attr, &e);
    if (!pa) {
        if (err) *err = QStringLiteral("cannot open audio output: %1").arg(QString::fromUtf8(pa_strerror(e)));
        return nullptr;
    }
    return std::make_unique<PulseSink>(pa);
}

StreamPlayer::StreamPlayer(QObject* parent, StreamSinkFactory factory) : QObject(parent), factory_(std::move(factory)) {}

StreamPlayer::~StreamPlayer() { stop(); }

bool StreamPlayer::play(Fill fill)
{
    if (playing_) return false;
    if (thread_.joinable()) thread_.join();
    quit_ = false;
    written_ = 0;
    playing_ = true;
    thread_ = std::thread([this, f = std::move(fill)]() mutable { run(std::move(f)); });
    return true;
}

void StreamPlayer::stop()
{
    quit_ = true;
    if (thread_.joinable()) thread_.join();
    playing_ = false;
}

void StreamPlayer::run(Fill fill)
{
    QString err;
    std::unique_ptr<StreamSink> sink = factory_ ? factory_(&err) : nullptr;
    if (!sink) {
        playing_ = false;
        const QString msg = err.isEmpty() ? QStringLiteral("cannot open audio output") : err;
        QMetaObject::invokeMethod(this, [this, msg] { emit failed(msg); }, Qt::QueuedConnection);
        return;
    }
    std::vector<float> buf(size_t(kBlockFrames) * 2);
    bool more = true;
    bool cancelled = false;
    while (more) {
        if (quit_) { cancelled = true; break; }
        std::fill(buf.begin(), buf.end(), 0.0f);
        more = fill(buf.data(), kBlockFrames);
        for (float& v : buf) v = std::clamp(v, -1.0f, 1.0f);
        if (!sink->write(buf.data(), kBlockFrames, &err)) {
            playing_ = false;
            QMetaObject::invokeMethod(this, [this, err] { emit failed(err); }, Qt::QueuedConnection);
            return;
        }
        written_ += kBlockFrames;
    }
    if (!cancelled) sink->drain();
    playing_ = false;
    if (!cancelled) QMetaObject::invokeMethod(this, [this] { emit finished(); }, Qt::QueuedConnection);
}

} // namespace vdj
