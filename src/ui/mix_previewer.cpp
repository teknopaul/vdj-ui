#include "mix_previewer.h"

#include "mix_filters.h"

#include <algorithm>

namespace vdj {

using namespace trakcrate;

namespace {
constexpr double kRate = 44100.0;
constexpr long long kSinkLatencyFrames = 5300;          // what the sound server holds back; the position shown is that much behind what was written
constexpr long long kPrefetchFrames = 60 * 44100;
}

// Never blocks the audio thread: a track that is not prepared yet is silence and gets queued.
class MixPreviewer::Source : public TrackSource {
public:
    Source(TrackCache* c, std::atomic<int>* missing) : cache_(c), missing_(missing) {}
    std::shared_ptr<const DecodedPcm> edited(int i) override
    {
        if (!cache_->isReady(i)) {
            cache_->prefetch(i);
            *missing_ = i + 1;
            return nullptr;
        }
        return cache_->edited(i);
    }
    void release(int i) override { cache_->release(i); }

private:
    TrackCache* cache_;
    std::atomic<int>* missing_;
};

MixPreviewer::MixPreviewer(StretchCache* cache, QObject* parent, StreamSinkFactory factory)
    : QObject(parent), stretchCache_(cache), player_(new StreamPlayer(this, std::move(factory))), timer_(new QTimer(this))
{
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, &MixPreviewer::tick);
    connect(player_, &StreamPlayer::finished, this, [this] { if (state_ == Playing) { emit status(QStringLiteral("End of the mix")); stop(); } });
    connect(player_, &StreamPlayer::failed, this, [this](const QString& m) { emit status(m); stop(); });
}

MixPreviewer::~MixPreviewer() { teardown(); }

void MixPreviewer::setMix(const Mix& mix, const std::vector<TrackFacts>& facts)
{
    mix_ = mix;
    facts_ = facts;
}

void MixPreviewer::setState(State s)
{
    if (s == state_) return;
    state_ = s;
    emit stateChanged(int(s));
}

void MixPreviewer::teardown()
{
    timer_->stop();
    player_->stop();
    if (cache_) cache_->cancel();
    engine_.reset();
    source_.reset();
    cache_.reset();
}

void MixPreviewer::toggle(double fromMs)
{
    if (state_ == Stopped) start(fromMs);
    else stop();
}

void MixPreviewer::stop()
{
    teardown();
    setState(Stopped);
}

void MixPreviewer::start(double fromMs)
{
    teardown();
    if (mix_.tracks.empty() || facts_.size() != mix_.tracks.size()) {
        emit status(QStringLiteral("Nothing to play"));
        setState(Stopped);
        return;
    }
    const std::string bad = validateMix(mix_, facts_);
    if (!bad.empty()) {
        emit status(QString::fromStdString(bad));
        setState(Stopped);
        return;
    }
    cache_ = std::make_unique<TrackCache>(mix_, facts_, stretchCache_);
    source_ = std::make_unique<Source>(cache_.get(), &missing_);
    engine_ = std::make_unique<MixEngine>(mix_, facts_, source_.get());
    MixFilterReport report;
    engine_->setProcessors(buildLadspaProcessors(mix_.filters, &report));
    if (!report.skipped.empty()) emit status(QStringLiteral("Filter skipped: %1").arg(report.skipped.front()));
    startFrame_ = std::clamp<long long>(llround(fromMs * kRate / 1000.0), 0, std::max<long long>(0, engine_->totalFrames() - 1));
    pos_ = startFrame_;
    missing_ = 0;
    underruns_ = 0;
    waited_ = 0;
    // The track that sounds at the start must be ready, so the first note is not lost; the others follow in the background.
    preparingFor_ = 0;
    for (int i = 0; i < int(mix_.tracks.size()); ++i) {
        const long long s = engine_->trackStartFrame(i);
        if (s <= startFrame_ && startFrame_ < s + engine_->effective()[size_t(i)].editedFrames) preparingFor_ = i;
    }
    cache_->prefetch(preparingFor_);
    setState(Preparing);
    emit status(QStringLiteral("Preparing track %1...").arg(preparingFor_ + 1));
    timer_->start();
}

void MixPreviewer::begin()
{
    MixEngine* engine = engine_.get();
    const long long total = engine->totalFrames() + engine->latencyFrames();
    const bool ok = player_->play([this, engine, total](float* out, int frames) {
        const long long p = pos_;
        engine->render(p, frames, out);
        pos_ = p + frames;
        return pos_ < total;
    });
    if (!ok) {
        emit status(QStringLiteral("Audio output is busy"));
        stop();
        return;
    }
    setState(Playing);
    emit status(QStringLiteral("Playing"));
}

void MixPreviewer::tick()
{
    if (state_ == Preparing) {
        if (cache_->isReady(preparingFor_)) {
            begin();
        } else if (!cache_->lastError().empty()) {
            emit status(QString::fromStdString(cache_->lastError()));
            stop();
        } else if (++waited_ % 30 == 0) {
            emit status(QStringLiteral("Preparing track %1... (%2 s)").arg(preparingFor_ + 1).arg(waited_ / 30));
        }
        return;
    }
    if (state_ != Playing) return;
    const long long now = std::max(startFrame_, startFrame_ + player_->positionFrames() - kSinkLatencyFrames);
    emit positionChanged(double(now) * 1000.0 / kRate);
    for (int i = 0; i < int(mix_.tracks.size()); ++i) {
        const long long s = engine_->trackStartFrame(i);
        if (s < now + kPrefetchFrames && s + engine_->effective()[size_t(i)].editedFrames > now) cache_->prefetch(i);
    }
    if (const int m = missing_.exchange(0)) {
        ++underruns_;
        emit status(QStringLiteral("Track %1 is not ready yet: silence until it is").arg(m));
    }
}

} // namespace vdj
