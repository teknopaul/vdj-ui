#ifndef TRAKCRATE_STRETCH_CACHE_H
#define TRAKCRATE_STRETCH_CACHE_H

#include "stretch_engine.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>

namespace trakcrate {

// $XDG_CACHE_HOME/vdj-ui/stretch, else ~/.cache/vdj-ui/stretch
fs::path defaultStretchCacheDir();

// Stretched tracks on disk, keyed by the audio, the engine and its settings and the ratio, so every combination is stretched once.
// Thread safe: a second get of a key being stretched waits for the first.
class StretchCache {
public:
    StretchCache(fs::path dir, uintmax_t maxBytes = uintmax_t(4) << 30, StretchRegistry* registry = nullptr);

    // `source` may be null (the track's audio is decoded when needed). A ratio of 1 never calls an engine. Null and err on failure or cancel.
    std::shared_ptr<DecodedPcm> get(const fs::path& trackDir, const DecodedPcm* source, const StretchChoice& choice, double ratio, const StretchProgress& progress,
                                    const std::atomic<bool>* cancel, std::string* err);
    // True when get() would not have to stretch.
    bool has(const fs::path& trackDir, const StretchChoice& choice, double ratio);
    void clear();
    uintmax_t size() const;           // bytes on disk
    std::string keyFor(const fs::path& trackDir, const StretchChoice& choice, double ratio) const;      // "" for an unknown engine

private:
    fs::path fileFor(const std::string& key) const { return dir_ / (key + ".pcm"); }
    bool readEntry(const fs::path& file, DecodedPcm* out) const;
    bool entryValid(const fs::path& file) const;
    void evict(const std::string& keep);

    fs::path dir_;
    uintmax_t maxBytes_;
    StretchRegistry* reg_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::set<std::string> inflight_;
};

} // namespace trakcrate

#endif
