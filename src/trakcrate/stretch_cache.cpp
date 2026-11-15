#include "stretch_cache.h"

#include "sha256.h"
#include "track.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

namespace trakcrate {

namespace {

const char kMagic[8] = {'T', 'C', 'S', 'T', 'R', 'C', 'H', '1'};
constexpr size_t kHeader = 8 + 8 + 4;          // magic, frames, sample rate

std::string audioHash(const fs::path& trackDir)
{
    std::string meta;
    if (readFile(trackDir / "meta.txt", &meta)) {
        const std::string h = KvDoc::parse(meta).get("audio_sha256");
        if (!h.empty()) return h;
    }
    const fs::path audio = audioFileOf(trackDir);
    FILE* f = audio.empty() ? nullptr : std::fopen(audio.c_str(), "rb");
    if (!f) return "noaudio:" + trackDir.string();
    Sha256 s;
    char buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.update(buf, n);
    std::fclose(f);
    return s.finishHex();
}

bool nearlyOne(double r) { return std::fabs(r - 1) < 1e-9; }

} // namespace

fs::path defaultStretchCacheDir()
{
    if (const char* x = std::getenv("XDG_CACHE_HOME"); x && *x) return fs::path(x) / "vdj-ui" / "stretch";
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / ".cache" / "vdj-ui" / "stretch";
}

StretchCache::StretchCache(fs::path dir, uintmax_t maxBytes, StretchRegistry* registry)
    : dir_(std::move(dir)), maxBytes_(maxBytes), reg_(registry ? registry : &StretchRegistry::instance())
{
}

std::string StretchCache::keyFor(const fs::path& trackDir, const StretchChoice& choice, double ratio) const
{
    StretchEngine* e = reg_->find(choice.engine);
    if (!e) return std::string();
    char r[40];
    std::snprintf(r, sizeof r, "%.6f", ratio);
    return sha256Hex(audioHash(trackDir) + "\n" + choiceKey(reg_->normalize(choice)) + "\n" + r + "\n" + std::to_string(e->version()) + "\n" + e->fingerprint());
}

bool StretchCache::entryValid(const fs::path& file) const
{
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) return false;
    unsigned char h[kHeader];
    bool ok = std::fread(h, 1, kHeader, f) == kHeader && std::memcmp(h, kMagic, 8) == 0;
    uint64_t frames = 0;
    if (ok) std::memcpy(&frames, h + 8, 8);
    std::fclose(f);
    std::error_code ec;
    return ok && fs::file_size(file, ec) == kHeader + frames * 8 && !ec;
}

bool StretchCache::readEntry(const fs::path& file, DecodedPcm* out) const
{
    if (!entryValid(file)) return false;
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) return false;
    unsigned char h[kHeader];
    bool ok = std::fread(h, 1, kHeader, f) == kHeader;
    uint64_t frames = 0;
    int32_t rate = 0;
    std::memcpy(&frames, h + 8, 8);
    std::memcpy(&rate, h + 16, 4);
    if (ok) {
        out->sampleRate = rate;
        out->stereo.resize(size_t(frames) * 2);
        ok = std::fread(out->stereo.data(), 4, out->stereo.size(), f) == out->stereo.size();
    }
    std::fclose(f);
    return ok;
}

bool StretchCache::has(const fs::path& trackDir, const StretchChoice& choice, double ratio)
{
    if (nearlyOne(ratio)) return true;
    const std::string key = keyFor(trackDir, choice, ratio);
    return !key.empty() && entryValid(fileFor(key));
}

std::shared_ptr<DecodedPcm> StretchCache::get(const fs::path& trackDir, const DecodedPcm* source, const StretchChoice& choice, double ratio,
                                              const StretchProgress& progress, const std::atomic<bool>* cancel, std::string* err)
{
    auto fail = [&](const std::string& m) -> std::shared_ptr<DecodedPcm> {
        if (err) *err = m;
        return nullptr;
    };
    auto sourcePcm = [&](DecodedPcm* into) -> bool {
        if (source) {
            *into = *source;
            return true;
        }
        const fs::path audio = audioFileOf(trackDir);
        if (audio.empty()) {
            fail("no audio file in " + trackDir.string());
            return false;
        }
        return decodeAudio(audio, into, err, cancel);
    };
    if (nearlyOne(ratio)) {
        auto p = std::make_shared<DecodedPcm>();
        if (!sourcePcm(p.get())) return nullptr;
        if (progress) progress(1);
        return p;
    }
    StretchEngine* engine = reg_->find(choice.engine);
    if (!engine) return fail("unknown stretch engine '" + choice.engine + "'");
    std::string why;
    if (!engine->available(&why)) return fail(engine->name() + " is not available: " + why);
    const std::string key = keyFor(trackDir, choice, ratio);
    const fs::path file = fileFor(key);

    for (;;) {
        if (cancel && cancel->load()) return fail("cancelled");
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [&] { return !inflight_.count(key) || (cancel && cancel->load()); });
            if (cancel && cancel->load()) return fail("cancelled");
            auto p = std::make_shared<DecodedPcm>();
            if (readEntry(file, p.get())) {
                std::error_code ec;
                fs::last_write_time(file, fs::file_time_type::clock::now(), ec);       // recently used
                if (progress) progress(1);
                return p;
            }
            inflight_.insert(key);
        }
        break;
    }

    // This thread stretches.
    struct Release {
        StretchCache* c;
        const std::string& key;
        ~Release()
        {
            { std::lock_guard<std::mutex> g(c->mu_); c->inflight_.erase(key); }
            c->cv_.notify_all();
        }
    } release{this, key};

    DecodedPcm src;
    if (!sourcePcm(&src)) return nullptr;
    auto result = std::make_shared<DecodedPcm>();
    if (!engine->stretch(src, ratio, reg_->normalize(choice).options, result.get(), progress, cancel, err)) return nullptr;

    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (!ec) {
        const fs::path tmp = dir_ / (key + ".tmp" + std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id()) % 1000000));
        FILE* f = std::fopen(tmp.c_str(), "wb");
        bool ok = f != nullptr;
        if (f) {
            unsigned char h[kHeader];
            std::memcpy(h, kMagic, 8);
            const uint64_t frames = result->frames();
            const int32_t rate = result->sampleRate;
            std::memcpy(h + 8, &frames, 8);
            std::memcpy(h + 16, &rate, 4);
            ok = std::fwrite(h, 1, kHeader, f) == kHeader && std::fwrite(result->stereo.data(), 4, result->stereo.size(), f) == result->stereo.size();
            ok = std::fclose(f) == 0 && ok;
        }
        if (ok) fs::rename(tmp, file, ec);
        if (!ok || ec) fs::remove(tmp, ec);       // the result is still returned: only the caching failed
        else evict(key);
    }
    return result;
}

void StretchCache::evict(const std::string& keep)
{
    struct E { fs::path p; fs::file_time_type t; uintmax_t size; };
    std::vector<E> files;
    uintmax_t total = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        if (e.path().extension() != ".pcm") continue;
        E x{e.path(), e.last_write_time(ec), e.file_size(ec)};
        total += x.size;
        files.push_back(x);
    }
    std::sort(files.begin(), files.end(), [](const E& a, const E& b) { return a.t < b.t; });
    for (const E& f : files) {
        if (total <= maxBytes_) break;
        if (f.p.stem().string() == keep) continue;
        fs::remove(f.p, ec);
        total -= f.size;
    }
}

void StretchCache::clear()
{
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec))
        if (e.path().extension() == ".pcm" || e.path().extension().string().rfind(".tmp", 0) == 0) fs::remove(e.path(), ec);
}

uintmax_t StretchCache::size() const
{
    uintmax_t total = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec))
        if (e.path().extension() == ".pcm") total += e.file_size(ec);
    return total;
}

} // namespace trakcrate
