#include "pdb_build.h"

#include "sha256.h"

#include <cdj/pdb.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace trakcrate {

namespace {

constexpr int kTableCount = 20;
constexpr uint32_t kEmptyEntry = 0x03ffffff;

void put32(std::string& s, size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) s[at + size_t(i)] = char((v >> (8 * i)) & 0xFF); }
void put16(std::string& s, size_t at, uint16_t v) { s[at] = char(v & 0xFF); s[at + 1] = char(v >> 8); }
uint32_t get32(const std::string& s, size_t at) { return uint32_t(uint8_t(s[at])) | uint32_t(uint8_t(s[at + 1])) << 8 | uint32_t(uint8_t(s[at + 2])) << 16 | uint32_t(uint8_t(s[at + 3])) << 24; }
void app32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xFF); }

std::string padTo4(std::string s)
{
    while (s.size() % 4) s += '\0';
    return s;
}

bool isPlainAscii(const std::string& s)
{
    for (unsigned char c : s) if (c < 0x20 || c >= 0x7f) return false;       // controls and non-ASCII need the UTF-16 form
    return true;
}

// UTF-8 to UTF-16LE; invalid bytes become U+FFFD.
std::string utf8ToUtf16le(const std::string& s)
{
    std::string out;
    auto put = [&](uint32_t cp) {
        if (cp >= 0x10000) {
            cp -= 0x10000;
            uint16_t hi = uint16_t(0xD800 + (cp >> 10)), lo = uint16_t(0xDC00 + (cp & 0x3FF));
            out += char(hi & 0xFF); out += char(hi >> 8); out += char(lo & 0xFF); out += char(lo >> 8);
        } else {
            out += char(cp & 0xFF); out += char(cp >> 8);
        }
    };
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0xFFFD;
        size_t n = 1;
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size() && (s[i + 1] & 0xC0) == 0x80) { cp = (c & 0x1F) << 6 | (s[i + 1] & 0x3F); n = 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size() && (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80) { cp = (c & 0x0F) << 12 | (s[i + 1] & 0x3F) << 6 | (s[i + 2] & 0x3F); n = 3; }
        else if ((c & 0xF8) == 0xF0 && i + 3 < s.size() && (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80 && (s[i + 3] & 0xC0) == 0x80) { cp = (c & 0x07) << 18 | (s[i + 1] & 0x3F) << 12 | (s[i + 2] & 0x3F) << 6 | (s[i + 3] & 0x3F); n = 4; }
        if (cp >= 0x110000 || (cp >= 0xD800 && cp < 0xE000)) cp = 0xFFFD;
        put(cp);
        i += n;
    }
    return out;
}

// Longest text we store in one string. A track row holds six of them and must fit in a 4096 byte page; no player shows more anyway.
constexpr size_t kMaxStringBytes = 500;

std::string truncateUtf8(const std::string& s, size_t max)
{
    if (s.size() <= max) return s;
    size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut);
}

} // namespace

std::string encodeDeviceSqlString(const std::string& utf8In)
{
    const std::string utf8 = truncateUtf8(utf8In, kMaxStringBytes);
    std::string out;
    if (isPlainAscii(utf8)) {
        if (utf8.size() <= 126) {
            out += char(((utf8.size() + 1) << 1) | 1);               // short ASCII: kind = (length + 1) << 1 | 1, empty = 0x03
            out += utf8;
        } else {
            out += char(0x40);
            out += char((utf8.size() + 4) & 0xFF);
            out += char((utf8.size() + 4) >> 8);
            out += char(0);
            out += utf8;
        }
        return out;
    }
    const std::string u16 = utf8ToUtf16le(utf8);
    out += char(0x90);
    out += char((u16.size() + 4) & 0xFF);
    out += char((u16.size() + 4) >> 8);
    out += char(0);
    out += u16;
    return out;
}

namespace {

// ---- row encoders ----------------------------------------------------------------------------------------------------------------------

std::string nameRow(uint32_t id, const std::string& name)                // genres, labels, artwork paths
{
    std::string r;
    app32(r, id);
    r += encodeDeviceSqlString(name);
    return padTo4(r);
}

std::string artistRow(uint32_t id, const std::string& name)
{
    std::string r(10, '\0');
    put16(r, 0, 0x60);
    put32(r, 4, id);
    r[8] = 0x03;
    r[9] = 0x0a;                                                         // the name starts right after this header
    r += encodeDeviceSqlString(name);
    return padTo4(r);
}

std::string albumRow(uint32_t id, uint32_t artistId, const std::string& name)
{
    std::string r(22, '\0');
    put16(r, 0, 0x80);
    put32(r, 8, artistId);
    put32(r, 12, id);
    r[20] = 0x03;
    r[21] = 0x16;
    r += encodeDeviceSqlString(name);
    return padTo4(r);
}

std::string playlistRow(uint32_t id, uint32_t sortOrder, const std::string& name)
{
    std::string r;
    app32(r, 0);                                                         // parent: the root
    app32(r, 0);
    app32(r, sortOrder);
    app32(r, id);
    app32(r, 0);                                                         // not a folder
    r += encodeDeviceSqlString(name);
    return padTo4(r);
}

std::string entryRow(uint32_t index, uint32_t trackId, uint32_t playlistId)
{
    std::string r;
    app32(r, index);
    app32(r, trackId);
    app32(r, playlistId);
    return r;
}

// The strings of a track row: 21 slots in the order the template row stores them.
constexpr int kTrackFixed = 94, kTrackSlots = 21;

size_t deviceSqlLength(const std::string& row, size_t at)
{
    if (at >= row.size()) return 0;
    uint8_t kind = uint8_t(row[at]);
    if (kind == 0x40 || kind == 0x90) return size_t(uint8_t(row[at + 1])) | size_t(uint8_t(row[at + 2])) << 8;
    return size_t(kind >> 1);
}

struct TrackTemplate {
    std::string fixed;                         // 94 bytes
    std::string strings[kTrackSlots];          // default encoded strings per slot
    std::string defaultDate;
};

bool loadTrackTemplate(const PdbFile& t, TrackTemplate* out)
{
    std::vector<const PdbRow*> rows = tableRows(t, 0);
    if (rows.empty() || rows[0]->bytes.size() < size_t(kTrackFixed + kTrackSlots * 2)) return false;
    const std::string& r = rows[0]->bytes;
    out->fixed = r.substr(0, kTrackFixed);
    for (int i = 0; i < kTrackSlots; ++i) {
        size_t ofs = size_t(uint8_t(r[kTrackFixed + 2 * i])) | size_t(uint8_t(r[kTrackFixed + 2 * i + 1])) << 8;
        size_t len = deviceSqlLength(r, ofs);
        if (ofs + len > r.size() || len == 0) return false;
        out->strings[i] = r.substr(ofs, len);
    }
    // slot 10 holds the date added: "2026-09-20" in short ASCII form
    out->defaultDate = out->strings[10].size() > 1 ? out->strings[10].substr(1) : std::string("2000-01-01");
    return true;
}

std::string trackRow(const TrackTemplate& tpl, const PlanTrack& t, uint32_t id, uint32_t genreId, uint32_t albumId, uint32_t artistId,
                     uint32_t labelId, uint32_t artworkId, const std::string& analyzePath)
{
    std::string fixed = tpl.fixed;
    put16(fixed, 0, 0x24);
    put32(fixed, 8, t.sampleRate);
    put32(fixed, 16, t.fileSize);
    put32(fixed, 20, 2406 + id);                                         // unknown; in the template it counts up with the track id
    put32(fixed, 28, artworkId);
    put32(fixed, 32, 0);                                                 // key: the template has an empty keys table
    put32(fixed, 40, labelId);
    put32(fixed, 44, 0);
    put32(fixed, 48, t.bitrateKbps);
    put32(fixed, 52, t.trackNumber);
    put32(fixed, 56, t.bpmX100);
    put32(fixed, 60, genreId);
    put32(fixed, 64, albumId);
    put32(fixed, 68, artistId);
    put32(fixed, 72, id);
    put16(fixed, 76, uint16_t(t.discNumber));
    put16(fixed, 78, uint16_t(t.playCount));
    put16(fixed, 80, uint16_t(t.year));
    put16(fixed, 82, uint16_t(t.sampleDepth));
    put16(fixed, 84, uint16_t(t.durationSecs));
    fixed[88] = char(t.colorId);
    fixed[89] = char(t.rating);

    const std::string date = t.dateAdded.empty() ? tpl.defaultDate : t.dateAdded;
    std::string s[kTrackSlots];
    for (int i = 0; i < kTrackSlots; ++i) s[i] = tpl.strings[i];
    s[10] = encodeDeviceSqlString(date);
    s[11] = encodeDeviceSqlString(std::string());
    s[12] = encodeDeviceSqlString(t.mixName);
    s[14] = encodeDeviceSqlString(analyzePath);
    s[15] = encodeDeviceSqlString(date);
    s[16] = encodeDeviceSqlString(t.comment);
    s[17] = encodeDeviceSqlString(t.title);
    s[19] = encodeDeviceSqlString(t.fileName);
    s[20] = encodeDeviceSqlString(t.filePath);

    std::string row = fixed + std::string(size_t(kTrackSlots) * 2, '\0');
    for (int i = 0; i < kTrackSlots; ++i) {
        put16(row, size_t(kTrackFixed + 2 * i), uint16_t(row.size()));
        row += s[i];
    }
    // the template rows end with 38 to 70 zero bytes after the last string (reserve for edits); the length is a multiple of 4
    row.append(40, '\0');
    return padTo4(row);
}

// ---- pages -----------------------------------------------------------------------------------------------------------------------------

// Splits rows over data pages: a row goes on the current page while heap + row index still fit.
std::vector<std::vector<PdbRow>> paginate(std::vector<std::string> rows)
{
    std::vector<std::vector<PdbRow>> pages;
    size_t heap = 0;
    for (std::string& r : rows) {
        const size_t slots = pages.empty() ? 0 : pages.back().size();
        const size_t newSlots = slots + 1;
        const size_t groups = (newSlots - 1) / kPdbRowsPerGroup + 1;
        const size_t span = (groups - 1) * kPdbGroupSize + 4 + 2 * (newSlots - (groups - 1) * kPdbRowsPerGroup);
        if (pages.empty() || kPdbPageHeader + heap + r.size() + span > kPdbPageSize || newSlots > 0x1FFF) {
            pages.emplace_back();
            heap = 0;
        }
        PdbRow row;
        row.bytes = std::move(r);
        heap += row.bytes.size();
        pages.back().push_back(std::move(row));
    }
    return pages;
}

void applyIndexShift(std::vector<PdbRow>& rows)
{
    for (size_t i = 0; i < rows.size(); ++i) put16(rows[i].bytes, 2, uint16_t(i * 0x20));
}

std::vector<uint16_t> freshGroupFlags(const std::vector<PdbRow>& rows)
{
    std::vector<uint16_t> g;
    for (size_t at = 0; at < rows.size(); at += kPdbRowsPerGroup) {
        size_t n = std::min<size_t>(kPdbRowsPerGroup, rows.size() - at);
        g.push_back(uint16_t(n == 16 ? 0xFFFFu : ((1u << n) - 1)));
    }
    return g;
}

struct Ids {
    std::map<std::string, uint32_t> artist, genre, label, artwork;
    std::map<std::pair<uint32_t, std::string>, uint32_t> album;
    std::vector<std::string> artistNames, genreNames, labelNames, artworkPaths;
    std::vector<std::pair<uint32_t, std::string>> albums;       // (artist id, name)
};

uint32_t idFor(std::map<std::string, uint32_t>& m, std::vector<std::string>& names, const std::string& name)
{
    if (name.empty()) return 0;
    auto it = m.find(name);
    if (it != m.end()) return it->second;
    names.push_back(name);
    return m[name] = uint32_t(names.size());
}

} // namespace

bool buildPdb(const ExportPlan& plan, const std::string& templateBytes, BuiltPdb* out, std::string* err)
{
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    PdbFile tpl;
    if (!parsePdb(templateBytes, &tpl, err)) return false;
    if (tpl.tables.size() != size_t(kTableCount)) return fail("the template must have 20 tables");
    TrackTemplate trackTpl;
    if (!loadTrackTemplate(tpl, &trackTpl)) return fail("the template has no usable track row");
    for (const PlanPlaylist& pl : plan.playlists)
        for (size_t t : pl.tracks)
            if (t >= plan.tracks.size()) return fail("playlist \"" + pl.name + "\" refers to a track that does not exist");

    // ids: first seen, stable
    Ids ids;
    std::vector<uint32_t> genreId, labelId, artistId, albumId, artworkId;
    std::vector<std::string> analyze;
    std::set<std::string> usedAnalyze;
    for (const PlanTrack& t : plan.tracks) {
        genreId.push_back(idFor(ids.genre, ids.genreNames, t.genre));
        labelId.push_back(idFor(ids.label, ids.labelNames, t.label));
        uint32_t ar = idFor(ids.artist, ids.artistNames, t.artist);
        artistId.push_back(ar);
        uint32_t al = 0;
        if (!t.album.empty()) {
            auto key = std::make_pair(ar, t.album);
            auto it = ids.album.find(key);
            if (it == ids.album.end()) { ids.albums.push_back(key); it = ids.album.emplace(key, uint32_t(ids.albums.size())).first; }
            al = it->second;
        }
        albumId.push_back(al);
        uint32_t aw = 0;
        if (!t.artworkKey.empty()) {
            auto it = ids.artwork.find(t.artworkKey);
            if (it == ids.artwork.end()) { ids.artworkPaths.push_back(t.artworkPath); it = ids.artwork.emplace(t.artworkKey, uint32_t(ids.artworkPaths.size())).first; }
            aw = it->second;
        }
        artworkId.push_back(aw);

        std::string path = t.analyzePath;
        if (path.empty()) {
            // The real names look like P00C/0000D1E2: 3 and 8 hex digits. The rule is not known; derive both from a digest of the audio.
            const std::string hex = t.audioHash.empty() ? sha256Hex(t.filePath) : t.audioHash;
            unsigned a = 0, b = 0;
            std::sscanf(hex.substr(0, 3).c_str(), "%x", &a);
            std::sscanf(hex.substr(3, 8).c_str(), "%x", &b);
            char buf[96];
            for (;;) {
                std::snprintf(buf, sizeof buf, "/PIONEER/USBANLZ/P%03X/%08X/ANLZ0000.DAT", a & 0xFFF, b);
                if (usedAnalyze.insert(buf).second) break;
                ++b;                                                       // two tracks hashed alike: step to a free name
            }
            path = buf;
        } else {
            usedAnalyze.insert(path);
        }
        analyze.push_back(path);
    }

    // rows per table type
    std::map<uint32_t, std::vector<std::string>> rows;
    for (size_t i = 0; i < plan.tracks.size(); ++i)
        rows[0].push_back(trackRow(trackTpl, plan.tracks[i], uint32_t(i + 1), genreId[i], albumId[i], artistId[i], labelId[i], artworkId[i], analyze[i]));
    for (size_t i = 0; i < ids.genreNames.size(); ++i) rows[1].push_back(nameRow(uint32_t(i + 1), ids.genreNames[i]));
    for (size_t i = 0; i < ids.artistNames.size(); ++i) rows[2].push_back(artistRow(uint32_t(i + 1), ids.artistNames[i]));
    for (size_t i = 0; i < ids.albums.size(); ++i) rows[3].push_back(albumRow(uint32_t(i + 1), ids.albums[i].first, ids.albums[i].second));
    for (size_t i = 0; i < ids.labelNames.size(); ++i) rows[4].push_back(nameRow(uint32_t(i + 1), ids.labelNames[i]));
    for (size_t i = 0; i < plan.playlists.size(); ++i) rows[7].push_back(playlistRow(uint32_t(i + 1), uint32_t(i), plan.playlists[i].name));
    for (size_t p = 0; p < plan.playlists.size(); ++p)
        for (size_t k = 0; k < plan.playlists[p].tracks.size(); ++k)
            rows[8].push_back(entryRow(uint32_t(k + 1), uint32_t(plan.playlists[p].tracks[k] + 1), uint32_t(p + 1)));
    for (size_t i = 0; i < ids.artworkPaths.size(); ++i) rows[13].push_back(nameRow(uint32_t(i + 1), ids.artworkPaths[i]));

    for (const auto& kv : rows)
        for (const std::string& r : kv.second)
            if (kPdbPageHeader + r.size() + 8 > kPdbPageSize) return fail("a row of table " + std::to_string(kv.first) + " is too large for a page");

    // lay the file out: for each table an index page, its data pages, then one blank page (the table's "empty candidate")
    PdbFile f;
    f.pageSize = kPdbPageSize;
    f.unknown10 = tpl.unknown10;
    f.gap = tpl.gap;
    f.headerTail = tpl.headerTail;
    f.pages.resize(1);
    uint32_t counter = 0;                                                  // u10 of data pages: a running write counter
    BuiltPdb built;
    for (const PdbTable& tt : tpl.tables) {
        const uint32_t type = tt.type;
        if (tt.firstPage == 0 || tt.firstPage >= tpl.pages.size() || tpl.pages[tt.firstPage].structured) return fail("the template has no index page for table " + std::to_string(type));
        const PdbPage& tIndex = tpl.pages[tt.firstPage];
        const uint32_t indexNo = uint32_t(f.pages.size());

        std::vector<PdbPage> data;
        const bool isStatic = type == 6 || type == 16 || type == 17 || type == 18 || type == 19;     // colours and the fixed column lists
        if (isStatic) {
            uint32_t n = tt.firstPage;
            for (size_t guard = 0; n < tpl.pages.size() && guard < tpl.pages.size(); ++guard) {
                const PdbPage& p = tpl.pages[n];
                if (p.type != type) break;
                if (p.structured) data.push_back(p);
                if (n == tt.lastPage || p.next == n) break;
                n = p.next;
            }
        } else {
            auto it = rows.find(type);
            if (it != rows.end() && !it->second.empty()) {
                std::vector<std::vector<PdbRow>> pages = paginate(it->second);
                const PdbPage* sample = nullptr;                            // unknown header words come from the template's page of this type
                for (size_t n = 1; n < tpl.pages.size(); ++n)
                    if (tpl.pages[n].structured && tpl.pages[n].type == type) { sample = &tpl.pages[n]; break; }
                for (std::vector<PdbRow>& pr : pages) {
                    PdbPage p;
                    p.structured = true;
                    p.type = type;
                    p.flags = 0x24;
                    p.u20 = 1;                                              // guess: 1 on every freshly written page of the big export
                    p.numLarge = uint16_t(pr.size() - 1);                   // guess: "last slot" on freshly written pages
                    (void)sample;
                    if (type == 0 || type == 2 || type == 3) applyIndexShift(pr);
                    p.groupFlags = freshGroupFlags(pr);
                    p.rows = std::move(pr);
                    data.push_back(std::move(p));
                }
            }
        }

        // index page: the template's, with the page numbers of this file
        PdbPage idx = tIndex;
        idx.structured = false;
        idx.index = indexNo;
        const uint32_t firstData = data.empty() ? 0 : indexNo + 1;
        const uint32_t blankNo = indexNo + 1 + uint32_t(data.size());
        idx.next = data.empty() ? blankNo : firstData;
        std::string raw = tIndex.raw;
        put32(raw, 0x04, indexNo);
        put32(raw, 0x0c, idx.next);
        put32(raw, 0x28, indexNo);
        put32(raw, 0x2c, data.empty() ? kEmptyEntry : firstData);
        idx.raw = raw;
        f.pages.push_back(idx);

        for (size_t i = 0; i < data.size(); ++i) {
            PdbPage& p = data[i];
            p.index = indexNo + 1 + uint32_t(i);
            p.next = (i + 1 < data.size()) ? p.index + 1 : blankNo;
            if (!isStatic) p.u10 = ++counter;
            f.pages.push_back(p);
            ++built.dataPages;
        }
        PdbPage blank;
        blank.raw.assign(kPdbPageSize, '\0');
        f.pages.push_back(blank);

        PdbTable nt;
        nt.type = type;
        nt.emptyCandidate = blankNo;
        nt.firstPage = indexNo;
        nt.lastPage = data.empty() ? indexNo : indexNo + uint32_t(data.size());
        f.tables.push_back(nt);
    }
    f.nextUnusedPage = uint32_t(f.pages.size());
    f.sequence = counter + 1;

    if (!serialisePdb(f, &built.bytes, err)) return false;
    built.analyzePaths = analyze;
    built.artworkPaths = ids.artworkPaths;
    *out = built;
    return true;
}

// ---- verification -----------------------------------------------------------------------------------------------------------------------

namespace {

struct Collected {
    std::vector<pdb_track_t> tracks;
    std::vector<std::string> titles, filenames, paths, analyze;
    std::map<uint32_t, std::string> artists, genres, labels, artwork;
    std::map<uint32_t, std::pair<uint32_t, std::string>> albums;
    std::vector<pdb_playlist_t> playlists;
    std::vector<std::string> playlistNames;
    std::vector<pdb_playlist_entry_t> entries;
};

std::string str(const char* s) { return s ? s : ""; }

} // namespace

std::vector<std::string> verifyPdb(const std::string& bytes, const ExportPlan& plan, const BuiltPdb& built)
{
    std::vector<std::string> diff;
    auto d = [&](const std::string& m) { diff.push_back(m); };
    const uint8_t* data = reinterpret_cast<const uint8_t*>(bytes.data());
    const uint32_t len = uint32_t(bytes.size());
    Collected c;
    if (pdb_parse(data, len, [](const pdb_track_t* t, void* ud) {
            Collected* c = static_cast<Collected*>(ud);
            c->tracks.push_back(*t);
            c->titles.push_back(str(t->title));
            c->filenames.push_back(str(t->filename));
            c->paths.push_back(str(t->file_path));
            c->analyze.push_back(str(t->analyze_path));
            return 0;
        }, &c) != 0) d("the track table cannot be read");
    auto named = [](const pdb_named_t* n, void* ud) { (*static_cast<std::map<uint32_t, std::string>*>(ud))[n->id] = str(n->name); return 0; };
    pdb_parse_artists(data, len, named, &c.artists);
    pdb_parse_genres(data, len, named, &c.genres);
    pdb_parse_labels(data, len, named, &c.labels);
    pdb_parse_artwork(data, len, named, &c.artwork);
    pdb_parse_albums(data, len, [](const pdb_album_t* a, void* ud) {
        (*static_cast<std::map<uint32_t, std::pair<uint32_t, std::string>>*>(ud))[a->id] = {a->artist_id, str(a->name)};
        return 0;
    }, &c.albums);
    pdb_parse_playlists(data, len, [](const pdb_playlist_t* p, void* ud) {
        Collected* c = static_cast<Collected*>(ud);
        c->playlists.push_back(*p);
        c->playlistNames.push_back(str(p->name));
        return 0;
    }, &c);
    pdb_parse_playlist_entries(data, len, [](const pdb_playlist_entry_t* e, void* ud) { static_cast<Collected*>(ud)->entries.push_back(*e); return 0; }, &c);

    if (c.tracks.size() != plan.tracks.size()) d("track count " + std::to_string(c.tracks.size()) + ", plan has " + std::to_string(plan.tracks.size()));
    const size_t n = std::min(c.tracks.size(), plan.tracks.size());
    for (size_t i = 0; i < n; ++i) {
        const PlanTrack& p = plan.tracks[i];
        const pdb_track_t& t = c.tracks[i];
        const std::string who = "track " + std::to_string(i + 1) + ": ";
        if (t.id != i + 1) d(who + "id " + std::to_string(t.id));
        if (c.titles[i] != p.title) d(who + "title \"" + c.titles[i] + "\" instead of \"" + p.title + "\"");
        if (c.filenames[i] != p.fileName) d(who + "file name \"" + c.filenames[i] + "\"");
        if (c.paths[i] != p.filePath) d(who + "file path \"" + c.paths[i] + "\"");
        if (i < built.analyzePaths.size() && c.analyze[i] != built.analyzePaths[i]) d(who + "analyze path \"" + c.analyze[i] + "\"");
        if (!p.analyzePath.empty() && c.analyze[i] != p.analyzePath) d(who + "analyze path differs from the plan");
        if (t.bpm_x100 != p.bpmX100) d(who + "bpm");
        if (t.duration_secs != p.durationSecs) d(who + "duration");
        if (t.rating != p.rating) d(who + "rating");
        if (t.year != p.year) d(who + "year");
        if (t.file_size != p.fileSize) d(who + "file size");
        if (t.sample_rate != p.sampleRate) d(who + "sample rate");
        if (t.bitrate != p.bitrateKbps) d(who + "bitrate");
        if (t.track_number != p.trackNumber) d(who + "track number");
        if (t.disc_number != p.discNumber) d(who + "disc number");
        auto lookup = [&](const std::map<uint32_t, std::string>& m, uint32_t id, const std::string& want, const char* what) {
            if (want.empty()) { if (id != 0) d(who + what + " set but the plan has none"); return; }
            auto it = m.find(id);
            if (it == m.end()) d(who + what + " id " + std::to_string(id) + " not found");
            else if (it->second != want) d(who + what + " \"" + it->second + "\" instead of \"" + want + "\"");
        };
        lookup(c.artists, t.artist_id, p.artist, "artist");
        lookup(c.genres, t.genre_id, p.genre, "genre");
        lookup(c.labels, t.label_id, p.label, "label");
        if (p.album.empty()) { if (t.album_id != 0) d(who + "album set but the plan has none"); }
        else {
            auto it = c.albums.find(t.album_id);
            if (it == c.albums.end()) d(who + "album id not found");
            else {
                if (it->second.second != p.album) d(who + "album \"" + it->second.second + "\"");
                if (it->second.first != t.artist_id) d(who + "album belongs to another artist");
            }
        }
        if (p.artworkKey.empty()) { if (t.artwork_id != 0) d(who + "artwork set but the plan has none"); }
        else {
            auto it = c.artwork.find(t.artwork_id);
            if (it == c.artwork.end()) d(who + "artwork id not found");
            else if (it->second != p.artworkPath) d(who + "artwork path \"" + it->second + "\"");
        }
    }
    if (c.playlists.size() != plan.playlists.size()) d("playlist count " + std::to_string(c.playlists.size()));
    for (size_t i = 0; i < std::min(c.playlists.size(), plan.playlists.size()); ++i) {
        if (c.playlistNames[i] != plan.playlists[i].name) d("playlist " + std::to_string(i + 1) + " is called \"" + c.playlistNames[i] + "\"");
        if (c.playlists[i].parent_id != 0 || c.playlists[i].is_folder) d("playlist " + std::to_string(i + 1) + " is not at the top level");
        std::vector<std::pair<uint32_t, uint32_t>> got;                     // (entry index, track id)
        for (const pdb_playlist_entry_t& e : c.entries)
            if (e.playlist_id == c.playlists[i].id) got.emplace_back(e.entry_index, e.track_id);
        std::sort(got.begin(), got.end());
        if (got.size() != plan.playlists[i].tracks.size()) { d("playlist \"" + plan.playlists[i].name + "\" has " + std::to_string(got.size()) + " entries"); continue; }
        for (size_t k = 0; k < got.size(); ++k)
            if (got[k].second != plan.playlists[i].tracks[k] + 1) d("playlist \"" + plan.playlists[i].name + "\" entry " + std::to_string(k + 1) + " is track " + std::to_string(got[k].second));
    }
    if (c.artwork.size() != built.artworkPaths.size()) d("artwork count " + std::to_string(c.artwork.size()));
    return diff;
}

} // namespace trakcrate
