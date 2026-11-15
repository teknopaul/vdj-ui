#include "mix_render.h"

#include "audio_probe.h"
#include "mp3_writer.h"

#include <cmath>

namespace trakcrate {

namespace {
bool cancelled(const std::atomic<bool>* c) { return c && c->load(); }
}

bool readTrackFacts(const fs::path& audioFile, TrackFacts* out, std::string* err)
{
    Track tr;
    if (!loadTrack(audioFile.parent_path(), &tr)) {
        if (err) *err = "the track folder is missing";
        return false;
    }
    out->grid = beatGridFrom(tr.grid);
    AudioInfo info;
    std::string perr;
    if (!probeAudio(audioFile, &info, &perr)) {
        if (err) *err = perr;
        return false;
    }
    out->frames = (long long)std::llround(info.durationSec * kEditRate);
    return true;
}

bool readMixFacts(const Mix& mix, std::vector<TrackFacts>* facts, std::string* err)
{
    facts->clear();
    for (size_t i = 0; i < mix.tracks.size(); ++i) {
        TrackFacts f;
        std::string e;
        if (mix.tracks[i].missing || !readTrackFacts(mix.tracks[i].file, &f, &e)) {
            if (err) *err = "Track " + std::to_string(i + 1) + ": " + (e.empty() ? "the audio file is missing" : e);
            return false;
        }
        facts->push_back(f);
    }
    return true;
}

fs::path uniqueMixOutPath(const fs::path& folder, const std::string& name)
{
    const std::string base = sanitizeName(name);
    std::error_code ec;
    for (int i = 1; i < 100000; ++i) {
        const fs::path p = folder / (base + (i == 1 ? "" : "_" + std::to_string(i)) + ".mp3");
        if (!fs::exists(p, ec) && !fs::exists(p.string() + ".part", ec)) return p;
    }
    return folder / (base + "_x.mp3");
}

bool renderMixToMp3(const Mix& mix, const fs::path& root, const fs::path& outFolder, const MixRenderOptions& opts, const MixRenderProgress& progress,
                    const std::atomic<bool>* cancel, std::string* err, fs::path* outFile)
{
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    auto report = [&](const std::string& stage, double d, double t) {
        if (progress) progress(stage, d, t);
    };
    report("checking", 0, 1);
    std::vector<TrackFacts> facts;
    if (!readMixFacts(mix, &facts, err)) return false;
    for (size_t i = 0; i < mix.tracks.size(); ++i) {
        const std::string p = analysedProblem(mix.tracks[i].file.parent_path());
        if (!p.empty()) return fail("Track " + std::to_string(i + 1) + ": " + p);
    }
    const std::string bad = validateMix(mix, facts);
    if (!bad.empty()) return fail(bad);
    fs::path cover;
    if (!mix.image.empty()) {
        cover = fs::path(mix.image).is_absolute() ? fs::path(mix.image) : root / mix.image;
        std::error_code ec;
        if (!fs::exists(cover, ec)) return fail("the cover image was not found: " + cover.string());
    }
    std::error_code ec;
    fs::create_directories(outFolder, ec);
    if (!fs::is_directory(outFolder, ec)) return fail("cannot use the folder " + outFolder.string());

    // ---- stage 1: every stretch in the cache
    StretchCache ownCache(defaultStretchCacheDir());
    StretchCache* cache = opts.stretchCache ? opts.stretchCache : &ownCache;
    const std::vector<EffectiveTrack> eff = effectiveTracks(mix, facts);
    const int n = int(mix.tracks.size());
    for (int i = 0; i < n; ++i) {
        if (cancelled(cancel)) return fail("cancelled");
        const MixTrack& t = mix.tracks[size_t(i)];
        const std::string stage = "stretching track " + std::to_string(i + 1) + " of " + std::to_string(n);
        report(stage, 0, 1);
        if (t.stretchOff || std::fabs(eff[size_t(i)].ratio - 1) < 1e-9) continue;
        const StretchChoice choice = StretchRegistry::instance().resolveChoice(t.stretch, mix.stretch);
        if (cache->has(t.file.parent_path(), choice, eff[size_t(i)].ratio)) continue;
        std::string serr;
        auto got = cache->get(t.file.parent_path(), nullptr, choice, eff[size_t(i)].ratio, [&](double f) { report(stage, f, 1); }, cancel, &serr);
        if (!got) return fail(cancelled(cancel) ? "cancelled" : "Track " + std::to_string(i + 1) + ": " + serr);
    }

    // ---- stage 2: render
    TrackCache tracks(mix, facts, cache);
    MixEngine engine(mix, facts, &tracks);
    if (opts.ladspaFactory) engine.setProcessors(opts.ladspaFactory());
    const fs::path out = uniqueMixOutPath(outFolder, mix.name.empty() ? "Studio mix" : mix.name);
    AudioTags tags;
    tags.tags["title"] = mix.name;
    tags.tags["artist"] = mix.author;
    tags.tags["album"] = mix.playlist;
    tags.tags["comment"] = "Studio mix";
    Mp3Writer w;
    if (!cover.empty()) w.setPicture(cover);
    std::string werr;
    if (!w.open(out, opts.kbps, opts.vbr, tags, kEditRate, &werr)) return fail(werr);
    constexpr int kBlock = 8192;
    std::vector<float> buf(size_t(kBlock) * 2);
    const long long total = engine.totalFrames();
    int prefetched = 0;
    for (long long pos = 0; pos < total; pos += kBlock) {
        if (cancelled(cancel)) {
            w.abort();
            return fail("cancelled");
        }
        // the next track is prepared in the background while this one plays out
        for (int i = prefetched; i < n; ++i) {
            if (engine.trackStartFrame(i) <= pos + 20LL * kEditRate) {
                tracks.prefetch(i);
                prefetched = i + 1;
            } else {
                break;
            }
        }
        const int len = int(std::min<long long>(kBlock, total - pos));
        engine.render(pos, len, buf.data());
        if (!w.write(buf.data(), size_t(len), &werr)) return fail(werr);
        report("rendering", double(pos + len), double(total));
    }
    report("finishing", 0, 1);
    if (!tracks.lastError().empty()) {
        w.abort();
        return fail(tracks.lastError());
    }
    if (!w.close(&werr)) return fail(werr);
    if (outFile) *outFile = out;
    report("finishing", 1, 1);
    return true;
}

} // namespace trakcrate
