#include "exporter.h"

#include "analysis.h"
#include "anlz_writer.h"
#include "mp3_index.h"
#include "artwork_paths.h"
#include "natsort.h"
#include "pdb_build.h"
#include "settings_files.h"
#include "sha256.h"

#include <cdj/anlz.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace trakcrate {

std::string fatSafeName(const std::string& name)
{
    std::string s;
    for (unsigned char c : name) s += (c < 0x20 || c == 0x7f || std::strchr("<>:\"/\\|?*", c)) ? '_' : char(c);
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    size_t b = s.find_first_not_of(' ');
    s = b == std::string::npos ? std::string() : s.substr(b);
    if (s.empty()) s = "_";
    std::string stem = s.substr(0, s.find('.'));
    std::string up;
    for (unsigned char c : stem) up += char(std::toupper(c));
    static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                     "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* r : reserved) if (up == r) { s.insert(stem.size(), "_"); break; }
    if (s.size() > 200) {
        size_t cut = 200;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s.resize(cut);
    }
    return s;
}

std::string rekordboxFolderName(const std::string& name, const char* fallback)
{
    std::string s;
    for (unsigned char c : name) s += (c < 0x20 || c == 0x7f || std::strchr("<>:\"/\\|?*", c)) ? '_' : char(c);
    size_t cut = 0, points = 0;
    while (cut < s.size() && points < 48) {                      // 48 code points, never in the middle of a UTF-8 sequence
        ++cut;
        while (cut < s.size() && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) ++cut;
        ++points;
    }
    s.resize(cut);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (!s.empty() && s.back() == '.') s.back() = '_';
    if (s.empty()) return fallback;
    return fatSafeName(s);                                       // reserved device names, leading spaces
}

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

struct GTrack {
    fs::path dir, audio;
    Track track;
    PlanTrack plan;
    Waveform wave;
    bool haveWave = false;
    BeatGrid grid;
    double durationMs = 0;
    std::vector<AnlzCue> hot;
    std::string artSource, artKey;
    uint64_t audioBytes = 0;
};

struct Gathered {
    std::vector<GTrack> tracks;
    ExportPlan plan;
    std::vector<std::string> problems;
};

std::string today()
{
    std::time_t t = std::time(nullptr);
    std::tm tm;
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

Gathered gather(const Library& lib, const ExportOptions& o)
{
    Gathered g;
    std::map<std::string, size_t> index;                       // canonical track dir -> track number
    std::set<std::string> usedPaths;                           // lower case Contents paths (FAT is case insensitive)
    std::map<std::string, std::string> artistDirs, albumDirs;  // lower case -> the first spelling used: FAT folders are case insensitive, rekordbox reuses it
    std::error_code ec;
    if (o.playlists.empty() && o.lists.empty()) g.problems.push_back("no playlist chosen");
    std::vector<ExportList> sources;
    for (const std::string& name : o.playlists) {
        if (!lib.hasPlaylist(name)) { g.problems.push_back("no such playlist: " + name); continue; }
        sources.push_back({name, lib.playlistTracks(name)});
    }
    for (const ExportList& l : o.lists) sources.push_back(l);
    for (const ExportList& src : sources) {
        const std::string& name = src.name;
        PlanPlaylist pl;
        pl.name = name;
        for (const fs::path& d : src.tracks) {
            const std::string key = fs::weakly_canonical(d, ec).string();
            auto it = index.find(key);
            if (it == index.end()) {
                GTrack t;
                t.dir = d;
                std::string err;
                if (!loadTrack(d, &t.track, &err)) { g.problems.push_back(name + ": " + err); continue; }
                t.audio = audioFileOf(d);
                if (t.audio.empty()) { g.problems.push_back("no audio file in " + d.string()); continue; }
                t.audioBytes = fs::file_size(t.audio, ec);
                const KvDoc& m = t.track.meta;
                PlanTrack& p = t.plan;
                const std::string stem = d.filename().string();
                p.title = m.get("title", stem);
                p.artist = m.get("artist");
                p.album = m.get("album");
                p.genre = m.get("genre");
                p.comment = m.get("comment");
                p.fileSize = uint32_t(t.audioBytes);
                p.sampleRate = uint32_t(m.getInt("sample_rate", 44100));
                p.bitrateKbps = uint32_t(m.getInt("bitrate", 0) / 1000);
                p.sampleDepth = uint32_t(m.getInt("bit_depth", 16));
                p.year = uint32_t(m.getInt("year", 0));
                p.trackNumber = uint32_t(m.getInt("track_number", 0));
                p.rating = uint8_t(std::max<long long>(0, std::min<long long>(5, m.getInt("rating", 0))));
                p.audioHash = m.get("audio_sha256");
                t.durationMs = double(m.getInt("duration_ms", 0));
                p.durationSecs = uint32_t(t.durationMs / 1000.0 + 0.5);
                const std::string imported = t.track.source.get("imported_at");
                p.dateAdded = imported.size() >= 10 ? imported.substr(0, 10) : today();
                t.grid = beatGridFrom(t.track.grid);
                p.bpmX100 = t.grid.empty() ? uint32_t(m.getDouble("bpm_tag", 0) * 100 + 0.5) : uint32_t(t.grid.bpm * 100 + 0.5);
                // The layout is rekordbox's: Contents/<artist>/<album>/<file>, from the track's tags. The library's own folders are not used here.
                std::string ext = lower(t.audio.extension().string());
                const std::string artistDir = artistDirs.emplace(lower(rekordboxFolderName(p.artist, "UnknownArtist")), rekordboxFolderName(p.artist, "UnknownArtist")).first->second;
                const std::string albumRaw = rekordboxFolderName(p.album, "UnknownAlbum");
                const std::string albumDir = albumDirs.emplace(lower(artistDir) + "/" + lower(albumRaw), albumRaw).first->second;
                std::string rel = "/Contents/" + artistDir + "/" + albumDir + "/" + fatSafeName(stem);
                std::string full = rel + ext;
                for (int n = 2; !usedPaths.insert(lower(full)).second; ++n) full = rel + " (" + std::to_string(n) + ")" + ext;
                p.filePath = full;
                p.fileName = full.substr(full.rfind('/') + 1);
                t.haveWave = loadWaveform(d, &t.wave);
                if (!t.haveWave && !o.allowUnanalysed) g.problems.push_back("not analysed: " + stem + " (" + d.string() + ")");
                for (const Cuemark& c : t.track.cuemarks.marks) {
                    if (c.hot < 'A' || c.hot > 'C') continue;
                    AnlzCue a;
                    a.hotCue = c.hot - 'A' + 1;
                    if (!std::isfinite(c.ms)) { g.problems.push_back("cuemark " + std::string(1, c.hot) + " has no time: " + stem); continue; }
                    // never negative (an imported cue can sit a hair before 0) and never past the end of the audio
                    double at = std::max(0.0, c.ms);
                    if (t.durationMs > 0) at = std::min(at, t.durationMs);
                    a.timeMs = uint32_t(at + 0.5);
                    t.hot.push_back(a);
                }
                std::sort(t.hot.begin(), t.hot.end(), [](const AnlzCue& a, const AnlzCue& b) { return a.hotCue < b.hotCue; });
                if (o.includeArtwork && o.encodeArtwork) {
                    for (const char* n : {"artwork.jpg", "artwork.jpeg", "artwork.png"}) {
                        if (readFile(d / n, &t.artSource)) { t.artKey = sha256Hex(t.artSource); break; }
                    }
                }
                index[key] = g.tracks.size();
                g.tracks.push_back(std::move(t));
                it = index.find(key);
            }
            pl.tracks.push_back(it->second);
        }
        g.plan.playlists.push_back(pl);
    }
    // artwork ids in the order buildPdb will give them: first seen
    std::map<std::string, uint32_t> artId;
    for (GTrack& t : g.tracks) {
        if (!t.artKey.empty()) {
            auto it = artId.find(t.artKey);
            if (it == artId.end()) it = artId.emplace(t.artKey, uint32_t(artId.size() + 1)).first;
            t.plan.artworkKey = t.artKey;
            t.plan.artworkPath = artworkPath(it->second);
        }
        g.plan.tracks.push_back(t.plan);
    }
    // optional files from another drive must exist
    auto need = [&](bool wanted, const char* rel) {
        if (wanted && !fs::is_regular_file(o.optionalFilesFrom / rel, ec)) g.problems.push_back(std::string("optional file missing: ") + (o.optionalFilesFrom / rel).string());
    };
    need(o.copyExportExt, "PIONEER/rekordbox/exportExt.pdb");
    need(o.copyRbfltr, "PIONEER/rekordbox/RBFLTR.DAT");
    need(o.copyUsbmng, "PIONEER/USBANLZ/USBMNG.DAT");
    need(o.copyDjprofile, "PIONEER/djprofile.nxs");
    std::string why;
    if (!o.devSetting.empty() && !verifySettingsFile(o.devSetting, &why)) g.problems.push_back("DEVSETTING.DAT template: " + why);
    if (!o.mySetting.empty() && !verifySettingsFile(o.mySetting, &why)) g.problems.push_back("MYSETTING.DAT template: " + why);
    return g;
}

uint64_t estimateMetadata(const Gathered& g)
{
    uint64_t n = 400000;                                            // database, settings, support files, pages
    for (const GTrack& t : g.tracks) {
        const uint64_t cols = t.haveWave ? t.wave.columns() : uint64_t(t.durationMs * 0.15 + 1);
        n += cols * 3 + 7200 + 2000 + 16384 + (t.grid.empty() ? 0 : uint64_t(t.durationMs / 400.0) * 10) + (t.artSource.empty() ? 0 : 40000);
    }
    return n;
}

// ---- staging directory ---------------------------------------------------------------------------------------------------------------

struct Staging {
    fs::path dir;
    ~Staging() { if (!dir.empty()) { std::error_code ec; fs::remove_all(dir, ec); } }
};

fs::path pickStagingBase(const ExportOptions& o, bool* tmpfs, std::vector<std::string>* warnings)
{
    std::error_code ec;
    *tmpfs = false;
    if (!o.stagingBase.empty()) { *tmpfs = false; return o.stagingBase; }
    for (const char* c : {"/dev/shm"}) if (fs::is_directory(c, ec) && ::access(c, W_OK) == 0) { *tmpfs = true; return c; }
    if (const char* x = std::getenv("XDG_RUNTIME_DIR")) if (fs::is_directory(x, ec) && ::access(x, W_OK) == 0) { *tmpfs = true; return x; }
    warnings->push_back("no tmpfs available: staging in the system temp directory");
    return fs::temp_directory_path(ec);
}

bool writeStaged(const fs::path& staging, const std::string& rel, const std::string& data, std::string* err)
{
    std::error_code ec;
    fs::create_directories((staging / rel).parent_path(), ec);
    return atomicWrite(staging / rel, data, err);
}

// ---- copying -------------------------------------------------------------------------------------------------------------------------

struct OutFile {
    std::string rel;               // path relative to the destination
    fs::path src;                  // source on disk, or empty when mem is used
    std::string mem;
    bool inMemory = false;
    std::string hash;              // sha256, known before the copy for staged files, learnt during it for audio
    uint64_t size = 0;
};

bool copyOne(const fs::path& dest, OutFile& f, const std::atomic<bool>* cancel, uint64_t* total, std::string* err, bool* cancelled)
{
    const fs::path out = dest / f.rel;
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    if (ec) { *err = "cannot create folder " + out.parent_path().string() + ": " + ec.message(); return false; }
    int fd = ::open(out.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) { *err = "cannot create " + out.string() + ": " + std::strerror(errno); return false; }
    Sha256 sha;
    uint64_t written = 0;
    auto put = [&](const char* p, size_t n) {
        sha.update(p, n);
        size_t done = 0;
        while (done < n) {
            ssize_t w = ::write(fd, p + done, n - done);
            if (w < 0) { if (errno == EINTR) continue; *err = "cannot write " + out.string() + ": " + std::strerror(errno); return false; }
            done += size_t(w);
        }
        written += n;
        return true;
    };
    bool ok = true;
    if (f.inMemory) {
        ok = put(f.mem.data(), f.mem.size());
    } else {
        std::ifstream in(f.src, std::ios::binary);
        if (!in) { *err = "cannot read " + f.src.string(); ok = false; }
        static thread_local std::string buf(1 << 20, '\0');
        while (ok && (in.read(&buf[0], std::streamsize(buf.size())) || in.gcount() > 0)) {
            if (cancel && cancel->load()) { *cancelled = true; ok = false; break; }
            ok = put(buf.data(), size_t(in.gcount()));
        }
        if (ok && in.bad()) { *err = "read error on " + f.src.string(); ok = false; }
    }
    if (ok && ::fsync(fd) != 0) { *err = "cannot sync " + out.string() + ": " + std::strerror(errno); ok = false; }
    ::close(fd);
    if (!ok) return false;
    const std::string h = sha.finishHex();
    if (!f.hash.empty() && f.hash != h) { *err = "source changed while copying: " + f.rel; return false; }
    f.hash = h;
    f.size = written;
    *total += written;
    return true;
}

std::string hashFile(const fs::path& p, std::string* err)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) { if (err) *err = "cannot read " + p.string(); return std::string(); }
    Sha256 h;
    static thread_local std::string buf(1 << 20, '\0');
    while (in.read(&buf[0], std::streamsize(buf.size())) || in.gcount() > 0) h.update(buf.data(), size_t(in.gcount()));
    return h.finishHex();
}

} // namespace

ExportSummary summariseExport(const Library& lib, const ExportOptions& opts)
{
    ExportSummary s;
    Gathered g = gather(lib, opts);
    s.tracks = int(g.tracks.size());
    for (const GTrack& t : g.tracks) s.audioBytes += t.audioBytes;
    s.metadataBytes = estimateMetadata(g);
    s.problems = g.problems;
    return s;
}

static const char* const kTopLevel[] = {"Contents", "PIONEER", "SHA256SUMS", "trakcrate-manifest.txt"};

std::vector<std::string> exportConflicts(const fs::path& dest)
{
    std::vector<std::string> found;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(dest, ec)) {
        const std::string name = e.path().filename().string();
        for (const char* mine : kTopLevel)
            if (lower(name) == lower(mine)) { found.push_back(name); break; }
    }
    std::sort(found.begin(), found.end());
    return found;
}

ExportResult exportDrive(const Library& lib, const ExportOptions& o, const ExportProgress& progress, const std::atomic<bool>* cancel)
{
    ExportResult res;
    auto fail = [&](const std::string& m) { res.error = m; return res; };
    std::error_code ec;

    if (o.templatePdb.empty()) return fail("no template export.pdb");
    if (o.dest.empty()) return fail("no destination");
    const bool destExisted = fs::exists(o.dest, ec);
    if (destExisted) {
        if (!fs::is_directory(o.dest, ec)) return fail("the destination is not a folder: " + o.dest.string());
        const std::vector<std::string> clash = exportConflicts(o.dest);
        if (!clash.empty()) {
            std::string names;
            for (const std::string& n : clash) names += (names.empty() ? "" : ", ") + n;
            return fail("the destination already has " + names + " (an earlier export?), remove it or choose another folder: " + o.dest.string());
        }
    }
    Gathered g = gather(lib, o);
    if (!g.problems.empty()) {
        std::string m = "cannot export:";
        for (size_t i = 0; i < g.problems.size() && i < 8; ++i) m += "\n  " + g.problems[i];
        if (g.problems.size() > 8) m += "\n  ... and " + std::to_string(g.problems.size() - 8) + " more";
        return fail(m);
    }
    if (g.tracks.empty()) return fail("the playlists are empty");
    res.tracks = int(g.tracks.size());
    res.playlists = int(g.plan.playlists.size());

    // space: the destination must hold audio and metadata, the staging area the metadata
    uint64_t audioBytes = 0;
    for (const GTrack& t : g.tracks) audioBytes += t.audioBytes;
    const uint64_t meta = estimateMetadata(g);
    {
        fs::path probe = o.dest;
        while (!probe.empty() && !fs::exists(probe, ec)) probe = probe.parent_path();
        fs::space_info sp = fs::space(probe.empty() ? fs::path(".") : probe, ec);
        if (!ec && sp.available < audioBytes + meta + o.minFreeBytes)
            return fail("not enough space on the destination: needs " + std::to_string((audioBytes + meta + o.minFreeBytes) >> 20) + " MiB, " +
                        std::to_string(sp.available >> 20) + " MiB free");
    }
    Staging stage;
    {
        bool tmpfs = false;
        fs::path base = pickStagingBase(o, &tmpfs, &res.warnings);
        res.stagedOnTmpfs = tmpfs;
        std::string err;
        stage.dir = makeTempDir("trakcrate-export", base, &err);
        if (stage.dir.empty()) return fail("cannot create the staging area: " + err);
        res.stagingUsed = stage.dir.string();
        fs::space_info sp = fs::space(stage.dir, ec);
        if (!ec && sp.available < meta + (8u << 20)) return fail("not enough space in the staging area " + base.string());
    }

    // ---- stage: database, analysis files, artwork, settings
    std::string err;
    BuiltPdb built;
    if (!buildPdb(g.plan, o.templatePdb, &built, &err)) return fail("database: " + err);
    std::vector<std::string> diff = verifyPdb(built.bytes, g.plan, built);
    if (!diff.empty()) return fail("the database does not say what the plan says: " + diff.front());
    std::vector<std::string> anlzDat;                          // staged analysis paths, for the check below
    for (size_t i = 0; i < g.tracks.size(); ++i) {
        const GTrack& t = g.tracks[i];
        AnlzTrackData d;
        d.audioPath = t.plan.filePath;
        d.includePvbr = o.includePvbr;
        d.includePwv5 = o.includePwv5;
        Waveform w = t.wave;
        if (!t.haveWave) {                                      // allowUnanalysed: a flat waveform of the right length
            const size_t cols = size_t(t.durationMs * Waveform::kColumnsPerSecond / 1000.0) + 1;
            w.pwv3.assign(cols, 0);
            w.pwv5.assign(cols, 0);
            w.pwav.assign(Waveform::kPreviewColumns, 0);
            w.pwv2.assign(Waveform::kTinyColumns, 0);
            res.warnings.push_back("not analysed, flat waveform and no beat grid: " + t.dir.filename().string());
        }
        setWaveforms(&d, w, o.includePwv4);                     // PWV4 comes from the true waveform: its layout is only partly understood, so it gets no spikes
        if (o.cuemarks.enabled && !t.track.cuemarks.marks.empty()) {
            const Waveform r = renderCuemarks(w, t.track.cuemarks.marks, t.durationMs, o.cuemarks);
            d.pwav = r.pwav;
            d.pwv2 = r.pwv2;
            d.pwv3 = r.pwv3;
            d.pwv5 = r.pwv5;
        }
        d.beats = beatsFromGrid(t.grid, t.durationMs, &d.beatSubUs);
        d.hotCues = t.hot;
        if (lower(t.audio.extension().string()) == ".mp3") {     // a VBR mp3 carries its seek index in the PVBR tag and in every cue
            const Mp3Index ix = indexMp3(t.audio);
            if (ix.ok && ix.vbr) {
                d.pvbr = ix.pvbr();
                for (AnlzCue& c : d.hotCues) { c.seek = true; c.bytePos = ix.offsetAtMs(c.timeMs); }
            }
        }
        AnlzFiles f;
        if (!buildAnlz(d, &f, &err)) return fail("analysis files for " + t.plan.title + ": " + err);
        std::string base = built.analyzePaths[i].substr(1);     // PIONEER/USBANLZ/Pxxx/yyyyyyyy/ANLZ0000.DAT
        if (!writeStaged(stage.dir, base, f.dat, &err) || !writeStaged(stage.dir, base.substr(0, base.size() - 3) + "EXT", f.ext, &err)) return fail("staging: " + err);
        anlzDat.push_back(base);
    }
    if (o.includeArtwork && o.encodeArtwork) {
        std::set<std::string> done;
        for (const GTrack& t : g.tracks) {
            if (t.artKey.empty() || !done.insert(t.artKey).second) continue;
            std::string full;
            if (!o.encodeArtwork(t.artSource, kArtworkSize, &full, &err)) {
                res.warnings.push_back("artwork of " + t.plan.title + " not usable: " + err);
                return fail("artwork of " + t.plan.title + ": " + err);
            }
            const std::string p = t.plan.artworkPath;           // /PIONEER/Artwork/00001/a1.jpg
            if (!writeStaged(stage.dir, p.substr(1), full, &err)) return fail("staging: " + err);
        }
    }
    if (o.includeSettings) {
        if (!writeStaged(stage.dir, "PIONEER/DEVSETTING.DAT", o.devSetting.empty() ? defaultDevSetting() : o.devSetting, &err) ||
            !writeStaged(stage.dir, "PIONEER/MYSETTING.DAT", o.mySetting.empty() ? defaultMySetting() : o.mySetting, &err))
            return fail("staging: " + err);
        if (!writeStaged(stage.dir, "PIONEER/djprofile.nxs", o.djprofile.empty() ? defaultDjprofile() : o.djprofile, &err) ||
            !writeStaged(stage.dir, "PIONEER/rekordbox/RBFLTR.DAT", o.rbfltr.empty() ? defaultRbfltr() : o.rbfltr, &err) ||
            !writeStaged(stage.dir, "PIONEER/rekordbox/exportExt.pdb", o.exportExt.empty() ? defaultExportExt() : o.exportExt, &err) ||
            !writeStaged(stage.dir, "PIONEER/USBANLZ/USBMNG.DAT", emptyUsbmng(), &err))
            return fail("staging: " + err);
    }
    std::vector<std::string> variant;
    auto copyOpt = [&](bool wanted, const char* rel, const char* outRel) {
        if (!wanted) return true;
        std::string bytes;
        if (!readFile(o.optionalFilesFrom / rel, &bytes, &err)) return false;
        variant.push_back(std::string("copied from another drive: ") + outRel);
        return writeStaged(stage.dir, outRel, bytes, &err);
    };
    if (!copyOpt(o.copyExportExt, "PIONEER/rekordbox/exportExt.pdb", "PIONEER/rekordbox/exportExt.pdb") ||
        !copyOpt(o.copyRbfltr, "PIONEER/rekordbox/RBFLTR.DAT", "PIONEER/rekordbox/RBFLTR.DAT") ||
        !copyOpt(o.copyUsbmng, "PIONEER/USBANLZ/USBMNG.DAT", "PIONEER/USBANLZ/USBMNG.DAT") ||
        !copyOpt(o.copyDjprofile, "PIONEER/djprofile.nxs", "PIONEER/djprofile.nxs"))
        return fail("staging: " + err);
    if (!writeStaged(stage.dir, "PIONEER/rekordbox/export.pdb", built.bytes, &err)) return fail("staging: " + err);

    // ---- verify the staged tree before anything touches the destination
    for (size_t i = 0; i < anlzDat.size(); ++i) {
        for (const char* ext : {"DAT", "EXT"}) {
            std::string rel = anlzDat[i].substr(0, anlzDat[i].size() - 3) + ext;
            anlz_t* a = nullptr;
            if (anlz_open_file((stage.dir / rel).c_str(), &a) != ANLZ_OK) return fail("staged analysis file does not parse: " + rel);
            const char* path = nullptr;
            const bool good = !anlz_is_damaged(a) && anlz_file_len_ok(a) && anlz_path(a, &path) == ANLZ_OK && g.tracks[i].plan.filePath == path;
            anlz_close(a);
            if (!good) return fail("staged analysis file is wrong: " + rel);
        }
    }
    if (o.includeSettings) {
        std::string bytes;
        readFile(stage.dir / "PIONEER/DEVSETTING.DAT", &bytes);
        if (!verifySettingsFile(bytes, &err)) return fail("DEVSETTING.DAT: " + err);
        readFile(stage.dir / "PIONEER/MYSETTING.DAT", &bytes);
        if (!verifySettingsFile(bytes, &err)) return fail("MYSETTING.DAT: " + err);
    }

    // ---- the list of files to write, in order: audio, other metadata, checksums, export.pdb last
    std::vector<OutFile> files;
    for (const GTrack& t : g.tracks) {
        OutFile f;
        f.rel = t.plan.filePath.substr(1);
        f.src = t.audio;
        files.push_back(f);
    }
    std::vector<std::string> staged;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(stage.dir, ec))
        if (e.is_regular_file(ec)) staged.push_back(e.path().lexically_relative(stage.dir).generic_string());
    std::sort(staged.begin(), staged.end());
    for (const std::string& rel : staged) {
        if (rel == "PIONEER/rekordbox/export.pdb") continue;
        OutFile f;
        f.rel = rel;
        f.src = stage.dir / rel;
        f.hash = hashFile(f.src, &err);
        if (f.hash.empty()) return fail(err);
        files.push_back(f);
    }
    const size_t metaFirst = files.size();
    OutFile pdb;
    pdb.rel = "PIONEER/rekordbox/export.pdb";
    pdb.src = stage.dir / pdb.rel;
    pdb.hash = sha256Hex(built.bytes);
    (void)metaFirst;

    if (!o.includeSettings) variant.push_back("no DEVSETTING.DAT / MYSETTING.DAT");
    if (!o.includeArtwork || !o.encodeArtwork) variant.push_back("no artwork");
    if (!o.includePvbr) variant.push_back("no PVBR tag");
    if (!o.includePwv4) variant.push_back("no PWV4 tag");
    if (!o.includePwv5) variant.push_back("no PWV5 tag");
    if (o.allowUnanalysed) variant.push_back("unanalysed tracks allowed");
    int total = int(files.size()) + 1 + (o.writeManifest ? 2 : 0);
    int done = 0;

    // ---- write
    if (!destExisted) fs::create_directories(o.dest, ec);
    auto cleanup = [&]() {
        std::error_code e2;
        for (const char* mine : kTopLevel) fs::remove_all(o.dest / mine, e2);          // only what this export wrote: the stick may hold other files
        if (!destExisted) fs::remove(o.dest, e2);
    };
    auto step = [&](OutFile& f) {
        if (cancel && cancel->load()) { res.cancelled = true; res.error = "cancelled"; return false; }
        if (progress) progress(done, total, f.rel);
        bool cancelled = false;
        if (!copyOne(o.dest, f, cancel, &res.bytesWritten, &err, &cancelled)) {
            res.cancelled = cancelled;
            res.error = cancelled ? "cancelled" : err;
            return false;
        }
        res.writeOrder.push_back(f.rel);
        ++done;
        return true;
    };
    for (OutFile& f : files) if (!step(f)) { cleanup(); return res; }
    if (o.writeManifest) {
        std::string sums;
        for (const OutFile& f : files) sums += f.hash + "  " + f.rel + "\n";
        sums += pdb.hash + "  " + pdb.rel + "\n";
        std::string manifest = "trakcrate export\ndate: " + today() + "\ntracks: " + std::to_string(res.tracks) + "\nplaylists:";
        for (const PlanPlaylist& p : g.plan.playlists) manifest += " \"" + p.name + "\"";
        manifest += "\nvariant: " + std::string(variant.empty() ? "full" : "");
        for (const std::string& v : variant) manifest += "\n  - " + v;
        manifest += "\nBytes that rekordbox files hold but trakcrate does not understand are copied from real drives or set to guessed values;\n"
                    "the list is doc/trakcrate/UNKNOWN_BYTES.md in the trakcrate sources.\n";
        OutFile a, b;
        a.rel = "SHA256SUMS"; a.mem = sums; a.inMemory = true;
        b.rel = "trakcrate-manifest.txt"; b.mem = manifest; b.inMemory = true;
        if (!step(a) || !step(b)) { cleanup(); return res; }
        files.push_back(a);
        files.push_back(b);
    }
    if (!step(pdb)) { cleanup(); return res; }
    files.push_back(pdb);
    res.files = int(files.size());
    {
        int fd = ::open(o.dest.c_str(), O_RDONLY | O_DIRECTORY);
        if (fd >= 0) { ::syncfs(fd); ::close(fd); }
    }
    if (progress) progress(done, total, std::string());

    if (o.verify) {
        for (const OutFile& f : files) {
            std::string herr;
            const std::string h = hashFile(o.dest / f.rel, &herr);
            if (h != f.hash) { res.error = "verification failed for " + f.rel + (herr.empty() ? "" : ": " + herr); cleanup(); return res; }
        }
        res.verified = true;
    }
    res.ok = true;
    return res;
}

} // namespace trakcrate
