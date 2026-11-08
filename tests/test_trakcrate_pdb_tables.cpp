#include <QtTest/QtTest>

#include "trakcrate/pdb_build.h"
#include "trakcrate/pdb_tables.h"

#include <fstream>
#include <sstream>

using namespace trakcrate;

namespace {

std::string slurp(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

PdbFile load(const std::string& path)
{
    PdbFile f;
    std::string err;
    const std::string bytes = slurp(path);
    if (bytes.empty() || !parsePdb(bytes, &f, &err)) qFatal("cannot read %s: %s", path.c_str(), err.c_str());
    return f;
}

const PdbMenuItem* menuItem(const std::vector<PdbMenuItem>& m, uint16_t category)
{
    for (const PdbMenuItem& i : m) if (i.categoryId == category) return &i;
    return nullptr;
}

} // namespace

class TestTrakcratePdbTables : public QObject
{
    Q_OBJECT

    std::string trakcrate_ = std::string(TRAKCRATE_FIXTURES) + "/trakcrate/pdb/export7.pdb";
    std::string demo_ = std::string(TRAKCRATE_FIXTURES) + "/rekordcrate/complete_export/demo_tracks/PIONEER/rekordbox/export.pdb";
    std::string big_ = std::string(TRAKCRATE_REFERENCES) + "/src/test/fixtures/export.pdb";

private slots:
    void columns()
    {
        for (const std::string& path : { trakcrate_, demo_, big_ }) {
            const std::vector<PdbColumn> c = readColumns(load(path));
            QCOMPARE(c.size(), size_t(27));
            QCOMPARE(c[0].id, uint16_t(1));
            QCOMPARE(c[0].name, std::string("GENRE"));
            QCOMPARE(c[1].name, std::string("ARTIST"));
            QCOMPARE(c[2].name, std::string("ALBUM"));
            QCOMPARE(c[3].name, std::string("TRACK"));
            for (const PdbColumn& x : c) QVERIFY2(!x.name.empty() && x.name.find("\xEF\xBF") == std::string::npos, x.name.c_str());
        }
    }

    void menuOfTheLmmsExport()
    {
        const std::vector<PdbMenuItem> m = readMenu(load(trakcrate_));
        QCOMPARE(m.size(), size_t(21));
        const PdbMenuItem* genre = menuItem(m, 1);
        QVERIFY(genre);
        QVERIFY(genre->hidden);                                   // GENRE is hidden in this export
        QCOMPARE(genre->unknown, uint8_t(0x63));
        const PdbMenuItem* artist = menuItem(m, 2);
        QVERIFY(artist && !artist->hidden);
        QCOMPARE(artist->sortOrder, uint16_t(1));
        const PdbMenuItem* track = menuItem(m, 4);
        QVERIFY(track && !track->hidden);
        QCOMPARE(track->sortOrder, uint16_t(3));
        QCOMPARE(track->unknown, uint8_t(1));
    }

    void menuOfARekordbox6Export()
    {
        const std::vector<PdbMenuItem> m = readMenu(load(demo_));
        QCOMPARE(m.size(), size_t(22));
        int visible = 0;
        for (const PdbMenuItem& i : m) visible += !i.hidden;
        QVERIFY(visible > 0 && visible < int(m.size()));
    }

    // rekordbox writes a new history row after every change and only marks the old one deleted: the 7 track LMMS stick shows the tracks being
    // added one by one (0, 1 ... 7), the demo export a track count going 0 2 3 4 5 4 3 2 1 0 1 2.
    void historyKeepsTheChangesOfAStick()
    {
        auto live = [](const std::vector<PdbHistory>& h) {
            std::vector<uint32_t> l;
            for (const PdbHistory& x : h) if (!x.deleted) l.push_back(x.numTracks);
            return l;
        };
        const std::vector<PdbHistory> h = readHistory(load(trakcrate_));
        QCOMPARE(h.size(), size_t(8));
        QCOMPARE(live(h), std::vector<uint32_t>({ 7 }));
        QCOMPARE(h[0].numTracks, uint32_t(0));
        QCOMPARE(h[7].numTracks, uint32_t(7));
        QCOMPARE(h[0].date, std::string("2026-10-16"));
        QCOMPARE(h[0].version, std::string("1000"));
        QVERIFY(h[0].label.empty());

        const std::vector<PdbHistory> d = readHistory(load(demo_));
        QCOMPARE(d.size(), size_t(13));
        QCOMPARE(live(d), std::vector<uint32_t>({ 2 }));
        QCOMPARE(d[0].date, std::string("2022-02-02"));

        const std::vector<PdbHistory> e = readHistory(load(std::string(TRAKCRATE_FIXTURES) + "/rekordcrate/complete_export/empty/PIONEER/rekordbox/export.pdb"));
        QCOMPARE(e.size(), size_t(1));
        QCOMPARE(e[0].numTracks, uint32_t(0));
        QVERIFY(!e[0].deleted);

        const std::vector<PdbHistory> big = readHistory(load(big_));
        QVERIFY(big.size() > 90);
        QCOMPARE(live(big), std::vector<uint32_t>({ 2195 }));
    }

    void playedSessions()
    {
        const PdbFile f = load(big_);
        const std::vector<PdbHistoryPlaylist> p = readHistoryPlaylists(f);
        QCOMPARE(p.size(), size_t(9));
        QCOMPARE(p[0].id, uint32_t(1));
        QCOMPARE(p[0].name, std::string("HISTORY 001"));
        const std::vector<PdbHistoryEntry> e = readHistoryEntries(f);
        QCOMPARE(e.size(), size_t(280));
        QCOMPARE(e[0].playlistId, uint32_t(1));
        QCOMPARE(e[0].index, uint32_t(1));
        for (const PdbHistoryEntry& x : e) QVERIFY(x.playlistId >= 1 && x.playlistId <= 9);
        QVERIFY(readHistoryPlaylists(load(trakcrate_)).empty());
        QVERIFY(readHistoryEntries(load(trakcrate_)).empty());
    }

    // The menu of a built export follows the plan; the rest of the file is untouched.
    void menuSettingsInABuiltExport()
    {
        const std::string tpl = slurp(trakcrate_);
        ExportPlan plan;
        BuiltPdb plain, changed;
        std::string err;
        QVERIFY2(buildPdb(plan, tpl, &plain, &err), err.c_str());
        plan.menu = { { 1, true, 0 }, { 2, false, 9 } };           // show GENRE first, hide ARTIST
        QVERIFY2(buildPdb(plan, tpl, &changed, &err), err.c_str());
        QCOMPARE(changed.bytes.size(), plain.bytes.size());

        PdbFile a, b;
        QVERIFY(parsePdb(plain.bytes, &a) && parsePdb(changed.bytes, &b));
        const std::vector<PdbMenuItem> ma = readMenu(a), mb = readMenu(b);
        QCOMPARE(ma.size(), mb.size());
        QVERIFY(menuItem(ma, 1)->hidden);
        QVERIFY(!menuItem(mb, 1)->hidden);
        QCOMPARE(menuItem(mb, 1)->sortOrder, uint16_t(0));
        QVERIFY(menuItem(mb, 2)->hidden);
        QCOMPARE(menuItem(mb, 2)->sortOrder, uint16_t(9));
        QCOMPARE(menuItem(mb, 4)->sortOrder, menuItem(ma, 4)->sortOrder);       // not named: unchanged
        QCOMPARE(readColumns(b).size(), readColumns(a).size());
        QCOMPARE(readHistory(b).size(), readHistory(a).size());

        std::vector<PdbPage> pages;                                // a settings list for a menu that is not there changes nothing
        QCOMPARE(applyMenuSettings(&pages, { { 1, true, 0 } }), 0);
    }
};

QTEST_APPLESS_MAIN(TestTrakcratePdbTables)
#include "test_trakcrate_pdb_tables.moc"
