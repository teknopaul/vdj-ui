#include "sample_cache.h"
#include "wav_info.h"

#include <QFileInfo>

namespace vdj {

std::shared_ptr<const Sample> SampleCache::get(const QString& path, QString* error)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(path);
        if (it != entries_.end()) {
            lru_.splice(lru_.begin(), lru_, it->second.lru);
            return it->second.sample;
        }
    }
    auto s = std::make_shared<Sample>();
    s->path = path;
    s->name = QFileInfo(path).fileName();
    s->audio = decodeAudioFile(path, error);
    if (s->audio.isEmpty())
        return nullptr;
    s->bpm = wavBpmFromFile(path);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(path);
    if (it != entries_.end())                        // another thread decoded it meanwhile
        return it->second.sample;
    lru_.push_front(path);
    entries_[path] = Entry{ s, lru_.begin() };
    bytes_ += sizeOf(*s);
    evictLocked();
    return s;
}

std::shared_ptr<const Sample> SampleCache::peek(const QString& path) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(path);
    if (it == entries_.end())
        return nullptr;
    lru_.splice(lru_.begin(), lru_, it->second.lru);
    return it->second.sample;
}

void SampleCache::evictLocked()
{
    while (bytes_ > maxBytes_ && lru_.size() > 1) {  // the newest stays even if it alone is over the limit
        const QString victim = lru_.back();
        lru_.pop_back();
        auto it = entries_.find(victim);
        bytes_ -= sizeOf(*it->second.sample);
        entries_.erase(it);
    }
}

void SampleCache::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    lru_.clear();
    bytes_ = 0;
}

int SampleCache::count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return int(entries_.size());
}

qint64 SampleCache::bytes() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_;
}

} // namespace vdj
