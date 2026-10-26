#ifndef SAMPLE_CACHE_H
#define SAMPLE_CACHE_H

#include <QString>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include "audio_decoder.h"

namespace vdj {

// A decoded DJ Tools sample. bpm is 0 when unknown.
struct Sample {
    QString path;
    QString name;                // file name without folder
    DecodedAudio audio;
    double bpm = 0.0;
    double lengthMs() const { return audio.lengthMs(); }
};

// Decoded samples by path, shared with the audio thread (the shared_ptr keeps a sample alive while a voice plays it).
// Least recently used samples are dropped when the total passes maxBytes. Thread safe. get() decodes on the calling
// thread and blocks: never call it from the audio thread, use peek() there.
class SampleCache {
public:
    explicit SampleCache(qint64 maxBytes = 128LL * 1024 * 1024) : maxBytes_(maxBytes) {}

    std::shared_ptr<const Sample> get(const QString& path, QString* error = nullptr);
    std::shared_ptr<const Sample> peek(const QString& path) const;   // cached only, null otherwise
    void clear();
    int count() const;
    qint64 bytes() const;

private:
    struct Entry {
        std::shared_ptr<const Sample> sample;
        std::list<QString>::iterator lru;
    };
    static qint64 sizeOf(const Sample& s) { return qint64(s.audio.samples.size()) * qint64(sizeof(qint16)); }
    void evictLocked();

    mutable std::mutex mutex_;
    std::map<QString, Entry> entries_;
    mutable std::list<QString> lru_;                 // front = most recent
    qint64 maxBytes_;
    qint64 bytes_ = 0;
};

} // namespace vdj

#endif // SAMPLE_CACHE_H
