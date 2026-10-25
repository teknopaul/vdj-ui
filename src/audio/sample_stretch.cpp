#include "sample_stretch.h"
#include "time_stretcher.h"

#include <algorithm>
#include <cmath>

namespace vdj {

int tempoPermille(double tempo)
{
    return int(std::lround(std::clamp(tempo, TimeStretcher::kMinTempo, TimeStretcher::kMaxTempo) * 1000.0));
}

std::shared_ptr<const Sample> stretchSample(const Sample& in, double tempo, qint64 exactFrames)
{
    tempo = std::clamp(tempo, TimeStretcher::kMinTempo, TimeStretcher::kMaxTempo);
    const qint64 srcFrames = in.audio.frames();
    auto out = std::make_shared<Sample>();
    out->path = in.path;
    out->name = in.name;
    out->bpm = in.bpm > 0 ? in.bpm * tempo : 0.0;
    const qint64 wanted = exactFrames > 0 ? exactFrames : qint64(std::llround(double(srcFrames) / tempo));
    out->audio.samples.resize(int(wanted * DecodedAudio::kChannels));
    if (srcFrames == 0)
        return out;

    TimeStretcher ts;
    ts.setSource(in.audio.samples.constData(), srcFrames);
    ts.setTempo(tempo);
    constexpr int kChunk = 1024;
    for (qint64 done = 0; done < wanted; done += kChunk) {
        const int n = int(std::min<qint64>(kChunk, wanted - done));
        ts.render(out->audio.samples.data() + done * DecodedAudio::kChannels, n);   // silence past the end of the source
    }
    return out;
}

std::shared_ptr<const Sample> StretchCache::peek(const QString& path, int permille) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(Key(path, permille));
    return it == entries_.end() ? nullptr : it->second;
}

void StretchCache::put(const QString& path, int permille, std::shared_ptr<const Sample> s)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const Key k(path, permille);
    if (entries_.count(k))
        return;
    bytes_ += qint64(s->audio.samples.size()) * qint64(sizeof(qint16));
    entries_[k] = std::move(s);
    order_.push_back(k);
    while (bytes_ > maxBytes_ && order_.size() > 1) {
        auto it = entries_.find(order_.front());
        bytes_ -= qint64(it->second->audio.samples.size()) * qint64(sizeof(qint16));
        entries_.erase(it);
        order_.pop_front();
    }
}

std::shared_ptr<const Sample> StretchCache::get(const Sample& base, int permille, qint64 exactFrames)
{
    if (auto hit = peek(base.path, permille))
        return hit;
    auto s = stretchSample(base, double(permille) / 1000.0, exactFrames);
    put(base.path, permille, s);
    return peek(base.path, permille);
}

void StretchCache::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    order_.clear();
    bytes_ = 0;
}

int StretchCache::count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return int(entries_.size());
}

} // namespace vdj
