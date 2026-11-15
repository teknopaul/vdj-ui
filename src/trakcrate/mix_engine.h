#ifndef TRAKCRATE_MIX_ENGINE_H
#define TRAKCRATE_MIX_ENGINE_H

#include "mix_compressor.h"
#include "mix_eq.h"
#include "mix_model.h"
#include "stretch_cache.h"

#include <condition_variable>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

// Renders a studio mix in blocks (plan studio-mix_PLAN.md, phase 4). No Qt: the owner supplies the tracks (TrackSource) and, for LADSPA
// filters, plain processor functions.
namespace trakcrate {

// Where the engine gets the audio of a track: STRETCHED to the master tempo and EDITED (cuts and loops applied), i.e. EffectiveTrack::editedFrames long.
class TrackSource {
public:
    virtual ~TrackSource() = default;
    // Null when the track cannot be prepared (the engine then plays silence for it). May block while the track is prepared.
    virtual std::shared_ptr<const DecodedPcm> edited(int trackIndex) = 0;
    // The engine is done with the track for now (it may be dropped from memory).
    virtual void release(int trackIndex) { (void)trackIndex; }
};

// The stretched and edited PCM of a track: `stretch` with the engine (nothing at ratio 1 / stretchOff), then the regions applied on the
// stretched, nudged grid. False when cancelled or on error.
bool buildEditedTrack(const EffectiveTrack& eff, const DecodedPcm& source, StretchEngine* engine, const StretchOptions& options, bool stretch,
                      std::shared_ptr<DecodedPcm>* out, std::string* err, const std::atomic<bool>* cancel = nullptr);

// The real thing: decodes the track, stretches through the StretchCache, applies the regions. Keeps at most `maxTracks` edited tracks in
// memory (least recently used). Thread safe; prefetch() prepares a track on a worker thread.
class TrackCache : public TrackSource {
public:
    TrackCache(const Mix& mix, const std::vector<TrackFacts>& facts, StretchCache* stretchCache, int maxTracks = 3);
    ~TrackCache() override;

    std::shared_ptr<const DecodedPcm> edited(int trackIndex) override;
    void release(int trackIndex) override;
    void prefetch(int trackIndex);
    bool isReady(int trackIndex);
    std::string lastError() const;
    void cancel() { cancel_ = true; }

private:
    std::shared_ptr<const DecodedPcm> build(int i);
    void worker();
    void touch(int i);

    Mix mix_;
    std::vector<EffectiveTrack> eff_;
    StretchCache* stretch_;
    size_t maxTracks_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::map<int, std::shared_ptr<const DecodedPcm>> ready_;
    std::list<int> lru_;                 // most recent first
    std::set<int> building_;
    std::list<int> queue_;
    std::string error_;
    std::atomic<bool> cancel_{false};
    bool stop_ = false;
    std::thread thread_;
};

using MixProcessor = std::function<void(float* stereo, int frames)>;

class MixEngine {
public:
    MixEngine(const Mix& mix, const std::vector<TrackFacts>& facts, TrackSource* source);

    // After the built-in compressor, in order. Not owned by the engine's maths: they carry their own state.
    void setProcessors(std::vector<MixProcessor> processors) { processors_ = std::move(processors); }

    long long totalFrames() const { return total_; }
    int latencyFrames() const { return latency_; }
    const std::vector<EffectiveTrack>& effective() const { return eff_; }
    long long trackStartFrame(int i) const { return startFrames_[size_t(i)]; }

    // `frames` interleaved stereo frames from mix frame startFrame (any position; past the end is silence).
    void render(long long startFrame, int frames, float* out);

private:
    struct TrackState {
        CrossoverEq eq;
        bool useEq = false;
        std::vector<CurvePoint> vol, bass, high;       // effective curves (5 ms ramps)
        std::shared_ptr<const DecodedPcm> pcm;
        long long validUntil = -1;                      // mix frame up to which the EQ state is continuous
    };
    void sumTracks(long long start, int frames, float* out);
    void sumTrack(int i, long long start, int frames, float* out);
    void runChain(float* buf, int frames);

    Mix mix_;
    std::vector<EffectiveTrack> eff_;
    std::vector<long long> startFrames_;
    std::vector<TrackState> tracks_;
    TrackSource* source_;
    long long total_ = 0;
    int latency_ = 0;
    HardKneeCompressor comp_;
    bool useComp_ = false;
    std::vector<MixProcessor> processors_;
    long long nextFeed_ = -1;                           // the chain's next input frame
    std::vector<float> scratch_, gv_, gb_, gh_;
};

// Convenience for tests and offline use: frames [start, start + frames) as a vector.
std::vector<float> renderMixRange(MixEngine& engine, long long start, long long frames);

} // namespace trakcrate

#endif
