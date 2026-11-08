#include "edit_job.h"

#include "track.h"

#include <algorithm>
#include <cmath>

namespace trakcrate {

std::string requestKey(const EditRequest& req)
{
    std::string k = req.trackDir.string() + "|" + std::to_string(req.grid.bpm) + "|" + std::to_string(req.grid.firstBeatUs);
    int n = 1;
    for (const EditRegion& r : req.regions) {
        k += "|" + describeRegion(n++, r) + "," + std::to_string(r.startBeat) + "," + std::to_string(r.lenBeats);
    }
    return k;
}

bool ensureSource(EditRenderCache* cache, const fs::path& trackDir, std::string* err, const std::atomic<bool>* cancel)
{
    const fs::path file = audioFileOf(trackDir);
    if (file.empty()) { if (err) *err = "the track has no audio file"; return false; }
    if (cache->source && cache->sourceFile == file) return true;
    auto src = std::make_shared<DecodedPcm>();
    std::string e;
    if (!decodeAudio(file, src.get(), &e, cancel)) { if (err) *err = "cannot read the track: " + e; return false; }
    cache->source = src;
    cache->sourceFile = file;
    cache->result.reset();
    cache->key.clear();
    return true;
}

bool ensureRendered(EditRenderCache* cache, const EditRequest& req, std::string* err, const std::atomic<bool>* cancel)
{
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (req.regions.empty()) return fail("no edits to render");
    const std::string key = requestKey(req);
    if (cache->result && cache->key == key) return true;
    if (!ensureSource(cache, req.trackDir, err, cancel)) return false;
    const long long total = (long long)cache->source->frames();
    const std::string bad = validateRegions(req.grid, req.regions, total);
    if (!bad.empty()) return fail(bad);
    cache->plan = planEdit(req.grid, req.regions, total);
    auto out = std::make_shared<DecodedPcm>();
    if (!renderEdit(*cache->source, cache->plan, req.grid, out.get(), cancel)) return fail(cancel && cancel->load() ? "cancelled" : "cannot render the edit");
    cache->result = out;
    cache->outGrid = outputGrid(req.grid, cache->plan);
    cache->key = key;
    return true;
}

double sourceToOutputMs(const EditPlan& plan, double sourceMs, bool last, int rate)
{
    const double f = sourceMs * rate / 1000.0;
    long long at = 0;
    double found = -1;
    for (const EditSegment& s : plan.segments) {
        const long long len = s.end - s.start;
        if (f >= double(s.start) && f < double(s.end)) {
            found = double(at) + (f - double(s.start));
            if (!last) break;
        } else if (found < 0 && f < double(s.start)) {
            found = double(at);                                  // inside a cut: where the audio after it starts
            if (!last) break;
        }
        at += len;
    }
    if (found < 0) found = double(at);
    return found * 1000.0 / rate;
}

double outputToSourceMs(const EditPlan& plan, double outputMs, int rate)
{
    const double f = std::max(0.0, outputMs) * rate / 1000.0;
    double at = 0;
    for (const EditSegment& s : plan.segments) {
        const double len = double(s.end - s.start);
        if (f < at + len) return (double(s.start) + (f - at)) * 1000.0 / rate;
        at += len;
    }
    return plan.segments.empty() ? 0.0 : double(plan.segments.back().end) * 1000.0 / rate;
}

std::vector<double> joinOutputMs(const EditPlan& plan, int rate)
{
    std::vector<double> out;
    long long at = 0;
    for (size_t i = 0; i + 1 < plan.segments.size(); ++i) {
        at += plan.segments[i].end - plan.segments[i].start;
        out.push_back(double(at) * 1000.0 / rate);
    }
    return out;
}

std::string originalStem(const fs::path& trackDir)
{
    Track t;
    std::string name;
    if (loadTrack(trackDir, &t)) name = t.meta.get("original_filename");
    fs::path stem = fs::path(name).stem();
    if (stem.empty()) stem = trackDir.filename();
    return stem.string();
}

std::string plannedFileName(const EditRequest& req, const EditOutputParams& p)
{
    const std::string stem = originalStem(req.trackDir);
    const char* ext = formatExt(p.format);
    if (p.intoLibrary || p.folder.empty()) return sanitizeName(stem) + "_" + fileUser(p.author) + "_edit." + ext;
    return uniqueEditPath(p.folder, stem, p.author, ext).filename().string();
}

std::string describePlaylistTarget(const Library& lib, const fs::path& originalTrackDir)
{
    const std::string folder = originalTrackDir.lexically_normal().parent_path().filename().string();
    if (!lib.hasPlaylist(folder)) return "No playlist named " + folder + ": the edit is only added to the folder";
    const std::vector<fs::path> order = lib.playlistTracks(folder);
    std::error_code ec;
    const fs::path orig = fs::weakly_canonical(originalTrackDir, ec);
    for (const fs::path& p : order) {
        std::error_code e2;
        if (fs::weakly_canonical(p, e2) == orig) return "Added to playlist " + folder + ", just before the original";
    }
    return "The original is not in playlist " + folder + ": the edit is only added to the folder";
}

bool writePreviewWav(const EditRenderCache& cache, const fs::path& path, std::string* err)
{
    if (!cache.result) { if (err) *err = "nothing rendered"; return false; }
    return writeWav(path, *cache.result, 0, AudioTags(), err);
}

EditOutcome writeEditOutput(const EditRenderCache& cache, const EditRequest& req, const EditOutputParams& p, const Library* lib, const std::atomic<bool>* cancel)
{
    EditOutcome res;
    auto fail = [&](const std::string& m) { res.error = m; return res; };
    if (!cache.result) return fail("nothing rendered");
    if (p.intoLibrary && !lib) return fail("no library is open");
    if (!p.intoLibrary && (p.folder.empty() || !fs::is_directory(p.folder))) return fail("the output folder does not exist");

    const std::string stem = originalStem(req.trackDir);
    const fs::path src = audioFileOf(req.trackDir);
    AudioTags tags;
    std::string err;
    if (!src.empty()) readAudioTags(src, &tags, nullptr);
    Track t;
    std::string title = loadTrack(req.trackDir, &t) ? t.title() : std::string();
    if (title.empty()) title = tags.get("title");
    if (title.empty()) title = stem;
    tags.tags["title"] = editTitle(title, p.author);

    // Library: render into a temp folder first (importing moves it); file only: straight to the unique path.
    fs::path workDir, audio, txt;
    if (p.intoLibrary) {
        workDir = makeTempDir("trakcrate-edit-out", fs::path(), &err);
        if (workDir.empty()) return fail(err);
        audio = workDir / plannedFileName(req, p);
    } else {
        audio = uniqueEditPath(p.folder, stem, p.author, formatExt(p.format));
    }
    txt = audio;
    txt.replace_extension(".txt");
    struct Cleanup {
        fs::path dir;
        ~Cleanup() { if (!dir.empty()) { std::error_code e; fs::remove_all(dir, e); } }
    } cleanup{workDir};

    const double bpm = cache.outGrid.bpm;
    const bool wrote = p.format == OutFormat::Wav ? writeWav(audio, *cache.result, bpm, tags, &err) : writeMp3(audio, *cache.result, p.mp3Kbps, bpm, tags, &err);
    if (!wrote) return fail(err);
    if (cancel && cancel->load()) { std::error_code e; fs::remove(audio, e); return fail("cancelled"); }

    EditInfo info;
    info.name = audio.filename().string();
    info.sourceFile = src.empty() ? stem : src.filename().string();
    info.author = p.author;
    info.timestamp = nowIso();
    info.sourceBpm = req.grid.bpm;
    info.sourceFirstBeatMs = double(req.grid.firstBeatUs) / 1000.0;
    info.bpm = cache.outGrid.bpm;
    info.firstBeatMs = double(cache.outGrid.firstBeatUs) / 1000.0;
    info.durationSec = double(cache.result->frames()) / cache.result->sampleRate;
    int n = 1;
    for (const EditRegion& r : req.regions) info.regions.push_back(describeRegion(n++, r));
    if (!writeInfoFile(txt, info, &err)) { std::error_code e; fs::remove(audio, e); return fail(err); }

    if (!p.intoLibrary) {
        res.ok = true;
        res.audioFile = audio;
        return res;
    }
    const EditImportResult imp = importEditToLibrary(*lib, req.trackDir, audio, txt, cache.outGrid, cancel);
    if (!imp.ok) return fail(imp.error);
    res.ok = true;
    res.trackDir = imp.trackDir;
    res.audioFile = audioFileOf(imp.trackDir);
    res.playlist = imp.playlist;
    res.playlistIndex = imp.playlistIndex;
    res.note = imp.note;
    return res;
}

std::string plannedLoopFileName(const fs::path& trackDir, const LoopExportParams& p)
{
    const std::string stem = sanitizeName(originalStem(trackDir) + "_" + (p.name.empty() ? "loop" : p.name));
    const char* ext = formatExt(p.format);
    if (p.folder.empty() || !fs::is_directory(p.folder)) return stem + "." + ext;
    for (int n = 1;; ++n) {
        const std::string name = n == 1 ? stem : stem + "_" + std::to_string(n);
        std::error_code ec;
        if (!fs::exists(p.folder / (name + "." + ext), ec)) return name + "." + ext;
    }
}

LoopOutcome exportLoop(EditRenderCache* cache, const fs::path& trackDir, const BeatGrid& grid, const LoopExportParams& p, const std::atomic<bool>* cancel)
{
    LoopOutcome res;
    auto fail = [&](const std::string& m) { res.error = m; return res; };
    if (grid.empty()) return fail("a beat grid is needed to cut out a loop");
    if (p.folder.empty() || !fs::is_directory(p.folder)) return fail("the loop folder does not exist");
    std::string err;
    if (!ensureSource(cache, trackDir, &err, cancel)) return fail(err);
    const DecodedPcm sample = renderLoopSample(*cache->source, grid, p.startBeat, p.lenBeats, p.algo, p.algoParam);
    if (sample.frames() == 0) return fail("the loop does not fit the track");
    if (cancel && cancel->load()) return fail("cancelled");

    const fs::path src = audioFileOf(trackDir);
    AudioTags tags;
    if (!src.empty()) readAudioTags(src, &tags, nullptr);
    Track t;
    std::string title = loadTrack(trackDir, &t) ? t.title() : std::string();
    if (title.empty()) title = tags.get("title");
    if (title.empty()) title = originalStem(trackDir);
    tags.tags["title"] = loopTitle(title, p.name, p.author);

    const fs::path file = p.folder / plannedLoopFileName(trackDir, p);
    const bool ok = p.format == OutFormat::Wav ? writeWav(file, sample, grid.bpm, tags, &err, int(p.lenBeats)) : writeMp3(file, sample, p.mp3Kbps, grid.bpm, tags, &err);
    if (!ok) return fail(err);
    res.ok = true;
    res.file = file;
    return res;
}

bool writeLoopPreviewWav(const DecodedPcm& sample, const fs::path& path, int repeats, std::string* err)
{
    DecodedPcm many;
    many.sampleRate = sample.sampleRate;
    for (int i = 0; i < std::max(1, repeats); ++i) many.stereo.insert(many.stereo.end(), sample.stereo.begin(), sample.stereo.end());
    return writeWav(path, many, 0, AudioTags(), err);
}

} // namespace trakcrate
