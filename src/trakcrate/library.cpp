#include "library.h"

#include "natsort.h"
#include "settings_sets.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace trakcrate {

static bool fail(std::string* err, const std::string& msg)
{
    if (err) *err = msg;
    return false;
}

bool Library::create(const fs::path& root, std::string* err)
{
    std::error_code ec;
    for (const char* sub : {"track-master-copy", "playlists", "user-settings", "drives"}) {
        fs::create_directories(root / sub, ec);
        if (ec) return fail(err, "cannot create " + (root / sub).string() + ": " + ec.message());
    }
    return true;
}

bool Library::open(const fs::path& root, Library* out, std::string* err)
{
    std::error_code ec;
    if (!fs::is_directory(root / "track-master-copy", ec)) return fail(err, "not a trakcrate library: " + root.string());
    out->root_ = fs::absolute(root, ec).lexically_normal();
    std::string migrateErr;
    if (!migrateFlatSettings(out->root_, &migrateErr)) return fail(err, migrateErr);      // flat user-settings/ files become the set "imported"
    return true;
}

static std::vector<std::string> listDirs(const fs::path& dir)
{
    std::vector<std::string> names;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        std::string n = e.path().filename().string();
        if (!n.empty() && n[0] == '.') continue;                    // .trash, .trakcrate-history, temp files
        if (e.is_directory(ec)) names.push_back(n);
    }
    std::sort(names.begin(), names.end(), naturalLess);
    return names;
}

std::vector<std::string> Library::listFolders() const { return listDirs(masterDir()); }

std::vector<std::string> Library::listTracks(const std::string& folder) const
{
    if (!isSafeName(folder)) return {};
    return listDirs(masterDir() / folder);
}

fs::path Library::createTrackDir(const std::string& folder, const std::string& name, std::string* err) const
{
    std::string f = sanitizeName(folder), n = sanitizeName(name);
    std::error_code ec;
    fs::create_directories(masterDir() / f, ec);
    if (ec) { fail(err, "cannot create folder " + f + ": " + ec.message()); return fs::path(); }
    for (int i = 1; i < 10000; ++i) {
        fs::path p = masterDir() / f / (i == 1 ? n : n + " " + std::to_string(i));
        if (fs::create_directory(p, ec)) return p;                   // false (no error) when it exists
        if (ec) { fail(err, "cannot create " + p.string() + ": " + ec.message()); return fs::path(); }
    }
    fail(err, "too many tracks named " + n);
    return fs::path();
}

// ---- playlists ----------------------------------------------------------------------------------------------------------------------

std::vector<std::string> Library::listPlaylists() const { return listDirs(playlistsDir()); }

bool Library::hasPlaylist(const std::string& name) const
{
    std::error_code ec;
    return isSafeName(name) && fs::is_directory(playlistsDir() / name, ec);
}

bool Library::createPlaylist(const std::string& name, std::string* err) const
{
    if (!isSafeName(name)) return fail(err, "bad playlist name: " + name);
    std::error_code ec;
    if (fs::exists(playlistsDir() / name, ec)) return fail(err, "playlist exists: " + name);
    fs::create_directories(symlinksDir(name), ec);
    if (ec) return fail(err, "cannot create playlist " + name + ": " + ec.message());
    return true;
}

bool Library::deletePlaylist(const std::string& name, std::string* err) const
{
    if (!hasPlaylist(name)) return fail(err, "no such playlist: " + name);
    std::error_code ec;
    fs::remove_all(playlistsDir() / name, ec);                       // removes the links, never their targets
    if (ec) return fail(err, "cannot delete playlist " + name + ": " + ec.message());
    return true;
}

// "007_name" -> 7; names without a numeric prefix sort last.
static long long prefixOf(const std::string& n)
{
    size_t i = 0;
    while (i < n.size() && n[i] >= '0' && n[i] <= '9') ++i;
    if (i == 0 || i >= n.size() || n[i] != '_') return -1;
    return std::strtoll(n.substr(0, i).c_str(), nullptr, 10);
}

std::vector<std::string> Library::playlistEntries(const std::string& name) const
{
    std::vector<std::string> names;
    if (!hasPlaylist(name)) return names;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(symlinksDir(name), ec)) {
        std::string n = e.path().filename().string();
        if (!n.empty() && n[0] != '.' && e.is_symlink(ec)) names.push_back(n);
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        long long pa = prefixOf(a), pb = prefixOf(b);
        if ((pa < 0) != (pb < 0)) return pb < 0;
        if (pa != pb) return pa < pb;
        return naturalLess(a, b);
    });
    return names;
}

std::vector<fs::path> Library::playlistTracks(const std::string& name) const
{
    std::vector<fs::path> out;
    fs::path dir = symlinksDir(name);
    std::error_code ec;
    for (const std::string& n : playlistEntries(name)) {
        fs::path target = fs::read_symlink(dir / n, ec);
        if (ec) continue;
        out.push_back(target.is_absolute() ? target : (dir / target).lexically_normal());
    }
    return out;
}

// Renames the links to 000_x, 001_y ... in the given order. Two passes (to a temporary name first) so no name is ever overwritten.
bool Library::writeOrder(const std::string& name, const std::vector<std::string>& entries, std::string* err) const
{
    fs::path dir = symlinksDir(name);
    std::error_code ec;
    size_t width = std::max<size_t>(3, std::to_string(entries.size() ? entries.size() - 1 : 0).size());
    std::vector<std::string> finals;
    for (size_t i = 0; i < entries.size(); ++i) {
        std::string base = entries[i];
        size_t us = prefixOf(base) >= 0 ? base.find('_') : std::string::npos;
        if (us != std::string::npos) base = base.substr(us + 1);
        char num[32];
        std::snprintf(num, sizeof num, "%0*zu", int(width), i);
        finals.push_back(std::string(num) + "_" + base);
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i] == finals[i]) continue;
        fs::rename(dir / entries[i], dir / (".r" + std::to_string(i)), ec);
        if (ec) return fail(err, "cannot rename " + entries[i] + ": " + ec.message());
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i] == finals[i]) continue;
        fs::rename(dir / (".r" + std::to_string(i)), dir / finals[i], ec);
        if (ec) return fail(err, "cannot rename to " + finals[i] + ": " + ec.message());
    }
    return true;
}

bool Library::renumberPlaylist(const std::string& name, std::string* err) const
{
    if (!hasPlaylist(name)) return fail(err, "no such playlist: " + name);
    return writeOrder(name, playlistEntries(name), err);
}

bool Library::addToPlaylist(const std::string& name, const fs::path& trackDir, int position, std::string* err) const
{
    return addTracksToPlaylist(name, std::vector<fs::path>{trackDir}, position, err);
}

bool Library::addTracksToPlaylist(const std::string& name, const std::vector<fs::path>& trackDirs, int position, std::string* err) const
{
    if (!hasPlaylist(name)) return fail(err, "no such playlist: " + name);
    std::error_code ec;
    fs::path dir = symlinksDir(name);
    fs::path linkBase = fs::weakly_canonical(dir, ec);
    std::vector<std::string> order = playlistEntries(name);
    size_t at = (position < 0 || size_t(position) > order.size()) ? order.size() : size_t(position);
    std::vector<std::string> added;
    for (size_t i = 0; i < trackDirs.size(); ++i) {
        fs::path absTrack = fs::weakly_canonical(trackDirs[i], ec);
        if (ec || !fs::is_directory(absTrack)) {
            for (const std::string& n : added) fs::remove(dir / n, ec);        // all or nothing
            return fail(err, "not a track folder: " + trackDirs[i].string());
        }
        // The placeholder prefix is a number no real entry has; writeOrder strips it and numbers by position. Relative links keep
        // the library movable.
        std::string linkName = std::to_string(90000000 + i) + "_" + sanitizeName(absTrack.filename().string());
        fs::create_directory_symlink(absTrack.lexically_relative(linkBase), dir / linkName, ec);
        if (ec) {
            for (const std::string& n : added) fs::remove(dir / n, ec);
            return fail(err, "cannot create link " + linkName + ": " + ec.message());
        }
        added.push_back(linkName);
    }
    order.insert(order.begin() + long(at), added.begin(), added.end());
    return writeOrder(name, order, err);
}

bool Library::removeFromPlaylist(const std::string& name, int index, std::string* err) const
{
    std::vector<std::string> order = playlistEntries(name);
    if (index < 0 || size_t(index) >= order.size()) return fail(err, "no such playlist entry");
    std::error_code ec;
    fs::remove(symlinksDir(name) / order[size_t(index)], ec);
    if (ec) return fail(err, "cannot remove entry: " + ec.message());
    order.erase(order.begin() + index);
    return writeOrder(name, order, err);
}

bool Library::moveInPlaylist(const std::string& name, int from, int to, std::string* err) const
{
    std::vector<std::string> order = playlistEntries(name);
    if (from < 0 || size_t(from) >= order.size() || to < 0 || size_t(to) >= order.size()) return fail(err, "no such playlist entry");
    if (from == to) return true;
    std::string moved = order[size_t(from)];
    order.erase(order.begin() + from);
    order.insert(order.begin() + to, moved);
    return writeOrder(name, order, err);
}

bool Library::moveEntries(const std::string& name, const std::vector<int>& rowsIn, int to, std::string* err) const
{
    std::vector<std::string> order = playlistEntries(name);
    std::vector<int> rows = rowsIn;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    if (rows.empty() || to < 0 || size_t(to) > order.size()) return fail(err, "nothing to move, or a bad position");
    for (int r : rows)
        if (r < 0 || size_t(r) >= order.size()) return fail(err, "no such playlist entry");
    std::vector<std::string> block, rest;
    int before = 0;                                              // selected rows above the target: they leave the list before it
    for (size_t i = 0; i < order.size(); ++i) {
        if (std::binary_search(rows.begin(), rows.end(), int(i))) {
            block.push_back(order[i]);
            if (int(i) < to) ++before;
        } else {
            rest.push_back(order[i]);
        }
    }
    rest.insert(rest.begin() + (to - before), block.begin(), block.end());
    if (rest == order) return true;
    return writeOrder(name, rest, err);
}

// ---- user defined pendrives -----------------------------------------------------------------------------------------------------------

std::vector<std::string> Library::listDrives() const { return listDirs(drivesDir()); }

bool Library::hasDrive(const std::string& name) const
{
    std::error_code ec;
    return isSafeName(name) && fs::is_directory(drivesDir() / name, ec);
}

bool Library::createDrive(const std::string& name, std::string* err) const
{
    if (!isSafeName(name)) return fail(err, "bad pendrive name: " + name);
    std::error_code ec;
    if (fs::exists(drivesDir() / name, ec)) return fail(err, "pendrive exists: " + name);
    fs::create_directories(drivesDir() / name / "playlists", ec);
    if (ec) return fail(err, "cannot create pendrive " + name + ": " + ec.message());
    return true;
}

bool Library::renameDrive(const std::string& from, const std::string& to, std::string* err) const
{
    if (!hasDrive(from)) return fail(err, "no such pendrive: " + from);
    if (!isSafeName(to)) return fail(err, "bad pendrive name: " + to);
    if (from == to) return true;
    std::error_code ec;
    if (fs::exists(drivesDir() / to, ec)) return fail(err, "pendrive exists: " + to);
    fs::rename(drivesDir() / from, drivesDir() / to, ec);          // same parent, so the relative links inside keep working
    if (ec) return fail(err, "cannot rename pendrive " + from + ": " + ec.message());
    return true;
}

bool Library::deleteDrive(const std::string& name, std::string* err) const
{
    if (!hasDrive(name)) return fail(err, "no such pendrive: " + name);
    std::error_code ec;
    fs::remove_all(drivesDir() / name, ec);                         // links only, never the tracks
    if (ec) return fail(err, "cannot delete pendrive " + name + ": " + ec.message());
    return true;
}

Library Library::driveLibrary(const std::string& drive) const
{
    Library l;
    l.root_ = drivesDir() / drive;
    return l;
}

static std::string withoutPrefix(const std::string& n)
{
    return prefixOf(n) >= 0 ? n.substr(n.find('_') + 1) : n;
}

std::vector<Library::DriveEntry> Library::driveEntries(const std::string& drive) const
{
    std::vector<DriveEntry> out;
    if (!hasDrive(drive)) return out;
    Library d = driveLibrary(drive);
    std::vector<std::string> names = d.listPlaylists();
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        long long pa = prefixOf(a), pb = prefixOf(b);
        if ((pa < 0) != (pb < 0)) return pb < 0;
        if (pa != pb) return pa < pb;
        return naturalLess(a, b);
    });
    for (const std::string& n : names) {
        DriveEntry e;
        e.dir = n;
        e.name = withoutPrefix(n);
        std::string text;
        if (readFile(d.playlistsDir() / n / "entry.txt", &text)) {
            KvDoc kv = KvDoc::parse(text);
            e.kind = kv.get("kind");
            e.source = kv.get("source");
        }
        e.tracks = int(d.playlistTracks(n).size());
        out.push_back(e);
    }
    return out;
}

// Gives the entry directories consecutive numbers in the given order (two passes, so a name is never overwritten).
static bool renumberEntries(const Library& d, const std::vector<std::string>& order, std::string* err)
{
    std::error_code ec;
    const size_t width = std::max<size_t>(3, std::to_string(order.empty() ? 0 : order.size() - 1).size());
    std::vector<std::string> finals;
    for (size_t i = 0; i < order.size(); ++i) {
        char num[32];
        std::snprintf(num, sizeof num, "%0*zu", int(width), i);
        finals.push_back(std::string(num) + "_" + withoutPrefix(order[i]));
    }
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == finals[i]) continue;
        fs::rename(d.playlistsDir() / order[i], d.playlistsDir() / (".r" + std::to_string(i)), ec);
        if (ec) return fail(err, "cannot rename " + order[i] + ": " + ec.message());
    }
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == finals[i]) continue;
        fs::rename(d.playlistsDir() / (".r" + std::to_string(i)), d.playlistsDir() / finals[i], ec);
        if (ec) return fail(err, "cannot rename to " + finals[i] + ": " + ec.message());
    }
    return true;
}

bool Library::addDriveEntry(const std::string& drive, const std::string& nameIn, const std::string& kind, const std::string& source,
                            const std::vector<fs::path>& tracks, std::string* err) const
{
    if (!hasDrive(drive)) return fail(err, "no such pendrive: " + drive);
    const std::string base = sanitizeName(nameIn);
    Library d = driveLibrary(drive);
    std::vector<DriveEntry> have = driveEntries(drive);
    std::string name = base;
    for (int i = 2;; ++i) {
        bool taken = false;
        for (const DriveEntry& e : have) taken = taken || e.name == name;
        if (!taken) break;
        name = base + " " + std::to_string(i);
    }
    char num[32];
    std::snprintf(num, sizeof num, "%03zu", have.size());
    const std::string dirName = std::string(num) + "_" + name;
    if (!d.createPlaylist(dirName, err)) return false;
    KvDoc kv;
    kv.addComment("trakcrate pendrive entry");
    kv.set("kind", kind);
    kv.set("source", source);
    if (!atomicWrite(d.playlistsDir() / dirName / "entry.txt", kv.serialise(), err) || !d.addTracksToPlaylist(dirName, tracks, -1, err)) {
        std::error_code ec;
        fs::remove_all(d.playlistsDir() / dirName, ec);
        return false;
    }
    return true;
}

static std::set<std::string> canonicalSet(const std::vector<fs::path>& dirs);

// Adding a folder or playlist that already has an entry on the pendrive tops that entry up with the tracks it lost (appended at the end)
// instead of making a second entry; with nothing missing it does nothing. Returns true when the entry was handled.
static bool topUpDriveEntry(const Library& lib, const std::string& drive, const std::string& kind, const std::string& source,
                            const std::vector<fs::path>& tracks, bool* handled, std::string* err)
{
    *handled = false;
    for (const Library::DriveEntry& e : lib.driveEntries(drive)) {
        if (e.kind != kind || e.source != source) continue;
        Library d = lib.driveLibrary(drive);
        const std::set<std::string> have = canonicalSet(d.playlistTracks(e.dir));
        std::error_code ec;
        std::vector<fs::path> missing;
        for (const fs::path& t : tracks)
            if (!have.count(fs::weakly_canonical(t, ec).string())) missing.push_back(t);
        *handled = true;
        return missing.empty() || d.addTracksToPlaylist(e.dir, missing, -1, err);
    }
    return true;
}

bool Library::addFolderToDrive(const std::string& drive, const std::string& folder, std::string* err) const
{
    if (!isSafeName(folder)) return fail(err, "bad folder name: " + folder);
    std::vector<fs::path> tracks;
    for (const std::string& t : listTracks(folder)) tracks.push_back(trackDir(folder, t));
    if (tracks.empty()) return fail(err, "the folder is empty: " + folder);
    bool handled = false;
    if (hasDrive(drive) && !topUpDriveEntry(*this, drive, "folder", folder, tracks, &handled, err)) return false;
    return handled || addDriveEntry(drive, folder, "folder", folder, tracks, err);
}

bool Library::addPlaylistToDrive(const std::string& drive, const std::string& playlist, std::string* err) const
{
    if (!hasPlaylist(playlist)) return fail(err, "no such playlist: " + playlist);
    std::vector<fs::path> tracks = playlistTracks(playlist);
    if (tracks.empty()) return fail(err, "the playlist is empty: " + playlist);
    bool handled = false;
    if (hasDrive(drive) && !topUpDriveEntry(*this, drive, "playlist", playlist, tracks, &handled, err)) return false;
    return handled || addDriveEntry(drive, playlist, "playlist", playlist, tracks, err);
}

bool Library::removeDriveEntry(const std::string& drive, int index, std::string* err) const
{
    std::vector<DriveEntry> es = driveEntries(drive);
    if (index < 0 || size_t(index) >= es.size()) return fail(err, "no such entry");
    Library d = driveLibrary(drive);
    if (!d.deletePlaylist(es[size_t(index)].dir, err)) return false;
    es.erase(es.begin() + index);
    std::vector<std::string> order;
    for (const DriveEntry& e : es) order.push_back(e.dir);
    return renumberEntries(d, order, err);
}

bool Library::moveDriveEntry(const std::string& drive, int from, int to, std::string* err) const
{
    std::vector<DriveEntry> es = driveEntries(drive);
    if (from < 0 || to < 0 || size_t(from) >= es.size() || size_t(to) >= es.size()) return fail(err, "no such entry");
    std::vector<std::string> order;
    for (const DriveEntry& e : es) order.push_back(e.dir);
    std::string moved = order[size_t(from)];
    order.erase(order.begin() + from);
    order.insert(order.begin() + to, moved);
    return renumberEntries(driveLibrary(drive), order, err);
}

bool Library::moveDriveTrack(const std::string& drive, int entry, int from, int to, std::string* err) const
{
    std::vector<DriveEntry> es = driveEntries(drive);
    if (entry < 0 || size_t(entry) >= es.size()) return fail(err, "no such entry");
    return driveLibrary(drive).moveInPlaylist(es[size_t(entry)].dir, from, to, err);
}

bool Library::removeDriveTrack(const std::string& drive, int entry, int index, std::string* err) const
{
    std::vector<DriveEntry> es = driveEntries(drive);
    if (entry < 0 || size_t(entry) >= es.size()) return fail(err, "no such entry");
    return driveLibrary(drive).removeFromPlaylist(es[size_t(entry)].dir, index, err);
}

// ---- what removing tracks breaks --------------------------------------------------------------------------------------------------------

static std::set<std::string> canonicalSet(const std::vector<fs::path>& dirs)
{
    std::set<std::string> s;
    std::error_code ec;
    for (const fs::path& d : dirs) s.insert(fs::weakly_canonical(d, ec).string());
    return s;
}

// Calls fn(listLibrary, playlistName, label) for every playlist of the library and every entry of every pendrive.
template <class Fn>
static void eachList(const Library& lib, Fn fn)
{
    for (const std::string& p : lib.listPlaylists()) fn(lib, p, std::string(), p);
    for (const std::string& dr : lib.listDrives()) {
        Library d = lib.driveLibrary(dr);
        for (const Library::DriveEntry& e : lib.driveEntries(dr)) fn(d, e.dir, dr, dr + " / " + e.name);
    }
}

Library::Impact Library::impactOfRemoving(const std::vector<fs::path>& trackDirs) const
{
    Impact im;
    const std::set<std::string> gone = canonicalSet(trackDirs);
    std::error_code ec;
    eachList(*this, [&](const Library& l, const std::string& name, const std::string& drive, const std::string& label) {
        int n = 0;
        for (const fs::path& t : l.playlistTracks(name))
            if (gone.count(fs::weakly_canonical(t, ec).string())) ++n;
        if (!n) return;
        im.links += n;
        (drive.empty() ? im.playlists : im.driveEntries).push_back(label);
    });
    return im;
}

bool Library::forgetTracks(const std::vector<fs::path>& trackDirs, std::string* err) const
{
    const std::set<std::string> gone = canonicalSet(trackDirs);
    std::error_code ec;
    bool ok = true;
    eachList(*this, [&](const Library& l, const std::string& name, const std::string&, const std::string&) {
        const std::vector<fs::path> tracks = l.playlistTracks(name);
        for (int i = int(tracks.size()) - 1; i >= 0; --i)          // from the end, so the indexes stay valid
            if (gone.count(fs::weakly_canonical(tracks[size_t(i)], ec).string())) ok = l.removeFromPlaylist(name, i, err) && ok;
    });
    return ok;
}

uint64_t Library::estimateTrackBytes(const fs::path& trackDir)
{
    const fs::path audio = audioFileOf(trackDir);
    if (audio.empty()) return 0;
    std::error_code ec;
    const auto n = fs::file_size(audio, ec);
    if (ec) return 0;
    return uint64_t(n) + 150000;                                    // ANLZ .DAT/.EXT, two artwork sizes, the database rows
}

uint64_t Library::estimateBytes(const std::vector<fs::path>& trackDirs)
{
    std::set<std::string> seen;
    uint64_t n = 0;
    for (const fs::path& d : trackDirs) {
        std::error_code ec;
        fs::path key = fs::weakly_canonical(d, ec);
        if (ec) key = d;
        if (seen.insert(key.string()).second) n += estimateTrackBytes(d);
    }
    return n > 0 ? n + 300000 : 0;                                  // export.pdb, settings, directory pages
}

std::string formatFolderSize(uint64_t bytes)
{
    // round half up on the hundredths in integers, so 23.335 is not at the mercy of binary floating point
    const uint64_t hundredths = (bytes / 10000000u) + ((bytes % 10000000u) >= 5000000u ? 1 : 0);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%llu.%02lluGb", (unsigned long long)(hundredths / 100), (unsigned long long)(hundredths % 100));
    return buf;
}

uint64_t pendriveGigabytes(uint64_t bytes)
{
    return (bytes + 999999999u) / 1000000000u;
}

std::string formatPendriveSize(uint64_t bytes)
{
    return std::to_string((unsigned long long)pendriveGigabytes(bytes)) + "Gb";
}

} // namespace trakcrate
