#include "importer.h"

#include "analysis.h"
#include "audio_probe.h"
#include "natsort.h"
#include "sha256.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <set>

namespace trakcrate {

static std::string lowerExt(const fs::path& p)
{
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e;
}

static bool isAudio(const fs::path& p)
{
    std::string e = lowerExt(p);
    return e == ".mp3" || e == ".wav" || e == ".flac";
}

static bool hashFile(const fs::path& p, std::string* hex, uintmax_t* size, std::string* err)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) { if (err) *err = "cannot read file"; return false; }
    Sha256 h;
    char buf[1 << 16];
    uintmax_t total = 0;
    while (in.read(buf, sizeof buf) || in.gcount() > 0) {
        h.update(buf, size_t(in.gcount()));
        total += uintmax_t(in.gcount());
    }
    if (in.bad()) { if (err) *err = "read error"; return false; }
    *hex = h.finishHex();
    *size = total;
    return true;
}

static std::string nowIso()
{
    std::time_t t = std::time(nullptr);
    std::tm tm;
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

static std::string tag(const AudioInfo& a, const char* key)
{
    auto it = a.tags.find(key);
    return it == a.tags.end() ? std::string() : it->second;
}

// "2019-05-01" -> 2019; "3/12" -> 3
static long long leadingNumber(const std::string& s)
{
    return std::strtoll(s.c_str(), nullptr, 10);
}

// hashes of the audio already in the master (written to meta.txt as audio_sha256 by this importer)
static std::map<std::string, fs::path> existingHashes(const Library& lib)
{
    std::map<std::string, fs::path> idx;
    for (const std::string& f : lib.listFolders()) {
        for (const std::string& t : lib.listTracks(f)) {
            fs::path dir = lib.trackDir(f, t);
            std::string text;
            if (!readFile(dir / "meta.txt", &text)) continue;
            std::string h = KvDoc::parse(text).get("audio_sha256");
            if (!h.empty()) idx.emplace(h, dir);
        }
    }
    return idx;
}

static bool moveOrCopy(const fs::path& from, const fs::path& to, ImportMode mode, std::string* err)
{
    std::error_code ec;
    if (mode == ImportMode::Link) {
        fs::create_symlink(fs::absolute(from, ec), to, ec);
        if (ec) { *err = "cannot link: " + ec.message(); return false; }
        return true;
    }
    if (mode == ImportMode::Move) {
        fs::rename(from, to, ec);
        if (!ec) return true;
        if (ec != std::errc::cross_device_link) { *err = "cannot move: " + ec.message(); return false; }
    }
    fs::copy_file(from, to, fs::copy_options::none, ec);
    if (ec) { *err = "cannot copy: " + ec.message(); return false; }
    if (mode == ImportMode::Move) fs::remove(from, ec);              // copied across devices: now remove the original
    return true;
}

struct Candidate {
    fs::path path;
    std::string rel;        // relative to the source, '/' separated: the sort key
    std::string folder;     // master folder
};

ImportReport importFolder(const Library& lib, const fs::path& dirIn, const ImportOptions& opts, const ImportProgress& progress,
                          const std::atomic<bool>* cancel)
{
    ImportReport rep;
    std::error_code ec;
    fs::path dir = fs::absolute(dirIn, ec).lexically_normal();
    if (dir.filename().empty()) dir = dir.parent_path();             // trailing slash
    if (!fs::is_directory(dir, ec)) { rep.error = "not a folder: " + dirIn.string(); return rep; }

    std::string base = sanitizeName(dir.filename().string());
    std::string folderName = opts.folder.empty() ? base : sanitizeName(opts.folder);
    std::string playlist = opts.playlist.empty() ? base : opts.playlist;
    if (!opts.noPlaylist && !isSafeName(playlist)) { rep.error = "bad playlist name: " + playlist; return rep; }
    if (!opts.noPlaylist && lib.hasPlaylist(playlist) && !opts.append && !opts.replace) {
        rep.error = "playlist exists: " + playlist + " (use append or replace)";
        return rep;
    }

    // scan, in a stable order
    std::vector<Candidate> found;
    auto consider = [&](const fs::path& p) {
        std::string rel = p.lexically_relative(dir).generic_string();
        if (!isAudio(p)) {
            ImportItem it;
            it.source = p;
            it.status = "skipped";
            it.detail = "not an mp3 or wav file";
            rep.items.push_back(it);
            ++rep.skipped;
            return;
        }
        Candidate c;
        c.path = p;
        c.rel = rel;
        std::string sub = fs::path(rel).parent_path().generic_string();
        std::string f = folderName;
        if (!sub.empty()) {
            std::string joined;
            for (const fs::path& part : fs::path(sub)) joined += (joined.empty() ? "" : " - ") + part.string();
            f += " - " + joined;
        }
        c.folder = sanitizeName(f);
        found.push_back(c);
    };
    if (opts.recursive) {
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (!it->path().filename().empty() && it->path().filename().string()[0] == '.') {
                if (it->is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(ec)) consider(it->path());
        }
    } else {
        for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
            if (e.path().filename().string()[0] == '.') continue;
            if (e.is_regular_file(ec)) consider(e.path());
        }
    }
    std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) { return naturalLess(a.rel, b.rel); });

    std::map<std::string, fs::path> known;
    if (opts.dedupe) known = existingHashes(lib);

    const int total = int(found.size());
    int done = 0;
    std::vector<fs::path> order;                                     // track folders for the playlist, in scan order
    for (const Candidate& c : found) {
        if (progress) progress(done, total, c.rel);                  // the callback may request the cancel, so check afterwards
        if (cancel && cancel->load()) { rep.cancelled = true; break; }
        ++done;

        ImportItem item;
        item.source = c.path;
        std::string err, hash;
        uintmax_t size = 0;
        if (!hashFile(c.path, &hash, &size, &err)) {
            item.status = "failed"; item.detail = err; ++rep.failed; rep.items.push_back(item); continue;
        }
        auto dup = known.find(hash);
        if (dup != known.end()) {
            item.status = "duplicate"; item.trackDir = dup->second; ++rep.duplicates;
            rep.items.push_back(item);
            order.push_back(dup->second);
            continue;
        }
        AudioInfo info;
        if (!probeAudio(c.path, &info, &err)) {
            item.status = "failed"; item.detail = err; ++rep.failed; rep.items.push_back(item); continue;
        }
        if (opts.dryRun) {
            item.status = "would import"; ++rep.imported; rep.items.push_back(item); continue;
        }

        std::string stem = c.path.stem().string();
        fs::path tdir = lib.createTrackDir(c.folder, stem, &err);
        if (tdir.empty()) {
            item.status = "failed"; item.detail = err; ++rep.failed; rep.items.push_back(item); continue;
        }
        bool good = moveOrCopy(c.path, tdir / ("track" + lowerExt(c.path)), opts.mode, &err);

        if (good) {
            Track t;
            t.dir = tdir;
            t.meta.addComment("trakcrate meta");
            std::string title = tag(info, "title");
            t.meta.set("title", title.empty() ? stem : title);
            t.meta.set("artist", tag(info, "artist"));
            t.meta.set("album", tag(info, "album"));
            t.meta.set("genre", tag(info, "genre"));
            long long year = leadingNumber(tag(info, "date").empty() ? tag(info, "year") : tag(info, "date"));
            if (year > 0) t.meta.setInt("year", year);
            long long tn = leadingNumber(tag(info, "track"));
            if (tn > 0) t.meta.setInt("track_number", tn);
            std::string bpm = tag(info, "bpm").empty() ? tag(info, "tbpm") : tag(info, "bpm");
            if (!bpm.empty()) t.meta.set("bpm_tag", bpm);
            t.meta.set("comment", tag(info, "comment"));
            t.meta.setInt("duration_ms", (long long)(info.durationSec * 1000 + 0.5));
            t.meta.setInt("sample_rate", info.sampleRate);
            t.meta.setInt("bit_depth", info.bitDepth);
            t.meta.setInt("bitrate", info.bitrate);
            t.meta.setInt("channels", info.channels);
            t.meta.set("container", info.container);
            t.meta.set("original_filename", c.path.filename().string());
            t.meta.setInt("file_size", (long long)size);
            t.meta.set("audio_sha256", hash);
            t.source.addComment("trakcrate source");
            for (const char* g : {"meta", "beatgrid", "cues", "waveform", "artwork"}) setOrigin(t.source, g, "generated");
            t.source.set("imported_from", c.path.string());
            t.source.set("imported_at", nowIso());
            good = saveMeta(t, &err) && saveSource(t, &err);
            if (good && !info.picture.empty()) good = atomicWrite(tdir / ("artwork." + info.pictureExt), info.picture, &err);
        }
        if (!good) {
            fs::remove_all(tdir, ec);                                // leave nothing half made
            item.status = "failed"; item.detail = err; ++rep.failed; rep.items.push_back(item); continue;
        }
        if (opts.analyse) {
            AnalyseResult ar = analyseTrack(tdir, AnalyseOptions(), cancel);
            if (!ar.ok) item.detail = "analysis failed: " + ar.error;          // the track is kept, it can be analysed later
        }
        item.status = "imported"; item.trackDir = tdir; ++rep.imported;
        rep.items.push_back(item);
        order.push_back(tdir);
        known[hash] = tdir;                                          // a second copy in the same batch is a duplicate too
    }
    if (progress) progress(done, total, std::string());

    // playlist
    if (!opts.noPlaylist && !opts.dryRun && !order.empty()) {          // nothing imported: no empty playlist, and replace deletes nothing
        std::string perr;
        bool ok = true;
        if (lib.hasPlaylist(playlist) && opts.replace && !opts.append) ok = lib.deletePlaylist(playlist, &perr);
        if (ok && !lib.hasPlaylist(playlist)) ok = lib.createPlaylist(playlist, &perr);
        if (ok) {
            std::set<fs::path> present;                              // append: tracks already in the playlist are not added again
            for (const fs::path& p : lib.playlistTracks(playlist)) present.insert(fs::weakly_canonical(p, ec));
            std::vector<fs::path> add;
            for (const fs::path& p : order) {
                fs::path canon = fs::weakly_canonical(p, ec);
                if (present.insert(canon).second) add.push_back(p);
            }
            if (!add.empty()) ok = lib.addTracksToPlaylist(playlist, add, -1, &perr);
        }
        if (!ok) { rep.error = perr; return rep; }
        rep.playlist = playlist;
        rep.playlistOrder = order;
    }
    rep.ok = true;
    return rep;
}

} // namespace trakcrate
