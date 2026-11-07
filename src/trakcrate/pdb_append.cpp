#include "pdb_append.h"

#include "pdb_rows.h"

#include <cdj/pdb.h>

#include <algorithm>
#include <cctype>
#include <tuple>

namespace trakcrate {

namespace {

constexpr size_t kMaxBytes = size_t(256) << 20;
constexpr size_t kMaxProblems = 40;
constexpr uint32_t kTableCount = 20;

uint32_t rd32(const std::string& s, size_t at) { return uint32_t(uint8_t(s[at])) | uint32_t(uint8_t(s[at + 1])) << 8 | uint32_t(uint8_t(s[at + 2])) << 16 | uint32_t(uint8_t(s[at + 3])) << 24; }
uint16_t rd16(const std::string& s, size_t at) { return uint16_t(uint8_t(s[at]) | uint8_t(s[at + 1]) << 8); }

std::string lowerAscii(std::string s)
{
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Checker {
    std::vector<std::string>* problems;
    bool failed = false;

    void problem(const std::string& what)
    {
        failed = true;
        if (problems && problems->size() < kMaxProblems) problems->push_back(what);
    }
};

std::string rowWhere(uint32_t table, uint32_t page, size_t slot)
{
    return "table " + std::to_string(table) + ", page " + std::to_string(page) + ", row " + std::to_string(slot) + ": ";
}

// The data pages of a table in chain order. The chain starts at an index page, stays inside the file, has no loop and ends at the table's empty
// candidate page (a blank page, or a page number past the end of the file); the table entry's last page is the last data page.
void walkTable(const PdbFile& f, const PdbTable& t, Checker& c, std::vector<uint32_t>* dataPages)
{
    const std::string name = "table " + std::to_string(t.type) + ": ";
    const size_t n = f.pages.size();
    if (t.firstPage == 0 || t.firstPage >= n) { c.problem(name + "the first page " + std::to_string(t.firstPage) + " is outside the file"); return; }
    if (f.pages[t.firstPage].structured || !(f.pages[t.firstPage].flags & 0x40)) { c.problem(name + "the first page " + std::to_string(t.firstPage) + " is not an index page"); return; }
    if (t.emptyCandidate == 0) { c.problem(name + "no empty candidate page"); return; }
    if (t.emptyCandidate < n) {
        const PdbPage& e = f.pages[t.emptyCandidate];
        if (e.structured || e.flags != 0 || e.index != 0) c.problem(name + "the empty candidate page " + std::to_string(t.emptyCandidate) + " is not blank");
    }
    std::set<uint32_t> seen{t.firstPage};
    uint32_t cur = t.firstPage;
    for (;;) {
        const uint32_t next = f.pages[cur].next;
        if (next == t.emptyCandidate) break;
        if (next == 0 || next >= n) { c.problem(name + "the chain ends at page " + std::to_string(cur) + " before the empty candidate page " + std::to_string(t.emptyCandidate)); return; }
        if (!seen.insert(next).second) { c.problem(name + "the chain loops at page " + std::to_string(next)); return; }
        const PdbPage& p = f.pages[next];
        if (!p.structured) { c.problem(name + "page " + std::to_string(next) + " of the chain is not a data page the model understands"); return; }
        if (p.type != t.type) c.problem(name + "page " + std::to_string(next) + " has the type of table " + std::to_string(p.type));
        if (p.index != next) c.problem(name + "page " + std::to_string(next) + " says it is page " + std::to_string(p.index));
        dataPages->push_back(next);
        cur = next;
    }
    const uint32_t lastData = dataPages->empty() ? t.firstPage : dataPages->back();
    if (t.lastPage != lastData) c.problem(name + "the table says its last page is " + std::to_string(t.lastPage) + " but the chain ends at " + std::to_string(lastData));
}

// Collects the ids of a table's rows and returns false for a duplicate among the present rows.
struct IdSet {
    std::set<uint32_t> present;
    bool add(uint32_t id, bool isPresent, uint32_t* maxId)
    {
        *maxId = std::max(*maxId, id);
        return !isPresent || present.insert(id).second;
    }
};

} // namespace

bool loadDriveDb(const std::string& bytes, DriveDb* out, std::vector<std::string>* problems)
{
    Checker c{problems};
    *out = DriveDb();
    if (bytes.size() > kMaxBytes) { c.problem("export.pdb is " + std::to_string(bytes.size()) + " bytes, more than the " + std::to_string(kMaxBytes) + " byte limit"); return false; }
    std::string err;
    if (!parsePdb(bytes, &out->file, &err)) { c.problem("not an export.pdb this program can read: " + err); return false; }
    const PdbFile& f = out->file;
    if (f.tables.size() != kTableCount) { c.problem("the database has " + std::to_string(f.tables.size()) + " tables, not " + std::to_string(kTableCount)); return false; }

    // chains
    std::map<uint32_t, std::vector<uint32_t>> data;                       // table type -> data pages in chain order
    uint32_t maxCandidate = 0;
    std::set<uint32_t> types;
    for (const PdbTable& t : f.tables) {
        if (!types.insert(t.type).second) c.problem("table type " + std::to_string(t.type) + " appears twice in the header");
        walkTable(f, t, c, &data[t.type]);
        maxCandidate = std::max(maxCandidate, t.emptyCandidate);
    }
    if (f.nextUnusedPage < f.pages.size()) c.problem("next unused page " + std::to_string(f.nextUnusedPage) + " is inside the file (" + std::to_string(f.pages.size()) + " pages)");
    if (f.nextUnusedPage <= maxCandidate) c.problem("next unused page " + std::to_string(f.nextUnusedPage) + " is not past the empty candidate pages (" + std::to_string(maxCandidate) + ")");
    if (c.failed) return false;                                           // the rows cannot be trusted when the chains are not

    // page counters: a page written after the header's sequence would make our new numbers go backwards
    for (const PdbTable& t : f.tables) {
        for (uint32_t pg : data[t.type])
            if (f.pages[pg].u10 > f.sequence) out->warnings.push_back("page " + std::to_string(pg) + " has write counter " + std::to_string(f.pages[pg].u10) + ", past the header's sequence " + std::to_string(f.sequence));
    }

    // rows
    IdSet trackIds, artistIds, albumIds, genreIds, labelIds, artworkIds, playlistIds;
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> entries;       // (entry index, track id, playlist id) of present rows
    std::set<uint32_t> presentTrack;
    auto idOf = [&](uint32_t type) -> uint32_t& { return out->maxId[type]; };
    for (const PdbTable& t : f.tables) {
        for (uint32_t pg : data[t.type]) {
            const PdbPage& page = f.pages[pg];
            for (size_t slot = 0; slot < page.rows.size(); ++slot) {
                const PdbRow& r = page.rows[slot];
                const std::string& b = r.bytes;
                const std::string at = rowWhere(t.type, pg, slot);
                auto str = [&](size_t off, std::string* s) { return decodeDeviceSqlString(b, off, s); };
                switch (t.type) {
                case 0: {                                                    // tracks
                    if (b.size() < size_t(kTrackFixed + kTrackSlots * 2)) { c.problem(at + "a track row of " + std::to_string(b.size()) + " bytes is too short"); break; }
                    if (rd16(b, 0) != 0x24) { c.problem(at + "unknown track row subtype " + std::to_string(rd16(b, 0))); break; }
                    DriveTrack tr;
                    tr.id = rd32(b, 72);
                    tr.fileSize = rd32(b, 16);
                    auto slotStr = [&](int i, std::string* s) { return str(rd16(b, size_t(kTrackFixed + 2 * i)), s); };
                    std::string fileName;
                    if (!slotStr(14, &tr.analyzePath) || !slotStr(17, &tr.title) || !slotStr(19, &fileName) || !slotStr(20, &tr.filePath)) { c.problem(at + "the strings of a track row cannot be read"); break; }
                    if (!trackIds.add(tr.id, r.present, &idOf(0))) c.problem(at + "track id " + std::to_string(tr.id) + " appears twice");
                    out->analyzePaths.insert(tr.analyzePath);
                    if (r.present) {
                        presentTrack.insert(tr.id);
                        std::set<std::string> none;
                        const std::string expected = analyzePathFor(tr.filePath, none);
                        if (expected.substr(0, expected.rfind('/')) != tr.analyzePath.substr(0, tr.analyzePath.rfind('/')))
                            out->warnings.push_back("track " + std::to_string(tr.id) + ": the analysis folder " + tr.analyzePath + " is not the one rekordbox's rule gives for " + tr.filePath);
                        out->trackByPath.emplace(lowerAscii(tr.filePath), out->tracks.size());
                        out->tracks.push_back(tr);
                    }
                    break;
                }
                case 2: {                                                    // artists
                    std::string name;
                    if (b.size() < 11 || rd16(b, 0) != 0x60 || !str(uint8_t(b[9]), &name)) { c.problem(at + "an artist row cannot be read"); break; }
                    const uint32_t id = rd32(b, 4);
                    if (!artistIds.add(id, r.present, &idOf(2))) c.problem(at + "artist id " + std::to_string(id) + " appears twice");
                    if (r.present) { auto it = out->artist.find(name); if (it == out->artist.end() || id < it->second) out->artist[name] = id; }
                    break;
                }
                case 3: {                                                    // albums
                    std::string name;
                    if (b.size() < 23 || rd16(b, 0) != 0x80 || !str(uint8_t(b[21]), &name)) { c.problem(at + "an album row cannot be read"); break; }
                    const uint32_t id = rd32(b, 12), artist = rd32(b, 8);
                    if (!albumIds.add(id, r.present, &idOf(3))) c.problem(at + "album id " + std::to_string(id) + " appears twice");
                    if (r.present) { auto key = std::make_pair(artist, name); auto it = out->album.find(key); if (it == out->album.end() || id < it->second) out->album[key] = id; }
                    break;
                }
                case 1: case 4: case 13: {                                   // genres, labels, artwork: id and a string
                    std::string name;
                    if (b.size() < 5 || !str(4, &name)) { c.problem(at + "a row cannot be read"); break; }
                    const uint32_t id = rd32(b, 0);
                    IdSet& ids = t.type == 1 ? genreIds : t.type == 4 ? labelIds : artworkIds;
                    if (!ids.add(id, r.present, &idOf(t.type))) c.problem(at + "id " + std::to_string(id) + " appears twice");
                    if (r.present) {
                        std::map<std::string, uint32_t>& m = t.type == 1 ? out->genre : t.type == 4 ? out->label : out->artwork;
                        auto it = m.find(name);
                        if (it == m.end() || id < it->second) m[name] = id;
                        if (t.type == 13) ++out->artworkCount;
                    }
                    break;
                }
                case 7: {                                                    // playlist tree
                    std::string name;
                    if (b.size() < 21 || !str(20, &name)) { c.problem(at + "a playlist row cannot be read"); break; }
                    const uint32_t parent = rd32(b, 0), sort = rd32(b, 8), id = rd32(b, 12), folder = rd32(b, 16);
                    if (!playlistIds.add(id, r.present, &idOf(7))) c.problem(at + "playlist id " + std::to_string(id) + " appears twice");
                    if (r.present) {
                        out->playlistIds.insert(id);
                        out->playlistByName.emplace(lowerAscii(name), id);
                        if (parent == 0) out->maxRootSortOrder = std::max(out->maxRootSortOrder, sort);
                        if (folder != 0) out->warnings.push_back("playlist \"" + name + "\" is a folder: folders are left as they are");
                    }
                    break;
                }
                case 8: {                                                    // playlist entries
                    if (b.size() < 12) { c.problem(at + "a playlist entry row is too short"); break; }
                    if (r.present) entries.emplace_back(rd32(b, 0), rd32(b, 4), rd32(b, 8));
                    idOf(8) = std::max(idOf(8), rd32(b, 0));
                    break;
                }
                default: break;                                              // keys, colors, history ...: only their chains matter
                }
            }
        }
    }
    for (const auto& e : entries) {
        if (!presentTrack.count(std::get<1>(e))) c.problem("a playlist entry refers to track " + std::to_string(std::get<1>(e)) + ", which is not in the track table");
        if (!out->playlistIds.count(std::get<2>(e))) c.problem("a playlist entry refers to playlist " + std::to_string(std::get<2>(e)) + ", which is not in the playlist table");
    }
    return !c.failed;
}

// ---- adding rows ------------------------------------------------------------------------------------------------------------------------

namespace {

void wr32(std::string& s, size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) s[at + size_t(i)] = char((v >> (8 * i)) & 0xFF); }
void wr16(std::string& s, size_t at, uint16_t v) { s[at] = char(v & 0xFF); s[at + 1] = char(v >> 8); }

// Bytes the row index takes at the end of a page with this many slots (full groups 36 bytes, the last one its used offsets plus 4).
size_t indexSpan(size_t slots)
{
    if (slots == 0) return 0;
    const size_t groups = (slots - 1) / kPdbRowsPerGroup + 1;
    return (groups - 1) * kPdbGroupSize + 4 + 2 * (slots - (groups - 1) * kPdbRowsPerGroup);
}

bool hasIndexShift(uint32_t type) { return type == 0 || type == 2 || type == 3; }      // tracks, artists, albums: bytes 2..3 of the row are slot * 0x20

struct Editor {
    PdbFile& f;
    uint32_t seq;                                   // the last write counter handed out
    std::set<uint32_t> touched;
    int rowsAdded = 0, pagesAdded = 0;

    explicit Editor(PdbFile& file) : f(file), seq(file.sequence)
    {
        for (const PdbPage& p : file.pages) seq = std::max(seq, p.u10);
    }

    void touch(PdbPage& p, uint32_t no)
    {
        if (touched.insert(no).second) p.u10 = ++seq;
    }

    PdbTable* table(uint32_t type)
    {
        for (PdbTable& t : f.tables) if (t.type == type) return &t;
        return nullptr;
    }

    bool fits(const PdbPage& p, size_t rowBytes) const
    {
        const size_t slots = p.rows.size() + 1;
        const size_t heap = size_t(p.heapBytes()) + rowBytes;
        return slots <= 0x1FFF && heap <= 0xFFFF && kPdbPageHeader + heap + indexSpan(slots) <= kPdbPageSize;
    }

    // Puts the new row into slot `slot` of a structured page and marks it as the row written last (the page's row flags mark only that one).
    void place(PdbPage& p, uint32_t no, std::string row, uint32_t type, bool newPage)
    {
        const size_t slot = p.rows.size();
        const bool fresh = p.u20 == 1 && p.numLarge + 1u == slot;                // the page follows the fresh pattern: keep following it
        if (hasIndexShift(type)) wr16(row, 2, uint16_t(slot * 0x20));
        PdbRow r;
        r.bytes = std::move(row);
        r.present = true;
        p.rows.push_back(std::move(r));
        const size_t group = slot / kPdbRowsPerGroup;
        if (p.groupFlags.size() <= group) p.groupFlags.resize(group + 1, 0);
        p.groupFlags[group] = uint16_t(p.groupFlags[group] | (1u << (slot % kPdbRowsPerGroup)));       // rekordbox: the flag word covers every slot of the page
        if (fresh) p.numLarge = uint16_t(slot);
        if (newPage) touch(p, no);                                              // a page that only gains a row keeps its counter (as rekordbox leaves it)
        else touched.insert(no);
        ++rowsAdded;
    }

    bool addRow(uint32_t type, std::string row, std::string* err)
    {
        PdbTable* t = table(type);
        if (!t) { *err = "the database has no table " + std::to_string(type); return false; }
        if (kPdbPageHeader + row.size() + 8 > kPdbPageSize) { *err = "a row of table " + std::to_string(type) + " is too large for a page"; return false; }
        if (t->lastPage != t->firstPage) {
            PdbPage& last = f.pages[t->lastPage];
            if (fits(last, row.size())) { place(last, t->lastPage, std::move(row), type, false); return true; }
        }
        // a new page: it takes the table's empty candidate number, and the new empty candidate is the next unused page
        const uint32_t no = t->emptyCandidate, newCandidate = f.nextUnusedPage;
        while (f.pages.size() <= no) {
            PdbPage blank;
            blank.raw.assign(kPdbPageSize, '\0');
            f.pages.push_back(blank);
        }
        PdbPage np;
        np.index = no;
        np.type = type;
        np.next = newCandidate;
        np.flags = 0x24;
        np.u20 = 1;
        np.numLarge = 0;
        np.structured = true;
        np.groupFlags.assign(1, 0);
        f.pages[no] = np;
        place(f.pages[no], no, std::move(row), type, true);
        PdbPage& prev = f.pages[t->lastPage];
        if (!prev.structured) {                                                 // the table was empty: its index page names the first data page
            if (prev.raw.size() == kPdbPageSize && rd32(prev.raw, 0x2c) == 0x03ffffffu) wr32(prev.raw, 0x2c, no);
        }
        t->lastPage = no;
        t->emptyCandidate = newCandidate;
        f.nextUnusedPage = newCandidate + 1;
        ++pagesAdded;
        return true;
    }
};

std::string lowerPath(std::string s) { return lowerAscii(std::move(s)); }

} // namespace

bool appendPlaylist(DriveDb& db, const ExportPlan& plan, const AppendOptions& o, AppendResult* out, std::string* err)
{
    auto fail = [&](const std::string& m) { *err = m; return false; };
    *out = AppendResult();
    if (plan.playlists.empty()) return fail("no playlist to add");
    {
        std::set<std::string> names;
        for (const PlanPlaylist& pl : plan.playlists) {
            if (pl.tracks.empty()) return fail("the playlist \"" + pl.name + "\" has no tracks");
            const std::string key = lowerAscii(pl.name);
            if (db.playlistByName.count(key)) return fail("the pendrive already has a playlist called \"" + pl.name + "\"");
            if (!names.insert(key).second) return fail("the playlist \"" + pl.name + "\" is given twice");
            for (size_t t : pl.tracks) if (t >= plan.tracks.size()) return fail("playlist \"" + pl.name + "\" refers to a track that does not exist");
        }
    }
    PdbFile tplFile;
    TrackTemplate tpl;
    if (!parsePdb(o.templatePdb, &tplFile, err) || !loadTrackTemplate(tplFile, &tpl)) return fail("the template database has no usable track row");

    // ids (nothing is changed until every row is laid out)
    std::map<uint32_t, uint32_t> next;                                          // table type -> last id handed out
    for (uint32_t type : {0u, 1u, 2u, 3u, 4u, 7u, 13u}) next[type] = db.maxId.count(type) ? db.maxId.at(type) : 0;
    std::vector<std::pair<uint32_t, std::string>> rows;                         // (table, row) in the order they are added
    std::map<std::string, uint32_t> newArtist, newGenre, newLabel, newArtwork;
    std::map<std::pair<uint32_t, std::string>, uint32_t> newAlbum;
    auto idFor = [&](const std::map<std::string, uint32_t>& have, std::map<std::string, uint32_t>& fresh, uint32_t type, const std::string& name,
                     std::vector<std::pair<uint32_t, std::string>>* mk) -> uint32_t {
        if (name.empty()) return 0;
        auto it = have.find(name);
        if (it != have.end()) return it->second;
        auto jt = fresh.find(name);
        if (jt != fresh.end()) return jt->second;
        const uint32_t id = ++next[type];
        fresh[name] = id;
        mk->emplace_back(type, name);
        return id;
    };
    std::vector<std::pair<uint32_t, std::string>> madeNames;                   // genres, labels, artists, artwork to create: (type, name)
    std::vector<std::pair<uint32_t, std::pair<uint32_t, std::string>>> madeAlbums;   // (id, (artist id, name))

    out->trackIds.assign(plan.tracks.size(), 0);
    out->trackIsNew.assign(plan.tracks.size(), false);
    out->analyzePaths.assign(plan.tracks.size(), std::string());
    std::set<std::string> usedAnalyze = db.analyzePaths;
    std::set<size_t> wanted;
    for (const PlanPlaylist& pl : plan.playlists) wanted.insert(pl.tracks.begin(), pl.tracks.end());
    std::vector<std::pair<uint32_t, std::string>> trackRows;                    // (id, row)
    for (size_t i : wanted) {
        const PlanTrack& t = plan.tracks[i];
        auto ex = db.trackByPath.find(lowerPath(t.filePath));
        if (ex != db.trackByPath.end()) {
            out->trackIds[i] = db.tracks[ex->second].id;
            out->analyzePaths[i] = db.tracks[ex->second].analyzePath;
            continue;
        }
        const uint32_t genre = idFor(db.genre, newGenre, 1, t.genre, &madeNames);
        const uint32_t label = idFor(db.label, newLabel, 4, t.label, &madeNames);
        const uint32_t artist = idFor(db.artist, newArtist, 2, t.artist, &madeNames);
        uint32_t album = 0;
        if (!t.album.empty()) {
            const auto key = std::make_pair(artist, t.album);
            auto it = db.album.find(key);
            auto jt = newAlbum.find(key);
            if (it != db.album.end()) album = it->second;
            else if (jt != newAlbum.end()) album = jt->second;
            else { album = ++next[3]; newAlbum[key] = album; madeAlbums.push_back({album, key}); }
        }
        uint32_t art = 0;
        if (!t.artworkKey.empty() && !t.artworkPath.empty()) {
            auto it = db.artwork.find(t.artworkPath);
            if (it != db.artwork.end()) art = it->second;
            else art = idFor(db.artwork, newArtwork, 13, t.artworkPath, &madeNames);
        }
        std::string analyze = t.analyzePath;
        if (analyze.empty()) analyze = analyzePathFor(t.filePath, usedAnalyze);
        else usedAnalyze.insert(analyze);
        const uint32_t id = ++next[0];
        out->trackIds[i] = id;
        out->trackIsNew[i] = true;
        out->analyzePaths[i] = analyze;
        trackRows.emplace_back(id, trackRow(tpl, t, id, genre, album, artist, label, art, analyze));
    }
    for (const auto& n : madeNames) {
        const uint32_t type = n.first;
        const uint32_t id = type == 1 ? newGenre.at(n.second) : type == 4 ? newLabel.at(n.second) : type == 2 ? newArtist.at(n.second) : newArtwork.at(n.second);
        rows.emplace_back(type, type == 2 ? artistRow(id, n.second) : nameRow(id, n.second));
        if (type == 13) out->artworkPaths.push_back(n.second);
    }
    for (const auto& a : madeAlbums) rows.emplace_back(3, albumRow(a.first, a.second.first, a.second.second));
    for (const auto& tr : trackRows) rows.emplace_back(0, tr.second);
    uint32_t sort = db.playlistIds.empty() ? 0 : db.maxRootSortOrder + 1;
    for (const PlanPlaylist& pl : plan.playlists) {
        const uint32_t pid = ++next[7];
        if (out->playlistId == 0) out->playlistId = pid;
        rows.emplace_back(7, playlistRow(pid, sort++, pl.name));
        for (size_t k = 0; k < pl.tracks.size(); ++k) rows.emplace_back(8, entryRow(uint32_t(k + 1), out->trackIds[pl.tracks[k]], pid));
    }

    // lay the rows out in a copy of the file
    PdbFile work = db.file;
    Editor ed(work);
    // rows of one table keep their order; the tables are filled in the order of the list above (tracks come before the entries that name them)
    for (auto& r : rows) if (!ed.addRow(r.first, std::move(r.second), err)) return false;
    work.sequence = ed.seq + 1;                                                 // rekordbox leaves the header sequence a little past the last page counter
    std::string bytes;
    if (!serialisePdb(work, &bytes, err)) return false;
    DriveDb fresh;
    std::vector<std::string> problems;
    if (!loadDriveDb(bytes, &fresh, &problems)) return fail("the new database is not valid: " + (problems.empty() ? std::string("unknown reason") : problems.front()));

    out->bytes = std::move(bytes);
    out->rowsAdded = ed.rowsAdded;
    out->pagesAdded = ed.pagesAdded;
    out->touchedPages.assign(ed.touched.begin(), ed.touched.end());
    db = std::move(fresh);
    return true;
}

std::vector<std::string> verifyAppended(const std::string& before, const std::string& after, const ExportPlan& plan, const AppendResult& result)
{
    std::vector<std::string> bad;
    PdbFile b, a;
    std::string err;
    if (!parsePdb(before, &b, &err)) { bad.push_back("the old database cannot be read: " + err); return bad; }
    if (!parsePdb(after, &a, &err)) { bad.push_back("the new database cannot be read: " + err); return bad; }
    if (a.pages.size() < b.pages.size()) bad.push_back("the new database is shorter than the old one");
    if (a.tables.size() != b.tables.size()) { bad.push_back("the number of tables changed"); return bad; }
    if (a.nextUnusedPage < b.nextUnusedPage) bad.push_back("next unused page went backwards");
    if (a.sequence < b.sequence) bad.push_back("the sequence went backwards");
    // nothing that was there changed
    for (size_t n = 1; n < b.pages.size() && n < a.pages.size(); ++n) {
        const PdbPage& x = b.pages[n];
        const PdbPage& y = a.pages[n];
        if (x.structured != y.structured && !(!x.structured && x.flags == 0)) { bad.push_back("page " + std::to_string(n) + " changed kind"); continue; }
        if (x.structured) {
            if (y.rows.size() < x.rows.size()) { bad.push_back("page " + std::to_string(n) + " lost rows"); continue; }
            for (size_t i = 0; i < x.rows.size(); ++i)
                if (x.rows[i].bytes != y.rows[i].bytes || x.rows[i].present != y.rows[i].present) bad.push_back("page " + std::to_string(n) + " row " + std::to_string(i) + " changed");
            if (y.rows.size() == x.rows.size() && y.next != x.next) bad.push_back("page " + std::to_string(n) + " changed its next page");
            if (y.rows.size() > x.rows.size() && y.flags != x.flags) bad.push_back("page " + std::to_string(n) + " changed its flags");
        } else if (x.flags & 0x40) {                                    // an index page: only the first data page of an empty table may be filled in
            std::string xr = x.raw, yr = y.raw;
            if (xr.size() == kPdbPageSize && yr.size() == kPdbPageSize) {
                if (rd32(xr, 0x2c) == 0x03ffffffu) wr32(xr, 0x2c, rd32(yr, 0x2c));
                if (xr != yr) bad.push_back("index page " + std::to_string(n) + " changed");
            }
        } else if (x.flags == 0 && !y.structured && x.raw != y.raw) {
            bad.push_back("blank page " + std::to_string(n) + " changed");
        }
    }
    // tables: chains, counters
    DriveDb db;
    std::vector<std::string> problems;
    if (!loadDriveDb(after, &db, &problems)) { for (const std::string& p : problems) bad.push_back("the new database fails the checks: " + p); return bad; }
    // the new rows through libcdj's reader
    struct Tracks { std::map<uint32_t, std::pair<std::string, std::string>> byId; };
    Tracks seen;
    pdb_parse(reinterpret_cast<const uint8_t*>(after.data()), uint32_t(after.size()),
              [](const pdb_track_t* t, void* ud) {
                  static_cast<Tracks*>(ud)->byId[t->id] = {t->title ? t->title : "", t->file_path ? t->file_path : ""};
                  return 0;
              }, &seen);
    for (size_t i = 0; i < plan.tracks.size(); ++i) {
        if (result.trackIds.size() <= i || result.trackIds[i] == 0) continue;
        auto it = seen.byId.find(result.trackIds[i]);
        if (it == seen.byId.end()) { bad.push_back("track \"" + plan.tracks[i].title + "\" is not in the new database"); continue; }
        if (lowerAscii(it->second.second) != lowerAscii(plan.tracks[i].filePath)) bad.push_back("track \"" + plan.tracks[i].title + "\" has the path " + it->second.second + ", expected " + plan.tracks[i].filePath);
        if (result.trackIsNew.size() > i && result.trackIsNew[i] && it->second.first != plan.tracks[i].title) bad.push_back("track " + std::to_string(result.trackIds[i]) + " has the title \"" + it->second.first + "\"");
    }
    // playlist entries in order
    std::map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> entries;   // playlist id -> (entry index, track id)
    for (const PdbRow* r : tableRows(a, 8)) if (r->bytes.size() >= 12) entries[rd32(r->bytes, 8)].emplace_back(rd32(r->bytes, 0), rd32(r->bytes, 4));
    for (const PlanPlaylist& pl : plan.playlists) {
        auto it = db.playlistByName.find(lowerAscii(pl.name));
        if (it == db.playlistByName.end()) { bad.push_back("playlist \"" + pl.name + "\" is not in the new database"); continue; }
        std::vector<std::pair<uint32_t, uint32_t>> es = entries[it->second];
        std::sort(es.begin(), es.end());
        if (es.size() != pl.tracks.size()) { bad.push_back("playlist \"" + pl.name + "\" has " + std::to_string(es.size()) + " entries, expected " + std::to_string(pl.tracks.size())); continue; }
        for (size_t k = 0; k < es.size(); ++k)
            if (es[k].first != k + 1 || es[k].second != result.trackIds[pl.tracks[k]]) bad.push_back("playlist \"" + pl.name + "\" entry " + std::to_string(k + 1) + " is wrong");
    }
    // counters agree with the contents, flags follow the deleted rows
    for (size_t n = 1; n < a.pages.size(); ++n) {
        const PdbPage& p = a.pages[n];
        if (!p.structured) continue;
        if (p.flags != (p.presentRows() == int(p.rows.size()) ? 0x24 : 0x34)) bad.push_back("page " + std::to_string(n) + " has flags " + std::to_string(p.flags) + " that do not fit its rows");
    }
    return bad;
}

} // namespace trakcrate
