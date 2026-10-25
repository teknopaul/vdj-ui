#ifndef SAMPLE_STRETCH_H
#define SAMPLE_STRETCH_H

#include <QString>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include "sample_cache.h"

namespace vdj {

// A copy of the sample played `tempo` times as fast with the key kept (TimeStretcher): the length becomes length / tempo.
// tempo is clamped to TimeStretcher's 0.5 .. 2.0. When `exactFrames` > 0 the result is trimmed or padded with silence to exactly that
// many frames (a loop that must be precisely 4 beats long). Slow (the whole sample is rendered): call it off the audio thread.
std::shared_ptr<const Sample> stretchSample(const Sample& s, double tempo, qint64 exactFrames = 0);

// tempo -> a cache key in 1/1000 steps (0.1 % is far below what anyone can hear in a 4 beat loop).
int tempoPermille(double tempo);

// Stretched copies by (path, tempo permille); the oldest are dropped past maxBytes. Thread safe.
class StretchCache {
public:
    explicit StretchCache(qint64 maxBytes = 128LL * 1024 * 1024) : maxBytes_(maxBytes) {}

    std::shared_ptr<const Sample> peek(const QString& path, int permille) const;
    void put(const QString& path, int permille, std::shared_ptr<const Sample> s);
    // peek, else render (blocking) and store
    std::shared_ptr<const Sample> get(const Sample& base, int permille, qint64 exactFrames = 0);
    void clear();
    int count() const;

private:
    using Key = std::pair<QString, int>;
    mutable std::mutex mutex_;
    std::map<Key, std::shared_ptr<const Sample>> entries_;
    std::list<Key> order_;                       // oldest first
    qint64 maxBytes_;
    qint64 bytes_ = 0;
};

} // namespace vdj

#endif // SAMPLE_STRETCH_H
