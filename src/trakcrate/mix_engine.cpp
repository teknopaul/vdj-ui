#include "mix_engine.h"

#include "edit_job.h"
#include "edit_render.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace trakcrate {

namespace {

constexpr int kBlock = 4096;
constexpr int kWarm = 4096;                // frames of filter history rebuilt after a jump

// Walks forward along an effective curve while the beat increases.
struct Cursor {
    const std::vector<CurvePoint>& p;
    size_t i = 0;
    double gain(double beat)
    {
        if (p.empty()) return 1;
        if (beat <= p.front().beat) return p.front().value / 100.0;
        if (beat >= p.back().beat) return p.back().value / 100.0;
        if (i + 1 >= p.size() || p[i].beat > beat) i = size_t(std::upper_bound(p.begin(), p.end(), beat, [](double b, const CurvePoint& q) { return b < q.beat; }) - p.begin()) - 1;
        while (i + 1 < p.size() && p[i + 1].beat <= beat) ++i;
        const CurvePoint &lo = p[i], &hi = p[i + 1];
        const double span = hi.beat - lo.beat;
        return (lo.value + (hi.value - lo.value) * (span > 0 ? (beat - lo.beat) / span : 1)) / 100.0;
    }
};

} // namespace

bool buildEditedTrack(const EffectiveTrack& eff, const DecodedPcm& source, StretchEngine* engine, const StretchOptions& options, bool stretch,
                      std::shared_ptr<DecodedPcm>* out, std::string* err, const std::atomic<bool>* cancel)
{
    auto stretched = std::make_shared<DecodedPcm>();
    if (stretch && std::fabs(eff.ratio - 1) > 1e-9) {
        if (!engine) {
            if (err) *err = "no stretch engine";
            return false;
        }
        if (!engine->stretch(source, eff.ratio, options, stretched.get(), nullptr, cancel, err)) return false;
    } else {
        *stretched = source;
    }
    if (eff.plan.segments.size() == 1 && eff.plan.segments[0].start == 0 && eff.plan.segments[0].end >= (long long)stretched->frames()) {
        *out = stretched;                           // no regions: nothing to cut or loop
        return true;
    }
    auto edited = std::make_shared<DecodedPcm>();
    if (!renderEdit(*stretched, eff.plan, eff.stretchedGrid, edited.get(), cancel)) {
        if (err) *err = (cancel && cancel->load()) ? "cancelled" : "the cuts and loops do not fit the track";
        return false;
    }
    *out = edited;
    return true;
}

// ---- TrackCache ------------------------------------------------------------------------------------------------------------------

TrackCache::TrackCache(const Mix& mix, const std::vector<TrackFacts>& facts, StretchCache* stretchCache, int maxTracks)
    : mix_(mix), eff_(effectiveTracks(mix, facts)), stretch_(stretchCache), maxTracks_(size_t(std::max(1, maxTracks)))
{
    thread_ = std::thread([this] { worker(); });
}

TrackCache::~TrackCache()
{
    {
        std::lock_guard<std::mutex> g(mu_);
        stop_ = true;
        cancel_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

std::string TrackCache::lastError() const
{
    return error_;
}

void TrackCache::touch(int i)
{
    lru_.remove(i);
    lru_.push_front(i);
    while (lru_.size() > maxTracks_) {
        ready_.erase(lru_.back());
        lru_.pop_back();
    }
}

bool TrackCache::isReady(int i)
{
    std::lock_guard<std::mutex> g(mu_);
    return ready_.count(i) > 0;
}

void TrackCache::release(int i)
{
    std::lock_guard<std::mutex> g(mu_);
    // kept until pushed out by newer tracks: a seek back to the track is then instant
    (void)i;
}

std::shared_ptr<const DecodedPcm> TrackCache::build(int i)
{
    if (i < 0 || size_t(i) >= mix_.tracks.size()) return nullptr;
    const MixTrack& t = mix_.tracks[size_t(i)];
    std::string err;
    DecodedPcm src;
    const fs::path dir = t.file.parent_path();
    const bool stretch = !t.stretchOff && std::fabs(eff_[size_t(i)].ratio - 1) > 1e-9;
    std::shared_ptr<const DecodedPcm> result;
    if (stretch) {
        const StretchChoice choice = StretchRegistry::instance().resolveChoice(t.stretch, mix_.stretch);
        auto stretched = stretch_->get(dir, nullptr, choice, eff_[size_t(i)].ratio, nullptr, &cancel_, &err);
        if (stretched) {
            const EffectiveTrack& e = eff_[size_t(i)];
            if (e.plan.segments.size() == 1 && e.plan.segments[0].start == 0 && e.plan.segments[0].end >= (long long)stretched->frames()) {
                result = stretched;
            } else {
                auto edited = std::make_shared<DecodedPcm>();
                if (renderEdit(*stretched, e.plan, e.stretchedGrid, edited.get(), &cancel_)) result = edited;
                else err = "the cuts and loops do not fit the track";
            }
        }
    } else {
        EditRenderCache c;
        if (ensureSource(&c, dir, &err, &cancel_)) {
            std::shared_ptr<DecodedPcm> out;
            if (buildEditedTrack(eff_[size_t(i)], *c.source, nullptr, {}, false, &out, &err, &cancel_)) result = out;
        }
    }
    if (!result) {
        std::lock_guard<std::mutex> g(mu_);
        error_ = "track " + std::to_string(i + 1) + ": " + err;
    }
    return result;
}

std::shared_ptr<const DecodedPcm> TrackCache::edited(int i)
{
    std::unique_lock<std::mutex> lock(mu_);
    for (;;) {
        auto it = ready_.find(i);
        if (it != ready_.end()) {
            touch(i);
            return it->second;
        }
        if (!building_.count(i)) break;
        cv_.wait(lock);
        if (stop_) return nullptr;
    }
    building_.insert(i);
    lock.unlock();
    auto pcm = build(i);
    lock.lock();
    building_.erase(i);
    if (pcm) {
        ready_[i] = pcm;
        touch(i);
    }
    cv_.notify_all();
    return pcm;
}

void TrackCache::prefetch(int i)
{
    std::lock_guard<std::mutex> g(mu_);
    if (ready_.count(i) || building_.count(i) || std::find(queue_.begin(), queue_.end(), i) != queue_.end()) return;
    queue_.push_back(i);
    cv_.notify_all();
}

void TrackCache::worker()
{
    std::unique_lock<std::mutex> lock(mu_);
    for (;;) {
        cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
        if (stop_) return;
        const int i = queue_.front();
        queue_.pop_front();
        if (ready_.count(i) || building_.count(i)) continue;
        building_.insert(i);
        lock.unlock();
        auto pcm = build(i);
        lock.lock();
        building_.erase(i);
        if (pcm) {
            ready_[i] = pcm;
            touch(i);
        }
        cv_.notify_all();
    }
}

// ---- MixEngine ---------------------------------------------------------------------------------------------------------------------------

MixEngine::MixEngine(const Mix& mix, const std::vector<TrackFacts>& facts, TrackSource* source)
    : mix_(mix), eff_(effectiveTracks(mix, facts)), source_(source)
{
    const std::vector<double> starts = trackStartMs(mix_, eff_);
    total_ = (long long)std::llround(mixLengthMs(starts, eff_) * kEditRate / 1000.0);
    tracks_.resize(eff_.size());
    for (size_t i = 0; i < eff_.size(); ++i) {
        startFrames_.push_back((long long)std::llround(starts[i] * kEditRate / 1000.0));
        TrackState& t = tracks_[i];
        const MixTrack& mt = mix_.tracks[i];
        t.vol = effectiveCurve(mt.volume, eff_[i].bpm);
        t.bass = effectiveCurve(mt.bass, eff_[i].bpm);
        t.high = effectiveCurve(mt.high, eff_[i].bpm);
        t.useEq = !curveIsDefault(mt.bass) || !curveIsDefault(mt.high);
        t.eq.configure(mix_.bassHz, mix_.highHz, mix_.slopeDb, kEditRate);
    }
    for (const MixFilter& f : mix_.filters) {
        if (f.kind != MixFilterKind::BuiltinCompressor || !f.on) continue;
        auto p = [&](const char* k, double d) { auto it = f.params.find(k); return it == f.params.end() ? d : it->second; };
        comp_.configure(p("threshold", -3), p("ratio", 10), p("attack", 1), p("release", 80), kEditRate);
        useComp_ = true;
        break;
    }
    latency_ = useComp_ ? comp_.latencyFrames() : 0;
}

void MixEngine::sumTrack(int i, long long start, int frames, float* out)
{
    TrackState& t = tracks_[size_t(i)];
    const EffectiveTrack& e = eff_[size_t(i)];
    const long long s0 = startFrames_[size_t(i)];
    const long long len = e.editedFrames;
    // the part of [start, start + frames) inside the track
    const long long a = std::max(start, s0), b = std::min(start + frames, s0 + len);
    if (a >= b) {
        if (start >= s0 + len && t.pcm) {
            t.pcm.reset();
            source_->release(i);
        }
        return;
    }
    const int n = int(b - a);
    float* dst = out + (a - start) * 2;
    const MixTrack& mt = mix_.tracks[size_t(i)];
    if (gv_.size() < size_t(n)) gv_.resize(size_t(n)), gb_.resize(size_t(n)), gh_.resize(size_t(n));

    // gains per frame; the beat of frame f is linear in f
    const double perFrame = e.bpm / 60.0 / kEditRate;
    const double beat0 = (double(a - s0) / kEditRate - double(e.grid.firstBeatUs) / 1e6) * e.bpm / 60.0;
    Cursor cv{t.vol}, cb{t.bass}, ch{t.high};
    const double trim = mt.trim / 100.0;
    bool silent = true;
    for (int k = 0; k < n; ++k) {
        const double beat = beat0 + k * perFrame;
        gv_[size_t(k)] = float(cv.gain(beat) * trim);
        gb_[size_t(k)] = t.useEq ? float(cb.gain(beat)) : 1.0f;
        gh_[size_t(k)] = t.useEq ? float(ch.gain(beat)) : 1.0f;
        if (gv_[size_t(k)] != 0) silent = false;
    }
    if (silent) {
        t.validUntil = -1;                         // the filter memory goes stale while skipped
        return;
    }
    if (!t.pcm) t.pcm = source_->edited(i);
    if (!t.pcm) return;
    const DecodedPcm& pcm = *t.pcm;
    const long long local = a - s0;
    auto at = [&](long long f, int c) -> float { return f >= 0 && f < (long long)pcm.frames() ? pcm.stereo[size_t(f) * 2 + size_t(c)] : 0.0f; };

    if (t.useEq) {
        if (t.validUntil != a) {                    // a jump or a skipped stretch: rebuild the filter memory from the audio before
            t.eq.reset();
            const long long from = std::max(0LL, local - kWarm);
            const int wn = int(local - from);
            if (wn > 0) {
                std::vector<float> wbuf(size_t(wn) * 2), ones(size_t(wn), 1.0f);
                for (int k = 0; k < wn; ++k) {
                    wbuf[size_t(k) * 2] = at(from + k, 0);
                    wbuf[size_t(k) * 2 + 1] = at(from + k, 1);
                }
                t.eq.process(wbuf.data(), wbuf.data(), wn, ones.data(), ones.data());
            }
        }
        std::vector<float> buf(size_t(n) * 2);
        for (int k = 0; k < n; ++k) {
            buf[size_t(k) * 2] = at(local + k, 0);
            buf[size_t(k) * 2 + 1] = at(local + k, 1);
        }
        t.eq.process(buf.data(), buf.data(), n, gb_.data(), gh_.data());
        for (int k = 0; k < n; ++k) {
            dst[k * 2] += buf[size_t(k) * 2] * gv_[size_t(k)];
            dst[k * 2 + 1] += buf[size_t(k) * 2 + 1] * gv_[size_t(k)];
        }
        t.validUntil = b;
    } else {
        for (int k = 0; k < n; ++k) {
            dst[k * 2] += at(local + k, 0) * gv_[size_t(k)];
            dst[k * 2 + 1] += at(local + k, 1) * gv_[size_t(k)];
        }
    }
    if (b >= s0 + len) {
        t.pcm.reset();
        source_->release(i);
    }
}

void MixEngine::sumTracks(long long start, int frames, float* out)
{
    std::fill(out, out + size_t(frames) * 2, 0.0f);
    for (size_t i = 0; i < tracks_.size(); ++i) sumTrack(int(i), start, frames, out);
}

void MixEngine::runChain(float* buf, int frames)
{
    if (useComp_) comp_.process(buf, frames);
    for (MixProcessor& p : processors_) p(buf, frames);
}

void MixEngine::render(long long startFrame, int frames, float* out)
{
    // The chain delays the audio by latency_: to get the mix frame f out, the chain is fed frame f + latency_.
    long long feed = startFrame + latency_;
    int done = 0;
    if (feed != nextFeed_) {
        if (useComp_) comp_.reset();
        // warm the master chain with the audio just before
        const long long from = std::max(0LL, feed - kWarm);
        long long pos = from;
        std::vector<float> buf(size_t(kBlock) * 2);
        while (pos < feed) {
            const int n = int(std::min<long long>(kBlock, feed - pos));
            sumTracks(pos, n, buf.data());
            runChain(buf.data(), n);
            pos += n;
        }
        nextFeed_ = feed;
    }
    while (done < frames) {
        const int n = std::min(kBlock, frames - done);
        sumTracks(feed, n, out + size_t(done) * 2);
        runChain(out + size_t(done) * 2, n);
        feed += n;
        done += n;
    }
    nextFeed_ = feed;
}

std::vector<float> renderMixRange(MixEngine& engine, long long start, long long frames)
{
    std::vector<float> v(size_t(frames) * 2);
    engine.render(start, int(frames), v.data());
    return v;
}

} // namespace trakcrate
