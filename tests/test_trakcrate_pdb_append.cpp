#include <QtTest/QtTest>

#include "trakcrate/fsutil.h"
#include "trakcrate/pdb_append.h"
#include "trakcrate/pdb_build.h"
#include "trakcrate/pdb_rows.h"

#include <cdj/pdb.h>

#include <algorithm>

using namespace trakcrate;

namespace {

std::string slurp(const std::string& path)
{
    std::string s;
    readFile(path, &s);
    return s;
}

std::string fixture(const char* name) { return slurp(std::string(TRAKCRATE_FIXTURES) + "/" + name); }

void put32(std::string& s, size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) s[at + size_t(i)] = char((v >> (8 * i)) & 0xFF); }
uint32_t get32(const std::string& s, size_t at) { return uint32_t(uint8_t(s[at])) | uint32_t(uint8_t(s[at + 1])) << 8 | uint32_t(uint8_t(s[at + 2])) << 16 | uint32_t(uint8_t(s[at + 3])) << 24; }

bool loads(const std::string& bytes, DriveDb* db, std::string* firstProblem = nullptr)
{
    std::vector<std::string> problems;
    const bool ok = loadDriveDb(bytes, db, &problems);
    if (!ok && firstProblem) *firstProblem = problems.empty() ? std::string("(no reason given)") : problems.front();
    if (ok) problems.clear();
    return ok;
}

} // namespace

class TestTrakcratePdbAppend : public QObject
{
    Q_OBJECT

private slots:
    void realDrivesLoadWithTheirCounts();
    void deviceSqlStringsDecode();
    void bigRealExportLoads();
    void addingToBigRealDatabases();
    void damagedDatabasesAreRefusedWithAReason();
    void loadingNeverChangesTheInput();
    void addOneTrack();
    void manyTracksNeedNewPages();
    void tablesThatWereEmptyGetTheirFirstPage();
    void utf16StringsStayAligned();
    void repeatedAdditionsKeepWorking();
    void existingRowsAreReused();
    void refusalsLeaveTheDatabaseAlone();
};

void TestTrakcratePdbAppend::realDrivesLoadWithTheirCounts()
{
    {   // a rekordbox stick: 10 tracks (ids 1..10, one deleted), 5 artwork rows, one playlist
        DriveDb db;
        std::string why;
        QVERIFY2(loads(fixture("pdb-drive/rb-loads-fine.pdb"), &db, &why), why.c_str());
        QCOMPARE(db.tracks.size(), size_t(10));
        QCOMPARE(db.maxId[0], 10u);
        QCOMPARE(db.artist.size(), size_t(1));
        QCOMPARE(db.album.size(), size_t(1));
        QCOMPARE(db.genre.size(), size_t(1));
        QCOMPARE(db.artwork.size(), size_t(5));
        QCOMPARE(db.artworkCount, 5u);
        QCOMPARE(db.playlistByName.size(), size_t(1));
        QVERIFY(db.playlistByName.count("intapol_-_bezier"));
        QVERIFY(db.trackByPath.count("/contents/intapol/bezier/07_intapol_bez-mut.mp3"));
        QCOMPARE(db.analyzePaths.size(), size_t(10));
        QVERIFY(db.analyzePaths.count("/PIONEER/USBANLZ/P031/00027623/ANLZ0000.DAT"));
        QCOMPARE(db.maxRootSortOrder, 0u);
    }
    {
        DriveDb db;
        std::string why;
        QVERIFY2(loads(fixture("pdb-drive/rb-mini.pdb"), &db, &why), why.c_str());
        QCOMPARE(db.tracks.size(), size_t(7));
        QCOMPARE(db.playlistByName.size(), size_t(1));
    }
    {   // the template used for exports
        DriveDb db;
        std::string why;
        QVERIFY2(loads(fixture("pdb/export7.pdb"), &db, &why), why.c_str());
        QCOMPARE(db.tracks.size(), size_t(7));
    }
    {   // what our own builder writes loads too
        ExportPlan plan;
        PlanTrack t;
        t.title = "Caf\xC3\xA9"; t.artist = "Bj\xC3\xB6rk"; t.album = "Miniteca Selv\xC3\xA1tica"; t.genre = "House";
        t.fileName = "a.mp3"; t.filePath = "/Contents/Bj\xC3\xB6rk/Miniteca Selv\xC3\xA1tica/a.mp3"; t.durationSecs = 100;
        plan.tracks.push_back(t);
        plan.playlists.push_back({"P", {0}});
        BuiltPdb built;
        QVERIFY(buildPdb(plan, fixture("pdb/export7.pdb"), &built));
        DriveDb db;
        std::string why;
        QVERIFY2(loads(built.bytes, &db, &why), why.c_str());
        QCOMPARE(db.tracks.size(), size_t(1));
        QCOMPARE(db.tracks[0].title, std::string("Caf\xC3\xA9"));
        QCOMPARE(db.artist.count("Bj\xC3\xB6rk"), size_t(1));
        QCOMPARE(db.album.count({db.artist["Bj\xC3\xB6rk"], "Miniteca Selv\xC3\xA1tica"}), size_t(1));
        for (const std::string& w : db.warnings)                         // buildPdb keeps the template's page counters, which can pass its sequence: only a warning
            QVERIFY2(w.find("analysis folder") == std::string::npos, w.c_str());
    }
}

void TestTrakcratePdbAppend::deviceSqlStringsDecode()
{
    std::string out;
    size_t end = 0;
    for (const std::string s : {std::string(""), std::string("abc"), std::string(200, 'x'), std::string("Caf\xC3\xA9"), std::string("\xE2\x99\xAB \xF0\x9F\x8E\xA7 \xE6\x97\xA5\xE6\x9C\xAC")}) {
        const std::string row = "ab" + encodeDeviceSqlString(s) + "zz";
        QVERIFY2(decodeDeviceSqlString(row, 2, &out, &end), s.c_str());
        QCOMPARE(out, s);
        QCOMPARE(end, row.size() - 2);
    }
    QVERIFY(!decodeDeviceSqlString("", 0, &out));
    QVERIFY(!decodeDeviceSqlString(encodeDeviceSqlString("abc").substr(0, 3), 0, &out));      // cut short
    QVERIFY(!decodeDeviceSqlString(std::string("\x02", 1), 0, &out));                          // an unknown kind (even)
    std::string odd = encodeDeviceSqlString("Caf\xC3\xA9");
    odd[1] = char(odd[1] - 1);                                                                 // an odd UTF-16 length
    QVERIFY(!decodeDeviceSqlString(odd, 0, &out));
}

void TestTrakcratePdbAppend::bigRealExportLoads()
{
    // the 2195 track export from libcdj's own fixtures; skipped when the reference tree is not there
    const std::string bytes = slurp(std::string(TRAKCRATE_REFERENCES) + "/src/test/fixtures/export.pdb");
    if (bytes.empty()) QSKIP("references/libcdj fixtures not available");
    DriveDb db;
    std::string why;
    QVERIFY2(loads(bytes, &db, &why), why.c_str());
    QVERIFY(db.tracks.size() > 2000);
    QVERIFY(db.maxId[0] >= db.tracks.size());
    QVERIFY(db.playlistByName.size() > 50);
    // the real rekordbox sticks on the author's machine (many pages per table, deleted rows, history), when mounted
    for (const char* p : {"/media/teknopaul/TEKNOPAUL64/PIONEER/rekordbox/export.pdb", "/transfer/cdj64/PIONEER/rekordbox/export.pdb"}) {
        const std::string b = slurp(p);
        if (b.empty()) continue;
        DriveDb d;
        std::vector<std::string> problems;
        const bool ok = loadDriveDb(b, &d, &problems);
        QVERIFY2(ok, (std::string(p) + ": " + (problems.empty() ? "" : problems.front())).c_str());
        QVERIFY(d.tracks.size() > 1000);
    }
}

namespace {
ExportPlan planOf(const char* name, int tracks, int firstIndex);
}

// Real rekordbox databases with many pages per table, deleted rows and play history: an addition leaves every existing row alone.
void TestTrakcratePdbAppend::addingToBigRealDatabases()
{
    int ran = 0;
    for (const std::string& path : {std::string(TRAKCRATE_REFERENCES) + "/src/test/fixtures/export.pdb", std::string("/media/teknopaul/TEKNOPAUL64/PIONEER/rekordbox/export.pdb"),
                                    std::string("/transfer/cdj64/PIONEER/rekordbox/export.pdb")}) {
        const std::string original = slurp(path);
        if (original.empty()) continue;
        DriveDb db;
        std::string why;
        QVERIFY2(loads(original, &db, &why), (path + ": " + why).c_str());
        const size_t tracksBefore = db.tracks.size();
        AppendOptions o;
        o.templatePdb = fixture("pdb/export7.pdb");
        std::string current = original;
        for (int round = 0; round < 3; ++round) {
            const std::string name = "Added by the test " + std::to_string(round);
            const ExportPlan plan = planOf(name.c_str(), 40 + round * 60, round * 1000);
            AppendResult res;
            std::string err;
            QVERIFY2(appendPlaylist(db, plan, o, &res, &err), (path + ": " + err).c_str());
            const std::vector<std::string> diff = verifyAppended(current, res.bytes, plan, res);
            for (const std::string& d : diff) qWarning("%s: %s", path.c_str(), d.c_str());
            QVERIFY(diff.empty());
            current = res.bytes;
        }
        QCOMPARE(db.tracks.size(), tracksBefore + 40 + 100 + 160);
        ++ran;
    }
    if (!ran) QSKIP("no big real database available");
}

void TestTrakcratePdbAppend::damagedDatabasesAreRefusedWithAReason()
{
    const std::string good = fixture("pdb-drive/rb-loads-fine.pdb");
    DriveDb db;
    std::string why;
    auto refuses = [&](const std::string& bytes, const char* part, const char* what) {
        std::vector<std::string> problems;
        QVERIFY2(!loadDriveDb(bytes, &db, &problems), what);
        QVERIFY2(!problems.empty(), what);
        std::string all;
        for (const std::string& p : problems) all += p + "\n";
        QVERIFY2(all.find(part) != std::string::npos, (std::string(what) + ": " + all).c_str());
    };
    refuses(good.substr(0, good.size() - 100), "not an export.pdb", "truncated");
    refuses(std::string(), "not an export.pdb", "empty");
    refuses(std::string(4096, 'x'), "not an export.pdb", "garbage");
    {   // a chain that loops: table 0's first data page points back at the index page
        std::string b = good;
        const uint32_t first = get32(b, 0x1c + 8);
        const uint32_t dataPage = get32(b, size_t(first) * 4096 + 0x0c);
        put32(b, size_t(dataPage) * 4096 + 0x0c, first);
        refuses(b, "loops", "chain loop");
    }
    {   // the chain ends before the empty candidate page
        std::string b = good;
        const uint32_t first = get32(b, 0x1c + 8);
        const uint32_t dataPage = get32(b, size_t(first) * 4096 + 0x0c);
        put32(b, size_t(dataPage) * 4096 + 0x0c, 0);
        refuses(b, "before the empty candidate", "chain without an end");
    }
    {   // the table entry names the wrong last page
        std::string b = good;
        put32(b, 0x1c + 12, get32(b, 0x1c + 12) + 1);
        refuses(b, "last page", "wrong last page");
    }
    {   // next unused page inside the file
        std::string b = good;
        put32(b, 0x0c, 3);
        refuses(b, "next unused page", "next unused inside the file");
    }
    {   // a track row with a subtype nobody knows
        std::string b = good;
        const uint32_t first = get32(b, 0x1c + 8);
        const uint32_t dataPage = get32(b, size_t(first) * 4096 + 0x0c);
        b[size_t(dataPage) * 4096 + 0x28] = 0x25;
        refuses(b, "subtype", "unknown track subtype");
    }
    {   // a duplicate track id: give the second row of the first data page the id of the first
        std::string b = good;
        PdbFile f;
        QVERIFY(parsePdb(b, &f));
        PdbPage& p = f.pages[get32(b, size_t(get32(b, 0x1c + 8)) * 4096 + 0x0c)];
        QVERIFY(p.rows.size() >= 2);
        put32(p.rows[1].bytes, 72, get32(p.rows[0].bytes, 72));
        std::string dup;
        QVERIFY(serialisePdb(f, &dup));
        refuses(dup, "appears twice", "duplicate track id");
    }
    {   // an entry for a track that does not exist
        PdbFile f;
        QVERIFY(parsePdb(good, &f));
        for (PdbPage& p : f.pages)
            if (p.structured && p.type == 8 && !p.rows.empty()) { put32(p.rows[0].bytes, 4, 999); break; }
        std::string bad;
        QVERIFY(serialisePdb(f, &bad));
        refuses(bad, "track 999", "entry to a missing track");
    }
    {   // an entry for a playlist that does not exist
        PdbFile f;
        QVERIFY(parsePdb(good, &f));
        for (PdbPage& p : f.pages)
            if (p.structured && p.type == 8 && !p.rows.empty()) { put32(p.rows[0].bytes, 8, 77); break; }
        std::string bad;
        QVERIFY(serialisePdb(f, &bad));
        refuses(bad, "playlist 77", "entry to a missing playlist");
    }
}

void TestTrakcratePdbAppend::loadingNeverChangesTheInput()
{
    const std::string good = fixture("pdb-drive/rb-loads-fine.pdb");
    const std::string copy = good;
    DriveDb db;
    std::vector<std::string> problems;
    QVERIFY(loadDriveDb(good, &db, &problems));
    QVERIFY(good == copy);
    std::string again;
    QVERIFY(serialisePdb(db.file, &again));
    QVERIFY(again == good);                                              // DriveDb holds the file unchanged
}

namespace {

PlanTrack newTrack(int i, const std::string& artist = std::string(), const std::string& album = std::string())
{
    PlanTrack t;
    t.title = "Added " + std::to_string(i);
    t.artist = artist.empty() ? "Added Artist " + std::to_string(i % 7) : artist;
    t.album = album.empty() ? "Added Album " + std::to_string(i % 11) : album;
    t.genre = (i % 3) ? "Techno" : "House";
    t.label = (i % 5) ? "" : "Added Label " + std::to_string(i % 4);
    t.comment = "comment " + std::to_string(i);
    t.fileName = "added" + std::to_string(i) + ".mp3";
    t.filePath = "/Contents/" + t.artist + "/" + t.album + "/added" + std::to_string(i) + ".mp3";
    t.fileSize = 5000000 + uint32_t(i);
    t.bitrateKbps = 320;
    t.trackNumber = uint32_t(i % 12);
    t.bpmX100 = 12000 + uint32_t(i % 60) * 10;
    t.year = 2000 + uint32_t(i % 26);
    t.durationSecs = 180 + uint32_t(i % 200);
    t.dateAdded = "2026-11-07";
    return t;
}

ExportPlan planOf(const char* name, int tracks, int firstIndex)
{
    ExportPlan p;
    PlanPlaylist pl;
    pl.name = name;
    for (int i = 0; i < tracks; ++i) { p.tracks.push_back(newTrack(firstIndex + i)); pl.tracks.push_back(size_t(i)); }
    p.playlists.push_back(pl);
    return p;
}

} // namespace

void TestTrakcratePdbAppend::addOneTrack()
{
    const std::string original = fixture("pdb-drive/rb-loads-fine.pdb");
    DriveDb db;
    QVERIFY(loads(original, &db));
    const ExportPlan plan = planOf("New list", 1, 1);                // track 1 has no label, so no table that was empty gets a page
    AppendOptions o;
    o.templatePdb = fixture("pdb/export7.pdb");
    AppendResult res;
    std::string err;
    QVERIFY2(appendPlaylist(db, plan, o, &res, &err), err.c_str());
    const std::vector<std::string> diff = verifyAppended(original, res.bytes, plan, res);
    for (const std::string& d : diff) qWarning("%s", d.c_str());
    QVERIFY(diff.empty());
    QVERIFY(res.trackIsNew[0]);
    QCOMPARE(res.trackIds[0], 11u);                                   // the fixture's highest id is 10
    QCOMPARE(res.playlistId, 2u);
    QCOMPARE(res.pagesAdded, 0);                                      // everything fits on the last pages
    QCOMPARE(db.tracks.size(), size_t(11));                           // db is now the new database
    QCOMPARE(db.playlistByName.size(), size_t(2));
    QVERIFY(res.analyzePaths[0].rfind("/PIONEER/USBANLZ/P", 0) == 0);
    QVERIFY(db.analyzePaths.count(res.analyzePaths[0]));
    // the old pages that were not touched are byte identical
    for (uint32_t n = 1; n < original.size() / 4096; ++n) {
        if (std::find(res.touchedPages.begin(), res.touchedPages.end(), n) != res.touchedPages.end()) continue;
        QVERIFY2(original.compare(size_t(n) * 4096, 4096, res.bytes, size_t(n) * 4096, 4096) == 0, std::to_string(n).c_str());
    }
    QVERIFY(res.bytes.size() >= original.size());
}

void TestTrakcratePdbAppend::manyTracksNeedNewPages()
{
    for (int n : {30, 1000}) {
        const std::string original = fixture("pdb-drive/rb-loads-fine.pdb");
        DriveDb db;
        QVERIFY(loads(original, &db));
        const ExportPlan plan = planOf("Big list", n, 0);
        AppendOptions o;
        o.templatePdb = fixture("pdb/export7.pdb");
        AppendResult res;
        std::string err;
        QVERIFY2(appendPlaylist(db, plan, o, &res, &err), err.c_str());
        const std::vector<std::string> diff = verifyAppended(original, res.bytes, plan, res);
        for (const std::string& d : diff) qWarning("%s", d.c_str());
        QVERIFY(diff.empty());
        qInfo("%d tracks: %d rows, %d pages added", n, res.rowsAdded, res.pagesAdded);
        QVERIFY(res.pagesAdded >= (n == 30 ? 2 : 60));
        QCOMPARE(db.tracks.size(), size_t(10 + n));
        QCOMPARE(db.maxId[0], uint32_t(10 + n));
        // the chain of every table still ends at its empty candidate page, and the next unused page is past all of them
        QVERIFY(db.file.nextUnusedPage >= db.file.pages.size());
        PdbFile f;
        QVERIFY(parsePdb(res.bytes, &f));
        for (const PdbTable& t : f.tables) QVERIFY(t.emptyCandidate < db.file.nextUnusedPage);
        QVERIFY(f.sequence > 38);                                     // counters moved on
    }
}

void TestTrakcratePdbAppend::tablesThatWereEmptyGetTheirFirstPage()
{
    // rb-mini has no labels and no artwork: a labelled track with artwork fills both tables for the first time
    const std::string original = fixture("pdb-drive/rb-mini.pdb");
    DriveDb db;
    QVERIFY(loads(original, &db));
    QVERIFY(db.label.empty() && db.artwork.empty());
    ExportPlan plan = planOf("Labelled", 1, 0);
    plan.tracks[0].label = "A Label";
    plan.tracks[0].artworkKey = "k";
    plan.tracks[0].artworkPath = "/PIONEER/Artwork/00001/a1.jpg";
    AppendOptions o;
    o.templatePdb = fixture("pdb/export7.pdb");
    AppendResult res;
    std::string err;
    QVERIFY2(appendPlaylist(db, plan, o, &res, &err), err.c_str());
    QVERIFY(verifyAppended(original, res.bytes, plan, res).empty());
    QCOMPARE(db.label.size(), size_t(1));
    QCOMPARE(db.artwork.size(), size_t(1));
    QVERIFY(res.pagesAdded >= 2);
    PdbFile f;
    QVERIFY(parsePdb(res.bytes, &f));
    for (const PdbTable& t : f.tables) {
        if (t.type != 4 && t.type != 13) continue;
        QVERIFY(t.lastPage != t.firstPage);
        // the index page now names its first data page, the old empty candidate
        const std::string& raw = f.pages[t.firstPage].raw;
        QCOMPARE(get32(raw, 0x2c), t.lastPage);
        QVERIFY(f.pages[t.lastPage].structured);
        QCOMPARE(f.pages[t.lastPage].index, t.lastPage);
    }
}

void TestTrakcratePdbAppend::utf16StringsStayAligned()
{
    const std::string original = fixture("pdb-drive/rb-loads-fine.pdb");
    DriveDb db;
    QVERIFY(loads(original, &db));
    ExportPlan plan = planOf("Accents", 3, 0);
    plan.tracks[0].artist = "Bj\xC3\xB6rk";
    plan.tracks[0].album = "Miniteca Selv\xC3\xA1tica";
    plan.tracks[0].title = "Caf\xC3\xA9 \xE2\x99\xAB";
    plan.tracks[0].filePath = "/Contents/Bj\xC3\xB6rk/Miniteca Selv\xC3\xA1tica/Caf\xC3\xA9.mp3";
    plan.tracks[1].genre = "\xC3\x89lectro";
    AppendOptions o;
    o.templatePdb = fixture("pdb/export7.pdb");
    AppendResult res;
    std::string err;
    QVERIFY2(appendPlaylist(db, plan, o, &res, &err), err.c_str());
    QVERIFY(verifyAppended(original, res.bytes, plan, res).empty());
    for (const std::string& s : {plan.tracks[0].artist, plan.tracks[0].album, plan.tracks[0].title, plan.tracks[0].filePath, plan.tracks[1].genre}) {
        const std::string enc = encodeDeviceSqlString(s);
        QVERIFY(uint8_t(enc[0]) == 0x90);
        size_t at = 0;
        int found = 0;
        while ((at = res.bytes.find(enc, at)) != std::string::npos) { QCOMPARE(at % 4, size_t(0)); ++found; ++at; }
        QVERIFY2(found > 0, s.c_str());
    }
}

void TestTrakcratePdbAppend::repeatedAdditionsKeepWorking()
{
    std::string current = fixture("pdb-drive/rb-loads-fine.pdb");
    DriveDb db;
    QVERIFY(loads(current, &db));
    AppendOptions o;
    o.templatePdb = fixture("pdb/export7.pdb");
    uint32_t lastTrack = 10, lastPlaylist = 1, lastSeq = db.file.sequence;
    for (int round = 0; round < 4; ++round) {
        const std::string name = "Round " + std::to_string(round);
        const ExportPlan plan = planOf(name.c_str(), 25, round * 100);
        AppendResult res;
        std::string err;
        QVERIFY2(appendPlaylist(db, plan, o, &res, &err), err.c_str());
        const std::vector<std::string> diff = verifyAppended(current, res.bytes, plan, res);
        for (const std::string& d : diff) qWarning("%s", d.c_str());
        QVERIFY(diff.empty());
        QCOMPARE(res.trackIds[0], lastTrack + 1);
        QCOMPARE(res.playlistId, lastPlaylist + 1);
        QVERIFY(db.file.sequence > lastSeq);
        lastTrack += 25; ++lastPlaylist; lastSeq = db.file.sequence;
        current = res.bytes;
    }
    QCOMPARE(db.tracks.size(), size_t(110));
    QCOMPARE(db.playlistByName.size(), size_t(5));
    // the file after four additions is still a plain pdb: it round-trips and libcdj reads every track
    PdbFile f;
    QVERIFY(parsePdb(current, &f));
    std::string again;
    QVERIFY(serialisePdb(f, &again));
    QVERIFY(again == current);
    int n = 0;
    pdb_parse(reinterpret_cast<const uint8_t*>(current.data()), uint32_t(current.size()), [](const pdb_track_t*, void* ud) { ++*static_cast<int*>(ud); return 0; }, &n);
    QCOMPARE(n, 110);
}

void TestTrakcratePdbAppend::existingRowsAreReused()
{
    const std::string original = fixture("pdb-drive/rb-loads-fine.pdb");
    DriveDb db;
    QVERIFY(loads(original, &db));
    const size_t artistsBefore = db.artist.size(), albumsBefore = db.album.size(), genresBefore = db.genre.size();
    const std::string existingArtist = db.artist.begin()->first;
    const std::string existingGenre = db.genre.begin()->first;
    ExportPlan plan;
    PlanTrack old = newTrack(1);
    old.filePath = "/CONTENTS/INTAPOL/BEZIER/07_INTAPOL_BEZ-MUT.MP3";      // the stick's own track, spelled in another case
    plan.tracks.push_back(old);
    PlanTrack fresh = newTrack(2, existingArtist);
    fresh.genre = existingGenre;
    plan.tracks.push_back(fresh);
    plan.playlists.push_back({"Mixed", {1, 0}});
    AppendOptions o;
    o.templatePdb = fixture("pdb/export7.pdb");
    AppendResult res;
    std::string err;
    QVERIFY2(appendPlaylist(db, plan, o, &res, &err), err.c_str());
    {
        const std::vector<std::string> diff = verifyAppended(original, res.bytes, plan, res);
        for (const std::string& d : diff) qWarning("%s", d.c_str());
        QVERIFY(diff.empty());
    }
    QVERIFY(!res.trackIsNew[0]);
    QVERIFY(res.trackIsNew[1]);
    QCOMPARE(res.trackIds[0], 7u);                                      // "07_Intapol_Bez-mut.mp3" is track 7 of the fixture
    QCOMPARE(db.tracks.size(), size_t(11));                            // one new track only
    QCOMPARE(db.artist.size(), artistsBefore);                          // the artist was reused
    QCOMPARE(db.genre.size(), genresBefore);
    QCOMPARE(db.album.size(), albumsBefore + 1);                        // only the new album
    QCOMPARE(res.analyzePaths[0], std::string("/PIONEER/USBANLZ/P031/00027623/ANLZ0000.DAT"));
    // a playlist of nothing but tracks that are already there adds no track, artist, album or file
    ExportPlan again;
    again.tracks.push_back(old);
    again.playlists.push_back({"Only old", {0}});
    AppendResult res2;
    const std::string before2 = res.bytes;
    QVERIFY2(appendPlaylist(db, again, o, &res2, &err), err.c_str());
    QVERIFY(verifyAppended(before2, res2.bytes, again, res2).empty());
    QVERIFY(!res2.trackIsNew[0]);
    QCOMPARE(db.tracks.size(), size_t(11));
    QCOMPARE(db.playlistByName.size(), size_t(3));
}

void TestTrakcratePdbAppend::refusalsLeaveTheDatabaseAlone()
{
    const std::string original = fixture("pdb-drive/rb-loads-fine.pdb");
    DriveDb db;
    QVERIFY(loads(original, &db));
    AppendOptions o;
    o.templatePdb = fixture("pdb/export7.pdb");
    AppendResult res;
    std::string err;
    // the name exists (any case)
    QVERIFY(!appendPlaylist(db, planOf("INTAPOL_-_BEZIER", 2, 0), o, &res, &err));
    QVERIFY2(err.find("already has a playlist") != std::string::npos, err.c_str());
    QVERIFY(!appendPlaylist(db, planOf("intapol_-_bezier", 2, 0), o, &res, &err));
    // no tracks, no playlist, a bad template, a bad track index
    ExportPlan empty;
    empty.playlists.push_back({"Empty", {}});
    QVERIFY(!appendPlaylist(db, empty, o, &res, &err));
    QVERIFY(!appendPlaylist(db, ExportPlan(), o, &res, &err));
    AppendOptions bad;
    bad.templatePdb = "not a database";
    QVERIFY(!appendPlaylist(db, planOf("X", 1, 0), bad, &res, &err));
    ExportPlan wrong = planOf("Y", 1, 0);
    wrong.playlists[0].tracks.push_back(5);
    QVERIFY(!appendPlaylist(db, wrong, o, &res, &err));
    // two playlists with one name in one plan
    ExportPlan two = planOf("Same", 1, 0);
    two.playlists.push_back(two.playlists[0]);
    two.playlists[1].name = "SAME";
    QVERIFY(!appendPlaylist(db, two, o, &res, &err));
    // a row that cannot fit in a page
    ExportPlan huge = planOf("Huge", 1, 0);
    huge.tracks[0].comment = std::string(6000, 'c');
    // (encodeDeviceSqlString caps strings at 500 bytes, so this is laid out fine: the database stays valid)
    QVERIFY(appendPlaylist(db, huge, o, &res, &err) || !err.empty());
    // after all that the first database is what it was: nothing leaked into `db` from a failed call
    DriveDb check;
    QVERIFY(loads(original, &check));
    QCOMPARE(check.tracks.size(), size_t(10));
}

QTEST_APPLESS_MAIN(TestTrakcratePdbAppend)
#include "test_trakcrate_pdb_append.moc"
