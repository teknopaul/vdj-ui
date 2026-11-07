#include <QtTest/QtTest>

#include <cdj/pdb.h>

#include "trakcrate/fsutil.h"
#include "trakcrate/pdb_writer.h"

#include <fstream>

using namespace trakcrate;

namespace {

std::string slurp(const std::string& path)
{
    std::string s;
    readFile(path, &s);
    return s;
}

int countRows(const std::string& bytes, uint32_t type)
{
    int n = 0;
    pdb_foreach_row(reinterpret_cast<const uint8_t*>(bytes.data()), uint32_t(bytes.size()), type,
                    [](const uint8_t*, uint32_t, void* ud) { ++*static_cast<int*>(ud); return 0; }, &n);
    return n;
}

struct Titles { std::vector<std::string> t; };
std::vector<std::string> trackTitles(const std::string& bytes)
{
    Titles ts;
    pdb_parse(reinterpret_cast<const uint8_t*>(bytes.data()), uint32_t(bytes.size()),
              [](const pdb_track_t* t, void* ud) { static_cast<Titles*>(ud)->t.push_back(t->title ? t->title : ""); return 0; }, &ts);
    return ts.t;
}

} // namespace

class TestTrakcratePdbRoundtrip : public QObject
{
    Q_OBJECT

private:
    std::string fixture(const char* name) const { return slurp(std::string(TRAKCRATE_FIXTURES) + "/pdb/" + name); }

private slots:
    void rawRoundTripIsByteIdentical();
    void largeRealExportRoundTrips();
    void rekordboxDriveFixturesRoundTripAndKeepTheirChains();
    void dataPagesAreUnderstood();
    void headerFieldsAndChains();
    void rowsAgreeWithLibcdj();
    void rebuiltPagesParseWithLibcdj();
    void addingRowsGrowsGroupsAndFreeSpace();
    void overflowIsRefused();
    void damagedPagesStayRaw();
    void badFilesAreRefused();
};

void TestTrakcratePdbRoundtrip::rawRoundTripIsByteIdentical()
{
    for (const char* name : {"export7.pdb", "exportExt7.pdb"}) {
        const std::string original = fixture(name);
        QVERIFY2(!original.empty(), name);
        PdbFile f;
        std::string err;
        QVERIFY2(parsePdb(original, &f, &err), err.c_str());
        std::string again;
        QVERIFY2(serialisePdb(f, &again, &err), err.c_str());
        QVERIFY2(again == original, name);                       // every byte, including the pages the model does not understand
        QCOMPARE(f.pages.size() * kPdbPageSize, original.size());
    }
}

// Real rekordbox sticks used by the add-to-rekordbox tests (doc/trakcrate/pdb-append-notes.md): every data page is understood, every table's
// chain ends at its empty candidate page, and a page has flags 0x34 exactly when it has deleted rows.
void TestTrakcratePdbRoundtrip::rekordboxDriveFixturesRoundTripAndKeepTheirChains()
{
    for (const char* name : {"rb-loads-fine.pdb", "rb-mini.pdb"}) {
        const std::string original = slurp(std::string(TRAKCRATE_FIXTURES) + "/pdb-drive/" + name);
        QVERIFY2(!original.empty(), name);
        PdbFile f;
        std::string err;
        QVERIFY2(parsePdb(original, &f, &err), err.c_str());
        std::string again;
        QVERIFY2(serialisePdb(f, &again, &err), err.c_str());
        QVERIFY2(again == original, name);
        QVERIFY2(f.nextUnusedPage >= f.pages.size(), name);
        for (const PdbTable& t : f.tables) {
            uint32_t pg = t.firstPage;
            size_t steps = 0;
            while (pg < f.pages.size() && f.pages[pg].next < f.pages.size() && f.pages[pg].next != 0 && steps++ <= f.pages.size()) pg = f.pages[pg].next;
            QVERIFY2(steps <= f.pages.size(), name);                      // no loop
            // the chain ends at the empty candidate page: a blank page inside the file, or a page number beyond the end of the file
            const bool endsAtCandidate = pg == t.emptyCandidate || (pg < f.pages.size() && f.pages[pg].next == t.emptyCandidate);
            QVERIFY2(endsAtCandidate, name);
        }
        for (size_t n = 1; n < f.pages.size(); ++n) {
            const PdbPage& p = f.pages[n];
            if (!p.structured) continue;                                  // index and blank pages
            QVERIFY2(p.flags == (p.presentRows() == int(p.rows.size()) ? 0x24 : 0x34), name);
        }
    }
}

void TestTrakcratePdbRoundtrip::largeRealExportRoundTrips()
{
    // the 2195 track export from libcdj's own fixtures: many pages per table; skipped when the reference tree is not there
    const std::string path = std::string(TRAKCRATE_REFERENCES) + "/src/test/fixtures/export.pdb";
    const std::string original = slurp(path);
    if (original.empty()) QSKIP("references/libcdj fixtures not available");
    PdbFile f;
    std::string err;
    QVERIFY2(parsePdb(original, &f, &err), err.c_str());
    int structured = 0, raw = 0;
    for (size_t n = 1; n < f.pages.size(); ++n) (f.pages[n].structured ? structured : raw)++;
    QVERIFY(structured > 100);
    std::string again;
    QVERIFY2(serialisePdb(f, &again, &err), err.c_str());
    QVERIFY(again == original);
    QCOMPARE(int(tableRows(f, PDB_PAGE_TRACKS).size()), countRows(original, PDB_PAGE_TRACKS));
    QCOMPARE(int(tableRows(f, PDB_PAGE_TRACKS).size()), 2195);
    QCOMPARE(int(tableRows(f, PDB_PAGE_PLAYLIST_ENTRIES).size()), countRows(original, PDB_PAGE_PLAYLIST_ENTRIES));
    qInfo("large export: %d data pages understood, %d kept raw", structured, raw);
}

void TestTrakcratePdbRoundtrip::dataPagesAreUnderstood()
{
    PdbFile f;
    QVERIFY(parsePdb(fixture("export7.pdb"), &f));
    QCOMPARE(f.pages.size(), size_t(41));
    int data = 0;
    for (size_t n = 1; n < f.pages.size(); ++n) {
        const PdbPage& p = f.pages[n];
        if (p.flags == 0x24 || p.flags == 0x34) {
            QVERIFY2(p.structured, qPrintable(QString("page %1 is a data page but was not decoded").arg(n)));
            ++data;
        } else {
            QVERIFY2(!p.structured, qPrintable(QString("page %1 (flags %2) should stay raw").arg(n).arg(p.flags, 0, 16)));
        }
    }
    QCOMPARE(data, 10);
    // known contents of the 7 track export
    QCOMPARE(f.pages[2].rows.size(), size_t(7));                       // tracks
    QCOMPARE(f.pages[2].presentRows(), 7);
    QCOMPARE(f.pages[2].freeBytes(), 1230);                            // the value rekordbox wrote
    QCOMPARE(f.pages[2].heapBytes(), 2808);
    QCOMPARE(f.pages[34].rows.size(), size_t(27));                     // two row groups
    QCOMPARE(f.pages[34].groupFlags.size(), size_t(2));
    QCOMPARE(f.pages[40].rows.size(), size_t(8));                      // eight slots, only the last one in use
    QCOMPARE(f.pages[40].presentRows(), 1);
    QVERIFY(!f.pages[40].rows[0].present && f.pages[40].rows[7].present);
    QCOMPARE(f.pages[40].rows[0].bytes.size(), size_t(40));
}

void TestTrakcratePdbRoundtrip::headerFieldsAndChains()
{
    PdbFile f;
    QVERIFY(parsePdb(fixture("export7.pdb"), &f));
    QCOMPARE(f.tables.size(), size_t(20));
    QCOMPARE(f.nextUnusedPage, 51u);                                   // the header counts pages that are not in the file (it is short)
    QCOMPARE(f.pages.size(), size_t(41));
    QCOMPARE(f.unknown10, 5u);
    QCOMPARE(f.sequence, 26u);
    QCOMPARE(f.tables[0].type, 0u);
    QCOMPARE(f.tables[0].firstPage, 1u);
    QCOMPARE(f.tables[0].lastPage, 2u);                                // the last page that holds rows
    QCOMPARE(f.tables[0].emptyCandidate, 49u);                         // the page the next row would go to: not in the file yet
    QCOMPARE(f.pages[1].type, 0u);                                     // the index page of the tracks table ...
    QCOMPARE(f.pages[1].next, 2u);                                     // ... points at its first data page
    QCOMPARE(f.pages[1].flags, uint8_t(0x64));
    QCOMPARE(f.pages[2].next, 49u);                                    // and that one points past the end of the file
    QCOMPARE(f.pages[4].index, 0u);                                    // blank page
    for (size_t i = 0; i < f.headerTail.size(); ++i) QCOMPARE(int(f.headerTail[i]), 0);
}

void TestTrakcratePdbRoundtrip::rowsAgreeWithLibcdj()
{
    const std::string original = fixture("export7.pdb");
    PdbFile f;
    QVERIFY(parsePdb(original, &f));
    for (uint32_t type = 0; type < 20; ++type)
        QCOMPARE(int(tableRows(f, type).size()), countRows(original, type));
    QCOMPARE(int(tableRows(f, PDB_PAGE_TRACKS).size()), 7);
    QCOMPARE(int(tableRows(f, PDB_PAGE_ARTWORK).size()), 0);
}

void TestTrakcratePdbRoundtrip::rebuiltPagesParseWithLibcdj()
{
    // the semantic re-emit: every data page is rebuilt from its rows alone (free space, heap size, counters, offsets and presence bits
    // are computed, never copied) and must equal the page rekordbox wrote
    const std::string original = fixture("export7.pdb");
    PdbFile f;
    QVERIFY(parsePdb(original, &f));
    PdbFile g = f;
    for (PdbPage& p : g.pages) {
        if (!p.structured) continue;
        PdbPage fresh;
        fresh.zero0 = p.zero0; fresh.index = p.index; fresh.type = p.type; fresh.next = p.next; fresh.u10 = p.u10; fresh.u14 = p.u14;
        fresh.flags = p.flags; fresh.u20 = p.u20; fresh.numLarge = p.numLarge; fresh.u24 = p.u24; fresh.u26 = p.u26;
        fresh.structured = true;
        fresh.groupFlags = p.groupFlags;
        for (const PdbRow& r : p.rows) fresh.rows.push_back(r);
        p = fresh;
    }
    std::string again;
    std::string err;
    QVERIFY2(serialisePdb(g, &again, &err), err.c_str());
    QVERIFY(again == original);
    QCOMPARE(trackTitles(again), trackTitles(original));
    QCOMPARE(int(trackTitles(again).size()), 7);
    QVERIFY(!trackTitles(again)[0].empty());
}

void TestTrakcratePdbRoundtrip::addingRowsGrowsGroupsAndFreeSpace()
{
    const std::string original = fixture("export7.pdb");
    PdbFile f;
    QVERIFY(parsePdb(original, &f));
    PdbPage& p = f.pages[18];                                          // playlist entries: 7 rows of 12 bytes, one group
    QCOMPARE(p.rows.size(), size_t(7));
    const int free0 = p.freeBytes();
    p.rows.push_back(p.rows.back());
    QCOMPARE(p.freeBytes(), free0 - 12 - 2);                           // 12 byte row + 2 byte offset
    for (int i = 0; i < 9; ++i) p.rows.push_back(p.rows.back());       // 17 rows: a second group is needed
    p.groupFlags.push_back(0);
    QCOMPARE(p.rows.size(), size_t(17));
    QCOMPARE(p.freeBytes(), free0 - 10 * 12 - 10 * 2 - 4);             // plus 4 bytes for the second group's words
    std::string out, err;
    QVERIFY2(serialisePdb(f, &out, &err), err.c_str());
    QCOMPARE(out.size(), original.size());
    PdbFile back;
    QVERIFY(parsePdb(out, &back));
    QVERIFY(back.pages[18].structured);
    QCOMPARE(back.pages[18].rows.size(), size_t(17));
    QCOMPARE(back.pages[18].presentRows(), 17);
    QCOMPARE(back.pages[18].groupFlags.size(), size_t(2));
    QCOMPARE(back.pages[18].freeBytes(), p.freeBytes());
    QCOMPARE(countRows(out, PDB_PAGE_PLAYLIST_ENTRIES), 17);          // libcdj reads all of them
    std::string again;
    QVERIFY(serialisePdb(back, &again));
    QVERIFY(again == out);                                             // and the result is stable
    QCOMPARE(countRows(out, PDB_PAGE_TRACKS), 7);                      // other tables untouched
}

void TestTrakcratePdbRoundtrip::overflowIsRefused()
{
    PdbFile f;
    QVERIFY(parsePdb(fixture("export7.pdb"), &f));
    PdbPage& p = f.pages[18];
    for (int i = 0; i < 400; ++i) p.rows.push_back(p.rows.back());     // 400 rows x 12 bytes cannot fit
    p.groupFlags.assign(size_t((p.rows.size() - 1) / 16 + 1), 0);
    std::string out, err;
    QVERIFY(!serialisePdb(f, &out, &err));
    QVERIFY2(err.find("fit") != std::string::npos, err.c_str());
    // missing row flag words are refused too, not guessed
    PdbFile g;
    QVERIFY(parsePdb(fixture("export7.pdb"), &g));
    g.pages[18].rows.push_back(g.pages[18].rows.back());
    g.pages[18].rows.resize(17, g.pages[18].rows.back());
    QVERIFY(!serialisePdb(g, &out, &err));
}

void TestTrakcratePdbRoundtrip::damagedPagesStayRaw()
{
    // damage a data page in ways the writer would never produce: the model keeps the page verbatim, so the file still round trips
    std::string original = fixture("export7.pdb");
    std::string damaged = original;
    damaged[2 * kPdbPageSize + 3000] = 'X';                           // garbage in the free gap of the tracks page
    damaged[6 * kPdbPageSize + 0x1c] = char(damaged[6 * kPdbPageSize + 0x1c] + 1);       // wrong free space counter
    PdbFile f;
    QVERIFY(parsePdb(damaged, &f));
    QVERIFY(!f.pages[2].structured);
    QVERIFY(!f.pages[6].structured);
    QVERIFY(f.pages[8].structured);
    std::string again;
    QVERIFY(serialisePdb(f, &again));
    QVERIFY(again == damaged);
    // fuzz: flipping any single byte of the file never breaks the round trip
    uint32_t rng = 7;
    for (int i = 0; i < 300; ++i) {
        rng = rng * 1664525u + 1013904223u;
        std::string m = original;
        size_t at = kPdbPageSize + (rng >> 8) % (m.size() - kPdbPageSize);        // not the header page: its size fields can be refused
        m[at] = char(m[at] ^ char(1 + (rng & 0x7f)));
        PdbFile g;
        QVERIFY(parsePdb(m, &g));
        std::string out;
        QVERIFY(serialisePdb(g, &out));
        QVERIFY2(out == m, qPrintable(QString("byte %1").arg(at)));
    }
}

void TestTrakcratePdbRoundtrip::badFilesAreRefused()
{
    PdbFile f;
    std::string err;
    QVERIFY(!parsePdb("", &f, &err));
    QVERIFY(!parsePdb(std::string(100, 'x'), &f, &err));
    QVERIFY(!parsePdb(std::string(4097, '\0'), &f, &err));             // not a multiple of the page size
    std::string wrongPage = fixture("export7.pdb");
    wrongPage[4] = 0x20;                                               // page size 8192
    QVERIFY(!parsePdb(wrongPage, &f, &err));
    std::string badMagic = fixture("export7.pdb");
    badMagic[0] = 1;
    QVERIFY(!parsePdb(badMagic, &f, &err));
    std::string tooManyTables = fixture("export7.pdb");
    tooManyTables[8] = char(0xff);
    QVERIFY(!parsePdb(tooManyTables, &f, &err));
    std::string out;
    PdbFile empty;
    QVERIFY(!serialisePdb(empty, &out, &err));
}

QTEST_APPLESS_MAIN(TestTrakcratePdbRoundtrip)
#include "test_trakcrate_pdb_roundtrip.moc"
