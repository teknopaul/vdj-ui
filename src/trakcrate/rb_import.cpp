#include "rb_import.h"

#include "analysis.h"
#include "analysis_wave.h"
#include "audio_probe.h"
#include "natsort.h"
#include "settings_files.h"
#include "settings_sets.h"
#include "sha256.h"
#include "track.h"

#include <cdj/anlz.h>
#include "deck_edits.h"
#include <cdj/pdb.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <set>

namespace trakcrate {

namespace {

struct RbTrack {
    pdb_track_t t{};
    std::string title, filename, filePath, comment, dateAdded, releaseDate, mixName, analyzePath;
};

std::string str(const char* s) { return s ? std::string(s) : std::string(); }

std::string nowIso()
{
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

std::string fixed(double v, int decimals)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

// A path from the database ("/Contents/a/b.mp3"): relative to the drive, never escaping it.
bool safeRelative(const std::string& p, fs::path* out)
{
    fs::path rel(p);
    if (rel.is_absolute()) rel = rel.relative_path();
    for (const fs::path& part : rel)
        if (part == "..") return false;
    if (rel.empty()) return false;
    *out = rel;
    return true;
}

bool hashOf(const fs::path& p, std::string* hex)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    Sha256 h;
    char buf[1 << 16];
    while (f.read(buf, sizeof buf) || f.gcount() > 0) h.update(buf, size_t(f.gcount()));
    *hex = h.finishHex();
    return true;
}

std::map<std::string, fs::path> existingHashes(const Library& lib)
{
    std::map<std::string, fs::path> idx;
    for (const std::string& f : lib.listFolders())
        for (const std::string& t : lib.listTracks(f)) {
            fs::path dir = lib.trackDir(f, t);
            std::string text;
            if (!readFile(dir / "meta.txt", &text)) continue;
            std::string h = KvDoc::parse(text).get("audio_sha256");
            if (!h.empty()) idx.emplace(h, dir);
        }
    return idx;
}

// "<drive>:<path on the drive>" and the file size of tracks imported from a drive before, so a repeated or resumed import does not read the
// audio of the tracks it already has (a slow or worn stick may take minutes for what the hash would re-read).
std::map<std::string, std::pair<uintmax_t, fs::path>> existingSources(const Library& lib)
{
    std::map<std::string, std::pair<uintmax_t, fs::path>> idx;
    for (const std::string& f : lib.listFolders())
        for (const std::string& t : lib.listTracks(f)) {
            fs::path dir = lib.trackDir(f, t);
            std::string text;
            if (!readFile(dir / "source.txt", &text)) continue;
            const std::string from = KvDoc::parse(text).get("imported_from");
            if (from.empty() || !readFile(dir / "meta.txt", &text)) continue;
            const long long size = KvDoc::parse(text).getInt("file_size", -1);
            if (size >= 0) idx.emplace(from, std::make_pair(uintmax_t(size), dir));
        }
    return idx;
}

struct NamedMap {
    std::map<uint32_t, std::string> names;
    static int cb(const pdb_named_t* r, void* ud) { static_cast<NamedMap*>(ud)->names[r->id] = str(r->name); return 0; }
    std::string get(uint32_t id) const { auto it = names.find(id); return it == names.end() ? std::string() : it->second; }
};

struct Tables {
    NamedMap genres, artists, labels, keys, colors, artwork;
    std::map<uint32_t, std::string> albums;
    std::vector<RbTrack> tracks;
    std::vector<pdb_playlist_t> playlistNodes;
    std::vector<std::string> playlistNames;
    std::vector<pdb_playlist_entry_t> entries;
};

bool readTables(const fs::path& pdbPath, Tables* tb, std::string* err)
{
    uint32_t len = 0;
    uint8_t* data = pdb_read_file(pdbPath.string().c_str(), &len);
    if (!data) { *err = "cannot read " + pdbPath.string(); return false; }
    struct Free { uint8_t* p; ~Free() { std::free(p); } } guard{data};
    pdb_parse_genres(data, len, NamedMap::cb, &tb->genres);
    pdb_parse_artists(data, len, NamedMap::cb, &tb->artists);
    pdb_parse_labels(data, len, NamedMap::cb, &tb->labels);
    pdb_parse_keys(data, len, NamedMap::cb, &tb->keys);
    pdb_parse_colors(data, len, NamedMap::cb, &tb->colors);
    pdb_parse_artwork(data, len, NamedMap::cb, &tb->artwork);
    pdb_parse_albums(data, len, [](const pdb_album_t* r, void* ud) { static_cast<Tables*>(ud)->albums[r->id] = str(r->name); return 0; }, tb);
    pdb_parse_playlists(data, len, [](const pdb_playlist_t* r, void* ud) {
        Tables* t = static_cast<Tables*>(ud);
        t->playlistNodes.push_back(*r);
        t->playlistNodes.back().name = nullptr;
        t->playlistNames.push_back(str(r->name));
        return 0;
    }, tb);
    pdb_parse_playlist_entries(data, len, [](const pdb_playlist_entry_t* r, void* ud) { static_cast<Tables*>(ud)->entries.push_back(*r); return 0; }, tb);
    if (pdb_parse(data, len, [](const pdb_track_t* t, void* ud) {
            RbTrack r;
            r.t = *t;
            r.title = str(t->title); r.filename = str(t->filename); r.filePath = str(t->file_path); r.comment = str(t->comment);
            r.dateAdded = str(t->date_added); r.releaseDate = str(t->release_date); r.mixName = str(t->mix_name); r.analyzePath = str(t->analyze_path);
            r.t.title = r.t.filename = r.t.file_path = r.t.comment = r.t.date_added = r.t.release_date = r.t.mix_name = r.t.analyze_path = nullptr;
            static_cast<Tables*>(ud)->tracks.push_back(std::move(r));
            return 0;
        }, tb) != 0) {
        *err = "export.pdb could not be read";
        return false;
    }
    return true;
}

struct Pl {
    uint32_t id = 0;
    std::string name;
    std::vector<uint32_t> trackIds;           // in the playlist's order
};

// Leaf playlists with their path names, tracks in order, sorted by name (natural order).
std::vector<Pl> collectPlaylists(const Tables& tb)
{
    std::map<uint32_t, size_t> byId;
    for (size_t i = 0; i < tb.playlistNodes.size(); ++i) byId[tb.playlistNodes[i].id] = i;
    std::vector<Pl> out;
    for (size_t i = 0; i < tb.playlistNodes.size(); ++i) {
        if (tb.playlistNodes[i].is_folder) continue;
        std::vector<std::string> parts;
        size_t at = i;
        for (int depth = 0; depth < 16; ++depth) {
            parts.push_back(tb.playlistNames[at]);
            auto p = byId.find(tb.playlistNodes[at].parent_id);
            if (tb.playlistNodes[at].parent_id == 0 || p == byId.end()) break;
            at = p->second;
        }
        Pl pl;
        pl.id = tb.playlistNodes[i].id;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) pl.name += (pl.name.empty() ? "" : " - ") + *it;
        pl.name = sanitizeName(pl.name);
        std::vector<pdb_playlist_entry_t> es;
        for (const pdb_playlist_entry_t& e : tb.entries) if (e.playlist_id == pl.id) es.push_back(e);
        std::sort(es.begin(), es.end(), [](const pdb_playlist_entry_t& a, const pdb_playlist_entry_t& b) { return a.entry_index < b.entry_index; });
        for (const pdb_playlist_entry_t& e : es) pl.trackIds.push_back(e.track_id);
        out.push_back(std::move(pl));
    }
    std::stable_sort(out.begin(), out.end(), [](const Pl& a, const Pl& b) { return naturalLess(a.name, b.name); });
    return out;
}

// ---- ANLZ -> trakcrate files ------------------------------------------------------------------------------------------------------------

struct Anlz {
    anlz_track_t* t = nullptr;
    ~Anlz() { if (t) anlz_track_close(t); }
};

void be16(std::vector<uint16_t>& v, const uint8_t* p) { v.push_back(uint16_t(p[0] << 8 | p[1])); }

bool realWaveform(anlz_track_t* a, Waveform* w)
{
    anlz_waveform_t wf;
    if (anlz_track_waveform(a, ANLZ_WAVE_PREVIEW, &wf) == ANLZ_OK && wf.count > 0) w->pwav.assign(wf.data, wf.data + wf.count * wf.entry_bytes);
    if (anlz_track_waveform(a, ANLZ_WAVE_TINY, &wf) == ANLZ_OK && wf.count > 0) w->pwv2.assign(wf.data, wf.data + wf.count * wf.entry_bytes);
    if (anlz_track_waveform(a, ANLZ_WAVE_DETAIL, &wf) == ANLZ_OK && wf.count > 0) w->pwv3.assign(wf.data, wf.data + wf.count * wf.entry_bytes);
    if (anlz_track_waveform(a, ANLZ_WAVE_COLOR_DETAIL, &wf) == ANLZ_OK && wf.count > 0 && wf.entry_bytes == 2)
        for (uint32_t i = 0; i < wf.count; ++i) be16(w->pwv5, wf.data + 2 * i);
    return !w->pwav.empty() && !w->pwv3.empty();
}

} // namespace

std::vector<RbPlaylistInfo> listRekordboxPlaylists(const fs::path& root, std::string* err)
{
    std::vector<RbPlaylistInfo> out;
    Tables tb;
    std::string e;
    if (!readTables(root / "PIONEER/rekordbox/export.pdb", &tb, &e)) { if (err) *err = e; return out; }
    for (const Pl& p : collectPlaylists(tb)) {
        RbPlaylistInfo i;
        i.id = p.id;
        i.name = p.name;
        i.tracks = int(p.trackIds.size());
        out.push_back(i);
    }
    return out;
}

bool rekordboxPlaylistKnown(const Library& lib, const RbPlaylistInfo& p) { return isSafeName(p.name) && lib.hasPlaylist(p.name); }

bool rekordboxFolderKnown(const Library& lib, const RbPlaylistInfo& p)
{
    const std::vector<std::string> f = lib.listFolders();
    return std::find(f.begin(), f.end(), p.name) != f.end();
}

bool isRekordboxDrive(const fs::path& root)
{
    std::error_code ec;
    return fs::is_regular_file(root / "PIONEER" / "rekordbox" / "export.pdb", ec);
}

RbImportReport importRekordboxDrive(const Library& lib, const fs::path& rootIn, const RbImportOptions& opts, const ImportProgress& progress,
                                    const std::atomic<bool>* cancel)
{
    RbImportReport rep;
    std::error_code ec;
    const fs::path root = fs::absolute(rootIn, ec).lexically_normal();
    if (!isRekordboxDrive(root)) { rep.error = "not a rekordbox drive (no PIONEER/rekordbox/export.pdb): " + rootIn.string(); return rep; }
    Tables tb;
    if (!readTables(root / "PIONEER/rekordbox/export.pdb", &tb, &rep.error)) return rep;
    rep.tracks = int(tb.tracks.size());

    std::map<std::string, fs::path> known;
    std::map<std::string, std::pair<uintmax_t, fs::path>> sources;
    if (opts.dedupe) { known = existingHashes(lib); sources = existingSources(lib); }
    const std::vector<Pl> lists = collectPlaylists(tb);
    std::map<uint32_t, std::string> folderOfTrack;                 // drive track id -> the playlist folder that owns the audio
    if (opts.playlistFolders)
        for (const Pl& p : lists) {
            if (opts.folderOff.count(p.id)) continue;
            for (uint32_t id : p.trackIds) folderOfTrack.emplace(id, p.name);
        }
    const bool pathFolders = opts.looseByPath || lists.empty();     // no playlists at all: the drive's own folders are all there is
    std::set<std::string> madeFolders;
    std::map<uint32_t, fs::path> dirOfId;                          // drive track id -> master folder
    const std::string topFolder = sanitizeName(opts.folder.empty() ? "rekordbox" : opts.folder);
    const std::string driveName = root.filename().string();
    // A playlist is created as soon as every track of it has been dealt with, so it shows up in the library while the import still runs.
    std::vector<std::set<uint32_t>> waiting(lists.size());          // track ids of each playlist not yet dealt with
    std::map<uint32_t, std::vector<size_t>> playlistsOfTrack;
    std::vector<bool> madePlaylist(lists.size(), false);
    for (size_t i = 0; i < lists.size(); ++i)
        for (uint32_t id : lists[i].trackIds) { waiting[i].insert(id); playlistsOfTrack[id].push_back(i); }
    auto makePlaylist = [&](size_t i) {
        if (madePlaylist[i] || !opts.playlists || opts.dryRun || opts.playlistOff.count(lists[i].id)) return;
        madePlaylist[i] = true;
        const Pl& pl = lists[i];
        std::vector<fs::path> order;
        for (uint32_t id : pl.trackIds) {
            auto d = dirOfId.find(id);
            if (d != dirOfId.end()) order.push_back(d->second);
        }
        if (order.empty()) return;
        std::string perr;
        if (pl.name.empty() || !isSafeName(pl.name)) { rep.warnings.push_back("playlist skipped, bad name: " + pl.name); return; }
        if (lib.hasPlaylist(pl.name)) {                      // an existing playlist keeps its tracks and order; only tracks it lacks are appended
            std::set<fs::path> have;
            for (const fs::path& p : lib.playlistTracks(pl.name)) have.insert(p.lexically_normal());
            std::vector<fs::path> fresh;
            for (const fs::path& p : order)
                if (have.insert(p.lexically_normal()).second) fresh.push_back(p);
            if (fresh.empty()) rep.warnings.push_back("playlist \"" + pl.name + "\" already exists, nothing new to add");
            else if (!lib.addTracksToPlaylist(pl.name, fresh, -1, &perr)) rep.warnings.push_back("playlist \"" + pl.name + "\": " + perr);
            return;
        }
        if (lib.createPlaylist(pl.name, &perr) && lib.addTracksToPlaylist(pl.name, order, -1, &perr)) ++rep.playlists;
        else rep.warnings.push_back("playlist \"" + pl.name + "\": " + perr);
    };
    bool havePrev = false;
    uint32_t prevId = 0;
    auto dealtWith = [&](uint32_t id) {
        auto p = playlistsOfTrack.find(id);
        if (p == playlistsOfTrack.end()) return;
        for (size_t i : p->second) {
            waiting[i].erase(id);
            if (waiting[i].empty()) makePlaylist(i);
        }
    };

    const int total = int(tb.tracks.size());
    int done = 0;

    for (const RbTrack& rt : tb.tracks) {
        if (havePrev) dealtWith(prevId);                             // the previous track has an outcome by now (stored, duplicate, missing or failed)
        havePrev = true;
        prevId = rt.t.id;
        if (progress) progress(done, total, rt.title);
        if (cancel && cancel->load()) { rep.cancelled = true; break; }
        ++done;

        fs::path rel;
        if (!safeRelative(rt.filePath, &rel)) { ++rep.failed; rep.warnings.push_back("unsafe path for \"" + rt.title + "\": " + rt.filePath); continue; }
        const fs::path audio = root / rel;
        if (!fs::is_regular_file(audio, ec)) {
            ++rep.missingAudio;
            rep.warnings.push_back("audio missing, track skipped: " + rt.filePath);
            continue;
        }
        if (opts.dedupe) {                                   // imported from this drive before, same size: no need to read it again
            auto s = sources.find(driveName + ":" + rt.filePath);
            if (s != sources.end() && s->second.first == fs::file_size(audio, ec) && !ec) {
                dirOfId[rt.t.id] = s->second.second;
                ++rep.duplicates;
                continue;
            }
        }
        std::string hash;
        if (!hashOf(audio, &hash)) { ++rep.failed; rep.warnings.push_back("cannot read " + audio.string()); continue; }
        auto dup = known.find(hash);
        if (dup != known.end()) { dirOfId[rt.t.id] = dup->second; ++rep.duplicates; continue; }
        if (opts.dryRun) { ++rep.imported; continue; }

        AudioInfo info;
        std::string err;
        if (!probeAudio(audio, &info, &err)) { ++rep.failed; rep.warnings.push_back(rt.filePath + ": " + err); continue; }

        // master folder from the path under Contents/
        std::string folder = topFolder;
        auto owner = folderOfTrack.find(rt.t.id);
        if (owner != folderOfTrack.end()) {
            folder = owner->second;
        } else if (pathFolders) {
            fs::path sub = rel.parent_path();
            std::vector<std::string> parts;
            for (const fs::path& p : sub) parts.push_back(p.string());
            if (!parts.empty() && (parts[0] == "Contents" || parts[0] == "CONTENTS")) parts.erase(parts.begin());
            if (!parts.empty()) {
                folder.clear();
                for (const std::string& p : parts) folder += (folder.empty() ? "" : " - ") + p;
                folder = sanitizeName(folder);
            }
        }
        const fs::path tdir = lib.createTrackDir(folder, audio.stem().string(), &err);
        if (tdir.empty()) { ++rep.failed; rep.warnings.push_back(rt.filePath + ": " + err); continue; }
        std::string ext = audio.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        bool good;
        if (opts.mode == ImportMode::Link) {
            fs::create_symlink(audio, tdir / ("track" + ext), ec);
            good = !ec;
            if (!good) err = "cannot link: " + ec.message();
        } else {
            fs::copy_file(audio, tdir / ("track" + ext), fs::copy_options::none, ec);
            good = !ec;
            if (!good) err = "cannot copy: " + ec.message();
        }

        Track t;
        t.dir = tdir;
        bool grid = false, cues = false, wave = false, art = false, anlzKept = false;
        if (good) {
            t.meta.addComment("trakcrate meta (imported from a rekordbox drive)");
            t.meta.set("title", rt.title.empty() ? audio.stem().string() : rt.title);
            t.meta.set("artist", tb.artists.get(rt.t.artist_id));
            auto al = tb.albums.find(rt.t.album_id);
            t.meta.set("album", al == tb.albums.end() ? std::string() : al->second);
            t.meta.set("genre", tb.genres.get(rt.t.genre_id));
            t.meta.set("label", tb.labels.get(rt.t.label_id));
            t.meta.set("key", tb.keys.get(rt.t.key_id));
            if (rt.t.remixer_id) t.meta.set("remixer", tb.artists.get(rt.t.remixer_id));
            if (!rt.mixName.empty()) t.meta.set("mix_name", rt.mixName);
            if (rt.t.year) t.meta.setInt("year", rt.t.year);
            if (rt.t.track_number) t.meta.setInt("track_number", rt.t.track_number);
            if (rt.t.bpm_x100) t.meta.set("bpm_tag", fixed(rt.t.bpm_x100 / 100.0, 2));
            t.meta.setInt("rating", std::min<int>(rt.t.rating, 5));
            if (rt.t.color_id) t.meta.set("color", tb.colors.get(rt.t.color_id));
            t.meta.set("comment", rt.comment);
            if (!rt.dateAdded.empty()) t.meta.set("date_added", rt.dateAdded);
            if (!rt.releaseDate.empty()) t.meta.set("release_date", rt.releaseDate);
            t.meta.setInt("duration_ms", rt.t.duration_secs ? (long long)rt.t.duration_secs * 1000 : (long long)(info.durationSec * 1000 + 0.5));
            t.meta.setInt("sample_rate", info.sampleRate);
            t.meta.setInt("bit_depth", info.bitDepth);
            t.meta.setInt("bitrate", info.bitrate);
            t.meta.setInt("channels", info.channels);
            t.meta.set("container", info.container);
            t.meta.set("original_filename", audio.filename().string());
            t.meta.setInt("file_size", (long long)fs::file_size(audio, ec));
            t.meta.set("audio_sha256", hash);
            t.meta.setInt("rekordbox_track_id", rt.t.id);
            t.source.addComment("trakcrate source");
            for (const char* g : {"meta"}) setOrigin(t.source, g, "rekordbox");
            for (const char* g : {"beatgrid", "cues", "waveform", "artwork"}) setOrigin(t.source, g, "generated");
            t.source.set("imported_from", driveName + ":" + rt.filePath);
            t.source.set("imported_at", nowIso());
            t.source.setInt("pdb_track_id", rt.t.id);

            // analysis files
            Anlz a;
            if (!rt.analyzePath.empty() && anlz_track_open(root.string().c_str(), rt.analyzePath.c_str(), &a.t) == ANLZ_OK && a.t) {
                BeatGrid g;
                bool variable = false;
                if (opts.beatgrid && gridFromAnlz(a.t, &g, &variable, pqtzGridOffsetMs(root.string().c_str(), rt.analyzePath.c_str()))) {
                    t.grid.addComment("trakcrate beatgrid (from rekordbox)");
                    t.hasGrid = true;
                    setBeatGrid(t.grid, g);
                    t.grid.set("analyser", "rekordbox");
                    if (variable) {
                        t.grid.set("variable_tempo", "1");
                        rep.warnings.push_back(rt.title + ": variable tempo grid, trakcrate keeps the first tempo (the original ANLZ files are kept in anlz/)");
                    }
                    setOrigin(t.source, "beatgrid", "rekordbox");
                    grid = true;
                }
                if (opts.cues) {
                    std::vector<Cuemark> marks = cuesFromAnlz(a.t, g);
                    if (!marks.empty()) {
                        t.hasCuemarks = true;
                        t.cuemarks.comments.push_back("# imported from rekordbox");
                        t.cuemarks.marks = std::move(marks);
                        setOrigin(t.source, "cues", "rekordbox");
                        cues = true;
                    }
                }
                if (opts.waveform) {
                    Waveform w;
                    if (realWaveform(a.t, &w) && atomicWrite(tdir / "waveform.bin", serialiseWaveform(w), &err)) {
                        setOrigin(t.source, "waveform", "rekordbox");
                        wave = true;
                    }
                }
                if (opts.keepAnlz) {
                    fs::create_directories(tdir / "anlz", ec);
                    const struct { anlz_file_t which; const char* name; } files[] = {{ANLZ_FILE_DAT, "ANLZ0000.DAT"}, {ANLZ_FILE_EXT, "ANLZ0000.EXT"}, {ANLZ_FILE_2EX, "ANLZ0000.2EX"}};
                    for (const auto& f : files) {
                        char p[1024];
                        if (anlz_resolve_path(root.string().c_str(), rt.analyzePath.c_str(), f.which, p, sizeof p) != 0) continue;
                        if (fs::is_regular_file(p, ec)) { fs::copy_file(p, tdir / "anlz" / f.name, fs::copy_options::overwrite_existing, ec); anlzKept = anlzKept || !ec; }
                    }
                }
            } else if (!rt.analyzePath.empty()) {
                rep.warnings.push_back(rt.title + ": analysis files not readable (" + rt.analyzePath + ")");
            }

            // artwork: the drive's file, else the picture inside the audio
            if (opts.artwork) {
                fs::path artFile;
                std::string artRel = tb.artwork.get(rt.t.artwork_id);
                fs::path rp;
                if (rt.t.artwork_id && !artRel.empty() && safeRelative(artRel, &rp) && fs::is_regular_file(root / rp, ec)) artFile = root / rp;
                if (!artFile.empty()) {
                    fs::copy_file(artFile, tdir / "artwork.jpg", fs::copy_options::overwrite_existing, ec);
                    if (!ec) { setOrigin(t.source, "artwork", "rekordbox"); art = true; }
                } else if (!info.picture.empty()) {
                    atomicWrite(tdir / ("artwork." + info.pictureExt), info.picture, &err);
                }
            }
            good = saveMeta(t, &err) && saveSource(t, &err);
            if (good && t.hasGrid) good = saveBeatGrid(t, &err);
            if (good && t.hasCuemarks) good = saveCuemarks(t, &err);
        }
        if (!good) {
            fs::remove_all(tdir, ec);
            ++rep.failed;
            rep.warnings.push_back(rt.filePath + ": " + err);
            continue;
        }
        madeFolders.insert(tdir.parent_path().filename().string());
        ++rep.imported;
        rep.withGrid += grid; rep.withCues += cues; rep.withWaveform += wave; rep.withArtwork += art; rep.withAnlz += anlzKept;
        dirOfId[rt.t.id] = tdir;
        known[hash] = tdir;
    }
    if (havePrev && !rep.cancelled) dealtWith(prevId);
    if (progress) progress(done, total, std::string());

    rep.folders = int(madeFolders.size());
    if (!rep.cancelled) for (size_t i = 0; i < lists.size(); ++i) makePlaylist(i);          // those not finished earlier (tracks the database lacks)

    // settings copied from the drive into a new settings set, only when asked
    if (!opts.dryRun && !rep.cancelled && (opts.settings || opts.deviceFiles || opts.profile)) {
        std::vector<std::string> only;
        if (opts.settings) { only.push_back("DEVSETTING.DAT"); only.push_back("MYSETTING.DAT"); }
        if (opts.deviceFiles) { only.push_back("RBFLTR.DAT"); only.push_back("exportExt.pdb"); }
        if (opts.profile) only.push_back("djprofile.nxs");
        const std::string setName = uniqueSettingsSetName(lib, opts.settingsSetName.empty() ? "rekordbox" : opts.settingsSetName);
        std::vector<std::string> found;
        std::string err;
        if (copySettingsSetFromDrive(lib, setName, root, &found, &err, &only)) {
            rep.settingsFiles = int(found.size());
            rep.settingsSet = setName;
        } else {
            rep.warnings.push_back("settings not copied: " + err);
        }
    }
    rep.ok = true;
    return rep;
}

} // namespace trakcrate
