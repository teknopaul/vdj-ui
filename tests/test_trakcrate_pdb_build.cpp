#include <QtTest/QtTest>

#include <cdj/pdb.h>

#include "trakcrate/fsutil.h"
#include "trakcrate/pdb_build.h"
#include "trakcrate/pdb_rows.h"
#include "trakcrate/sha256.h"

#include <cstring>
#include <set>

using namespace trakcrate;

namespace {

std::string slurp(const std::string& path)
{
    std::string s;
    readFile(path, &s);
    return s;
}

PlanTrack makeTrack(int i)
{
    PlanTrack t;
    t.title = "Song " + std::to_string(i);
    t.artist = "Artist " + std::to_string(i % 17);
    t.album = "Album " + std::to_string(i % 40);
    t.genre = (i % 3) ? "Techno" : "House";
    t.label = (i % 5) ? "" : "Label " + std::to_string(i % 4);
    t.comment = "comment " + std::to_string(i);
    t.fileName = "song" + std::to_string(i) + ".mp3";
    t.filePath = "/Contents/Folder/song" + std::to_string(i) + ".mp3";
    t.audioHash = sha256Hex(std::to_string(i));
    t.fileSize = 5000000 + uint32_t(i);
    t.bitrateKbps = 320;
    t.trackNumber = uint32_t(i % 12);
    t.bpmX100 = 12000 + uint32_t(i % 60) * 10;
    t.year = 2000 + uint32_t(i % 26);
    t.durationSecs = 180 + uint32_t(i % 200);
    t.rating = uint8_t(i % 6);
    t.dateAdded = "2026-10-27";
    if (i % 4 == 0) { t.artworkKey = "art" + std::to_string(i % 9); t.artworkPath = "/PIONEER/Artwork/00001/a" + std::to_string(i % 9 + 1) + ".jpg"; }
    return t;
}

} // namespace

class TestTrakcratePdbBuild : public QObject
{
    Q_OBJECT

private:
    std::string tpl_;

    ExportPlan plan(int tracks, int playlists = 1) const
    {
        ExportPlan p;
        for (int i = 0; i < tracks; ++i) p.tracks.push_back(makeTrack(i + 1));
        for (int k = 0; k < playlists; ++k) {
            PlanPlaylist pl;
            pl.name = "Playlist " + std::to_string(k + 1);
            for (int i = 0; i < tracks; ++i) pl.tracks.push_back(size_t((i + k * 3) % std::max(1, tracks)));
            p.playlists.push_back(pl);
        }
        return p;
    }

    void buildAndVerify(const ExportPlan& p, BuiltPdb* b = nullptr)
    {
        BuiltPdb local;
        std::string err;
        QVERIFY2(buildPdb(p, tpl_, &local, &err), err.c_str());
        std::vector<std::string> diff = verifyPdb(local.bytes, p, local);
        for (const std::string& d : diff) qWarning("%s", d.c_str());
        QVERIFY2(diff.empty(), diff.empty() ? "" : diff.front().c_str());
        PdbFile f;
        QVERIFY2(parsePdb(local.bytes, &f, &err), err.c_str());
        std::string again;
        QVERIFY(serialisePdb(f, &again));
        QVERIFY(again == local.bytes);                                    // the output is a well formed file for our own model
        QCOMPARE(f.tables.size(), size_t(20));
        for (size_t n = 1; n < f.pages.size(); ++n)
            if (f.pages[n].type != 0 || f.pages[n].index != 0)             // not a blank page: it knows its own number
                QCOMPARE(f.pages[n].index, uint32_t(n));
        if (b) *b = local;
    }

private slots:
    void initTestCase() { tpl_ = slurp(std::string(TRAKCRATE_FIXTURES) + "/pdb/export7.pdb"); QVERIFY(!tpl_.empty()); }

    void stringEncodings();
    void emptyAndSmallPlans();
    void fileTypeFollowsTheExtension();
    void autoloadHotcuesString();
    void multiPageTables();
    void unicodeAndLongStrings();
    void analyzePathRuleMatchesRealRekordboxDrives();
    void idsAreSharedAndStable();
    void playlists();
    void analyzePaths();
    void sevenTrackPlanFromTheTemplatesOwnMetadata();
    void layoutOfTheBuiltFile();
    void deterministic();
    void badInputs();
};

void TestTrakcratePdbBuild::stringEncodings()
{
    QCOMPARE(encodeDeviceSqlString(""), std::string("\x03", 1));
    QCOMPARE(encodeDeviceSqlString("ON"), std::string("\x07ON", 3));
    QCOMPARE(encodeDeviceSqlString("a"), std::string("\x05" "a", 2));
    std::string s126(126, 'x');
    QCOMPARE(encodeDeviceSqlString(s126).size(), size_t(127));             // longest short string
    QCOMPARE(uint8_t(encodeDeviceSqlString(s126)[0]), uint8_t(0xFF));
    {   // rekordbox's ISRC form (0x90, 0x03, ASCII, NUL) decodes as the code, not as UTF-16
        const std::string isrc("\x90\x12\x00\x00\x03" "GBAHT0500594" "\x00", 18);
        std::string out;
        size_t end = 0;
        QVERIFY(decodeDeviceSqlString(isrc, 0, &out, &end));
        QCOMPARE(out, std::string("GBAHT0500594"));
        QCOMPARE(end, size_t(18));
        const std::string text("\x90\x0a\x00\x00" "a\0b\0c\0", 10);
        QVERIFY(decodeDeviceSqlString(text, 0, &out));
        QCOMPARE(out, std::string("abc"));
    }
    std::string s127(127, 'x');
    std::string l = encodeDeviceSqlString(s127);
    QCOMPARE(uint8_t(l[0]), uint8_t(0x40));                                // long ASCII: 0x40, u16 total (with the 4 byte header), 0
    QCOMPARE(int(uint8_t(l[1]) | uint8_t(l[2]) << 8), 131);
    QCOMPARE(l.size(), size_t(131));
    std::string u = encodeDeviceSqlString("caf\xC3\xA9");                  // non-ASCII: UTF-16LE with the 0x90 header
    QCOMPARE(uint8_t(u[0]), uint8_t(0x90));
    QCOMPARE(int(uint8_t(u[1]) | uint8_t(u[2]) << 8), 12);
    QCOMPARE(u.substr(4), std::string("c\0a\0f\0\xE9\0", 8));
    std::string emoji = encodeDeviceSqlString("\xF0\x9F\x8E\xA7");        // U+1F3A7 becomes a surrogate pair
    QCOMPARE(emoji.substr(4), std::string("\x3C\xD8\xA7\xDF", 4));
    QCOMPARE(uint8_t(encodeDeviceSqlString("tab\there")[0]), uint8_t(0x90));   // control characters take the UTF-16 form too
    QCOMPARE(encodeDeviceSqlString(std::string(10000, 'y')).size(), size_t(500 + 4));   // capped
}

void TestTrakcratePdbBuild::fileTypeFollowsTheExtension()
{
    QCOMPARE(rekordboxFileType("a.mp3"), 1);
    QCOMPARE(rekordboxFileType("a.M4A"), 4);
    QCOMPARE(rekordboxFileType("a.flac"), 5);
    QCOMPARE(rekordboxFileType("a.wav"), 0x0b);
    QCOMPARE(rekordboxFileType("a.aiff"), 0x0c);
    QCOMPARE(rekordboxFileType("a.AIF"), 0x0c);
    QCOMPARE(rekordboxFileType("a.ogg"), -1);
    QCOMPARE(rekordboxFileType("noextension"), -1);

    ExportPlan p = plan(3, 0);
    p.tracks[0].fileName = "x.aiff";
    p.tracks[1].fileName = "x.m4a";
    p.tracks[2].fileName = "x.wav";
    BuiltPdb b;
    buildAndVerify(p, &b);
    PdbFile f;
    QVERIFY(parsePdb(b.bytes, &f));
    const std::vector<const PdbRow*> rows = tableRows(f, 0);
    QCOMPARE(rows.size(), size_t(3));
    auto type = [&](size_t i) { return int(uint8_t(rows[i]->bytes[90]) | uint8_t(rows[i]->bytes[91]) << 8); };
    QCOMPARE(type(0), 0x0c);
    QCOMPARE(type(1), 4);
    QCOMPARE(type(2), 0x0b);
}

void TestTrakcratePdbBuild::autoloadHotcuesString()
{
    ExportPlan p = plan(2, 0);
    p.tracks[1].autoloadHotcues = false;
    BuiltPdb b;
    buildAndVerify(p, &b);
    PdbFile f;
    QVERIFY(parsePdb(b.bytes, &f));
    const std::vector<const PdbRow*> rows = tableRows(f, 0);
    QCOMPARE(rows.size(), size_t(2));
    auto slot7 = [&](size_t i) {
        const std::string& r = rows[i]->bytes;
        const size_t ofs = size_t(uint8_t(r[kTrackFixed + 2 * kStrAutoloadHotcues])) | size_t(uint8_t(r[kTrackFixed + 2 * kStrAutoloadHotcues + 1])) << 8;
        return r.substr(ofs, 3);
    };
    QCOMPARE(slot7(0), std::string("\x07ON", 3));
    QCOMPARE(slot7(1).substr(0, 1), std::string("\x03", 1));
}

void TestTrakcratePdbBuild::emptyAndSmallPlans()
{
    for (int n : {0, 1, 2, 7}) {
        BuiltPdb b;
        buildAndVerify(plan(n, n ? 1 : 0), &b);
        QVERIFY(b.bytes.size() % kPdbPageSize == 0);
    }
    BuiltPdb none;
    buildAndVerify(ExportPlan(), &none);                                   // no tracks, no playlists at all
    PdbFile f;
    QVERIFY(parsePdb(none.bytes, &f));
    QVERIFY(tableRows(f, 0).empty());
    QVERIFY(!tableRows(f, 6).empty());                                     // the static colour table is still there
}

void TestTrakcratePdbBuild::multiPageTables()
{
    BuiltPdb b;
    ExportPlan p = plan(2000, 3);
    buildAndVerify(p, &b);
    PdbFile f;
    QVERIFY(parsePdb(b.bytes, &f));
    QCOMPARE(int(tableRows(f, 0).size()), 2000);
    QCOMPARE(int(tableRows(f, 8).size()), 6000);                           // 3 playlists of 2000
    int trackPages = 0;
    for (size_t n = 1; n < f.pages.size(); ++n) trackPages += (f.pages[n].structured && f.pages[n].type == 0);
    QVERIFY2(trackPages > 100, qPrintable(QString::number(trackPages)));
    // the table's chain: index page -> data pages in order -> the blank page that is its empty candidate
    const PdbTable& t = f.tables[0];
    QCOMPARE(t.type, 0u);
    uint32_t n = t.firstPage;
    int hops = 0;
    while (n != t.emptyCandidate && hops++ < 5000) n = f.pages[n].next;
    QCOMPARE(n, t.emptyCandidate);
    QCOMPARE(hops, trackPages + 1);
    QCOMPARE(f.pages[t.lastPage].next, t.emptyCandidate);
    QVERIFY(!f.pages[t.emptyCandidate].structured);
    QCOMPARE(int(uint8_t(f.pages[t.emptyCandidate].raw[0x1b])), 0);        // blank
    // every page of a table is in the file and tracks are written in id order
    PdbFile g;
    QVERIFY(parsePdb(b.bytes, &g));
    std::vector<const PdbRow*> rows = tableRows(g, 0);
    uint32_t prev = 0;
    for (const PdbRow* r : rows) {
        uint32_t id = uint32_t(uint8_t(r->bytes[72])) | uint32_t(uint8_t(r->bytes[73])) << 8;
        QVERIFY(id == prev + 1);
        prev = id;
    }
}

// The analysis folder is a function of the track's database path (doc/trakcrate/usbanalz-hashing.md): every track of the real rekordbox drives in
// the fixtures must give back the folder rekordbox chose, and tracks that share a slot are told apart by the counter.
void TestTrakcratePdbBuild::analyzePathRuleMatchesRealRekordboxDrives()
{
    struct Seen { std::vector<std::pair<std::string, std::string>> pairs; };
    int checked = 0;
    for (const char* name : {"rb-loads-fine.pdb", "rb-mini.pdb", "../pdb/export7.pdb"}) {
        const std::string bytes = slurp(std::string(TRAKCRATE_FIXTURES) + "/pdb-drive/" + name);
        QVERIFY2(!bytes.empty(), name);
        Seen seen;
        pdb_parse(reinterpret_cast<const uint8_t*>(bytes.data()), uint32_t(bytes.size()),
                  [](const pdb_track_t* t, void* ud) {
                      static_cast<Seen*>(ud)->pairs.emplace_back(t->file_path ? t->file_path : "", t->analyze_path ? t->analyze_path : "");
                      return 0;
                  }, &seen);
        QVERIFY2(!seen.pairs.empty(), name);
        std::set<std::string> used;
        for (const auto& pr : seen.pairs) {
            QCOMPARE(analyzePathFor(pr.first, used), pr.second);
            ++checked;
        }
    }
    QVERIFY(checked >= 10);
    // a second track with the same slot (here the very same path) gets the next counter
    std::set<std::string> used;
    const std::string first = analyzePathFor("/Contents/A/B/c.mp3", used);
    const std::string second = analyzePathFor("/Contents/A/B/c.mp3", used);
    QVERIFY(first.size() > 4 && first.rfind("ANLZ0000.DAT") == first.size() - 12);
    QCOMPARE(second.substr(0, second.size() - 12), first.substr(0, first.size() - 12));
    QVERIFY(second.rfind("ANLZ0001.DAT") == second.size() - 12);
    QVERIFY(analyzePartition(analyzeSlot("/Contents/A/B/c.mp3")) < 128);
    QVERIFY(analyzeSlot("/Contents/A/B/c.mp3") < 200003u);
    // a path with a character outside the BMP counts two UTF-16 units; the slot differs from the same path with an ASCII letter
    QVERIFY(analyzeSlot("/Contents/\xF0\x9F\x8E\xA7.mp3") != analyzeSlot("/Contents/a.mp3"));
}

void TestTrakcratePdbBuild::unicodeAndLongStrings()
{
    ExportPlan p = plan(4);
    p.tracks[0].title = "Caf\xC3\xA9 del Mar \xE2\x80\x93 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";       // accents, dash, Japanese
    p.tracks[1].title = std::string(300, 'L');                                                          // long ASCII
    p.tracks[1].comment = std::string(500, 'c');
    p.tracks[2].artist = "Bj\xC3\xB6rk";
    p.tracks[0].filePath = "/Contents/Wost/Miniteca Selv\xC3\xA1tica/Palo.mp3";
    p.tracks[2].album = "\xE2\x99\xAB Music \xF0\x9F\x8E\xA7";                                           // note + emoji
    p.tracks[3].genre = "";
    p.tracks[3].artist = "";
    p.tracks[3].album = "";
    p.tracks[3].title = "";
    buildAndVerify(p);
    BuiltPdb b;
    std::string err;
    QVERIFY(buildPdb(p, tpl_, &b, &err));
    // every UTF-16 string starts on a 4 byte boundary like in real rekordbox files (a deck crashed on an album name at 2 mod 4)
    for (const std::string& str : {p.tracks[0].title, p.tracks[2].artist, p.tracks[2].album, p.tracks[0].filePath}) {
        const std::string enc = encodeDeviceSqlString(str);
        size_t at = 0;
        int found = 0;
        while ((at = b.bytes.find(enc, at)) != std::string::npos) { QCOMPARE(at % 4, size_t(0)); ++found; ++at; }
        if (uint8_t(enc[0]) == 0x90) QVERIFY2(found > 0, str.c_str());
    }
    // title longer than the cap is shortened, not corrupted
    ExportPlan q = plan(1);
    q.tracks[0].title = std::string(6000, 'T');
    QVERIFY(buildPdb(q, tpl_, &b, &err));
    std::string seen;
    pdb_parse(reinterpret_cast<const uint8_t*>(b.bytes.data()), uint32_t(b.bytes.size()),
              [](const pdb_track_t* t, void* ud) { *static_cast<std::string*>(ud) = t->title ? t->title : ""; return 0; }, &seen);
    QCOMPARE(seen.size(), size_t(500));
}

void TestTrakcratePdbBuild::idsAreSharedAndStable()
{
    ExportPlan p;
    for (int i = 0; i < 6; ++i) {
        PlanTrack t = makeTrack(i + 1);
        t.artist = (i < 3) ? "Same Artist" : "Other Artist";
        t.album = "Same Album";                                            // the same album name under two artists is two albums
        t.genre = "Techno";
        t.artworkKey = (i % 2) ? "k1" : "k2";
        t.artworkPath = (i % 2) ? "/PIONEER/Artwork/00001/a2.jpg" : "/PIONEER/Artwork/00001/a1.jpg";
        p.tracks.push_back(t);
    }
    BuiltPdb b;
    buildAndVerify(p, &b);
    int artists = 0, albums = 0, genres = 0, art = 0;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(b.bytes.data());
    uint32_t n = uint32_t(b.bytes.size());
    auto count = [](const pdb_named_t*, void* ud) { ++*static_cast<int*>(ud); return 0; };
    pdb_parse_artists(d, n, count, &artists);
    pdb_parse_genres(d, n, count, &genres);
    pdb_parse_artwork(d, n, count, &art);
    pdb_parse_albums(d, n, [](const pdb_album_t*, void* ud) { ++*static_cast<int*>(ud); return 0; }, &albums);
    QCOMPARE(artists, 2);
    QCOMPARE(genres, 1);
    QCOMPARE(albums, 2);
    QCOMPARE(art, 2);
    QCOMPARE(b.artworkPaths.size(), size_t(2));
    BuiltPdb b2;
    buildAndVerify(p, &b2);
    QVERIFY(b2.bytes == b.bytes);                                          // first-seen order: ids do not move between builds
}

void TestTrakcratePdbBuild::playlists()
{
    ExportPlan p = plan(5, 0);
    PlanPlaylist empty;
    empty.name = "Empty";
    PlanPlaylist some;
    some.name = "Backwards \xE2\x80\x93 caf\xC3\xA9";
    some.tracks = {4, 3, 2, 1, 0};
    PlanPlaylist dup;
    dup.name = "Twice";
    dup.tracks = {2, 2};                                                   // the same track twice is allowed
    p.playlists = {empty, some, dup};
    buildAndVerify(p);
    ExportPlan bad = plan(2, 0);
    PlanPlaylist broken;
    broken.name = "Broken";
    broken.tracks = {0, 7};
    bad.playlists.push_back(broken);
    BuiltPdb b;
    std::string err;
    QVERIFY(!buildPdb(bad, tpl_, &b, &err));
    QVERIFY(err.find("Broken") != std::string::npos);
}

void TestTrakcratePdbBuild::analyzePaths()
{
    ExportPlan p = plan(300, 0);
    BuiltPdb b;
    buildAndVerify(p, &b);
    std::set<std::string> seen;
    for (const std::string& a : b.analyzePaths) {
        QVERIFY2(a.rfind("/PIONEER/USBANLZ/P", 0) == 0, a.c_str());
        QCOMPARE(a.size(), strlen("/PIONEER/USBANLZ/P00C/0000D1E2/ANLZ0000.DAT"));
        QVERIFY(a.size() > 4 && a.substr(a.size() - 13) == "/ANLZ0000.DAT");
        QVERIFY(seen.insert(a).second);                                    // all distinct
    }
    // two tracks with the same digest still get distinct names
    ExportPlan q = plan(2, 0);
    q.tracks[0].audioHash = q.tracks[1].audioHash = "0123456789abcdef";
    BuiltPdb c;
    buildAndVerify(q, &c);
    QVERIFY(c.analyzePaths[0] != c.analyzePaths[1]);
    // an explicit analyze path is used as given
    q.tracks[0].analyzePath = "/PIONEER/USBANLZ/P00C/0000D1E2/ANLZ0000.DAT";
    BuiltPdb d;
    buildAndVerify(q, &d);
    QCOMPARE(d.analyzePaths[0], q.tracks[0].analyzePath);
}

void TestTrakcratePdbBuild::sevenTrackPlanFromTheTemplatesOwnMetadata()
{
    // read the real drive with libcdj, make a plan from what it says, build, and read the result back: same tracks, ids, strings, playlists
    struct Seen { std::vector<pdb_track_t> t; std::vector<std::string> title, file, path, analyze, comment, mix; std::map<uint32_t, std::string> artists, albums; std::map<uint32_t, uint32_t> albumArtist; };
    Seen s;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(tpl_.data());
    const uint32_t n = uint32_t(tpl_.size());
    pdb_parse(d, n, [](const pdb_track_t* t, void* ud) {
        Seen* s = static_cast<Seen*>(ud);
        s->t.push_back(*t);
        s->title.push_back(t->title ? t->title : "");
        s->file.push_back(t->filename ? t->filename : "");
        s->path.push_back(t->file_path ? t->file_path : "");
        s->analyze.push_back(t->analyze_path ? t->analyze_path : "");
        s->comment.push_back(t->comment ? t->comment : "");
        s->mix.push_back(t->mix_name ? t->mix_name : "");
        return 0;
    }, &s);
    pdb_parse_artists(d, n, [](const pdb_named_t* a, void* ud) { static_cast<Seen*>(ud)->artists[a->id] = a->name ? a->name : ""; return 0; }, &s);
    pdb_parse_albums(d, n, [](const pdb_album_t* a, void* ud) { static_cast<Seen*>(ud)->albums[a->id] = a->name ? a->name : ""; return 0; }, &s);
    QCOMPARE(s.t.size(), size_t(7));

    ExportPlan p;
    for (size_t i = 0; i < s.t.size(); ++i) {
        PlanTrack t;
        t.title = s.title[i]; t.fileName = s.file[i]; t.filePath = s.path[i]; t.analyzePath = s.analyze[i]; t.comment = s.comment[i]; t.mixName = s.mix[i];
        t.artist = s.artists[s.t[i].artist_id]; t.album = s.albums[s.t[i].album_id];
        t.fileSize = s.t[i].file_size; t.sampleRate = s.t[i].sample_rate; t.bitrateKbps = s.t[i].bitrate; t.bpmX100 = s.t[i].bpm_x100;
        t.year = s.t[i].year; t.durationSecs = s.t[i].duration_secs; t.rating = s.t[i].rating; t.playCount = s.t[i].play_count;
        t.sampleDepth = s.t[i].sample_depth; t.trackNumber = s.t[i].track_number;
        t.dateAdded = "2026-09-20";
        p.tracks.push_back(t);
    }
    PlanPlaylist pl;
    pl.name = "Intapol_-_Dembow";
    for (size_t i = 0; i < 7; ++i) pl.tracks.push_back(i);
    p.playlists.push_back(pl);
    BuiltPdb b;
    buildAndVerify(p, &b);

    // compare with the original through the same reader, field by field
    Seen r;
    pdb_parse(reinterpret_cast<const uint8_t*>(b.bytes.data()), uint32_t(b.bytes.size()), [](const pdb_track_t* t, void* ud) {
        static_cast<Seen*>(ud)->t.push_back(*t);
        static_cast<Seen*>(ud)->title.push_back(t->title ? t->title : "");
        static_cast<Seen*>(ud)->analyze.push_back(t->analyze_path ? t->analyze_path : "");
        return 0;
    }, &r);
    QCOMPARE(r.t.size(), s.t.size());
    for (size_t i = 0; i < 7; ++i) {
        QCOMPARE(r.t[i].id, s.t[i].id);
        QCOMPARE(r.title[i], s.title[i]);
        QCOMPARE(r.analyze[i], s.analyze[i]);
        QCOMPARE(r.t[i].file_size, s.t[i].file_size);
        QCOMPARE(r.t[i].bpm_x100, s.t[i].bpm_x100);
        QCOMPARE(r.t[i].duration_secs, s.t[i].duration_secs);
        QCOMPARE(r.t[i].rating, s.t[i].rating);
        QCOMPARE(r.t[i].play_count, s.t[i].play_count);
        QCOMPARE(r.t[i].year, s.t[i].year);
        QCOMPARE(r.t[i].artist_id, s.t[i].artist_id);                       // ids are first-seen order, as rekordbox numbered them here
        QCOMPARE(r.t[i].album_id, s.t[i].album_id);
    }
    // the fixed part of each track row equals the original wherever we know the field: compare the raw rows apart from what we derive
    PdbFile orig, built;
    QVERIFY(parsePdb(tpl_, &orig));
    QVERIFY(parsePdb(b.bytes, &built));
    std::vector<const PdbRow*> a = tableRows(orig, 0), c = tableRows(built, 0);
    QCOMPARE(a.size(), c.size());
    for (size_t i = 0; i < a.size(); ++i) {
        QVERIFY2(a[i]->bytes.size() >= 94 && c[i]->bytes.size() >= 94, "row too short");
        QCOMPARE(a[i]->bytes.substr(0, 4), c[i]->bytes.substr(0, 4));         // subtype and index shift
        QCOMPARE(a[i]->bytes.substr(4, 4), c[i]->bytes.substr(4, 4));
        QCOMPARE(a[i]->bytes.substr(20, 8), c[i]->bytes.substr(20, 8));      // the unknown counter and constant: reproduced
        QCOMPARE(a[i]->bytes.substr(86, 2), c[i]->bytes.substr(86, 2));
        QCOMPARE(a[i]->bytes.substr(90, 4), c[i]->bytes.substr(90, 4));
    }
    qInfo("7 track plan: original pdb %zu bytes, rebuilt %zu bytes", tpl_.size(), b.bytes.size());
}

void TestTrakcratePdbBuild::layoutOfTheBuiltFile()
{
    BuiltPdb b;
    buildAndVerify(plan(3), &b);
    PdbFile f;
    QVERIFY(parsePdb(b.bytes, &f));
    PdbFile t;
    QVERIFY(parsePdb(tpl_, &t));
    QCOMPARE(f.unknown10, t.unknown10);
    QCOMPARE(f.tables.size(), t.tables.size());
    for (size_t i = 0; i < f.tables.size(); ++i) QCOMPARE(f.tables[i].type, t.tables[i].type);       // same 20 tables in the same order
    QCOMPARE(f.nextUnusedPage, uint32_t(f.pages.size()));                  // nothing is promised beyond the end of the file
    // the tracks index page: own number, then the first data page
    const PdbTable& tr = f.tables[0];
    const std::string& idx = f.pages[tr.firstPage].raw;
    auto word = [&](size_t at) { return uint32_t(uint8_t(idx[at])) | uint32_t(uint8_t(idx[at + 1])) << 8 | uint32_t(uint8_t(idx[at + 2])) << 16 | uint32_t(uint8_t(idx[at + 3])) << 24; };
    QCOMPARE(word(0x28), tr.firstPage);
    QCOMPARE(word(0x2c), tr.firstPage + 1);
    QCOMPARE(word(0x30), 0x03ffffffu);
    // an empty table's index page says "no data page"
    const PdbTable& genres = f.tables[1];
    QCOMPARE(genres.type, 1u);
    QVERIFY(genres.lastPage >= genres.firstPage);
    const std::string& gidx = f.pages[f.tables[5].firstPage].raw;           // keys: always empty
    QCOMPARE(uint32_t(uint8_t(gidx[0x2c])) | uint32_t(uint8_t(gidx[0x2d])) << 8 | uint32_t(uint8_t(gidx[0x2e])) << 16 | uint32_t(uint8_t(gidx[0x2f])) << 24, 0x03ffffffu);
    QCOMPARE(f.tables[5].lastPage, f.tables[5].firstPage);
    // the static tables are the template's rows
    QCOMPARE(tableRows(f, 6).size(), tableRows(t, 6).size());
    QCOMPARE(tableRows(f, 16).size(), tableRows(t, 16).size());
    QCOMPARE(tableRows(f, 19).size(), tableRows(t, 19).size());
    for (size_t i = 0; i < tableRows(t, 16).size(); ++i) QCOMPARE(tableRows(f, 16)[i]->bytes, tableRows(t, 16)[i]->bytes);
    // freshly written data pages use the guessed header words
    for (size_t n = 1; n < f.pages.size(); ++n) {
        const PdbPage& p = f.pages[n];
        if (!p.structured || p.type > 13 || p.type == 6) continue;
        QCOMPARE(p.flags, uint8_t(0x24));
        QCOMPARE(p.u20, uint16_t(1));
        QCOMPARE(int(p.numLarge), int(p.rows.size()) - 1);
    }
}

void TestTrakcratePdbBuild::deterministic()
{
    BuiltPdb a, b;
    buildAndVerify(plan(50, 2), &a);
    buildAndVerify(plan(50, 2), &b);
    QVERIFY(a.bytes == b.bytes);
}

void TestTrakcratePdbBuild::badInputs()
{
    BuiltPdb b;
    std::string err;
    QVERIFY(!buildPdb(plan(1), "", &b, &err));
    QVERIFY(!buildPdb(plan(1), std::string(100, 'x'), &b, &err));
    std::string noTracks = tpl_;                                           // a template without a track row cannot give the fixed bytes
    PdbFile f;
    QVERIFY(parsePdb(tpl_, &f));
    f.pages[2].rows.clear();
    f.pages[2].structured = false;
    f.pages[2].raw.assign(kPdbPageSize, '\0');
    QVERIFY(serialisePdb(f, &noTracks));
    QVERIFY(!buildPdb(plan(1), noTracks, &b, &err));
    QVERIFY(!err.empty());
}

QTEST_APPLESS_MAIN(TestTrakcratePdbBuild)
#include "test_trakcrate_pdb_build.moc"
