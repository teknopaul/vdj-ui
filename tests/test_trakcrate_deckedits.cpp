#include <QtTest/QtTest>

#include "trakcrate/deck_edits.h"

#include <fstream>
#include <unistd.h>

using namespace trakcrate;

class TestDeckEdits : public QObject
{
    Q_OBJECT

    fs::path tmp_;
    Library lib_;

    fs::path makeTrack(const std::string& name, const std::string& title, const std::string& artist)
    {
        const fs::path d = lib_.createTrackDir("House", name);
        Track t;
        t.dir = d;
        t.meta.set("title", title);
        t.meta.set("artist", artist);
        t.hasGrid = true;
        BeatGrid g;
        g.bpm = 120;
        g.firstBeatUs = 100000;
        setBeatGrid(t.grid, g);
        t.hasCuemarks = true;
        Cuemark m;
        m.ms = 1000; m.beat = 1.8; m.hot = 'A'; m.label = "drop";
        t.cuemarks.marks.push_back(m);
        std::string err;
        EXPECT_TRUE(saveMeta(t, &err) && saveBeatGrid(t, &err) && saveCuemarks(t, &err) && saveSource(t, &err));
        std::ofstream(d / "track.wav") << "AUDIO";
        std::ofstream(d / "meta.extra") << "keep";
        return d;
    }
    static void EXPECT_TRUE(bool b) { QVERIFY(b); }

    static std::string slurp(const fs::path& p) { std::string s; readFile(p, &s); return s; }

private slots:
    void init()
    {
        tmp_ = fs::temp_directory_path() / ("vdj-deckedits-" + std::to_string(::getpid()));
        fs::remove_all(tmp_);
        QVERIFY(Library::create(tmp_));
        QVERIFY(Library::open(tmp_, &lib_));
    }
    void cleanup() { fs::remove_all(tmp_); }

    void matchByTitleArtistAndStem()
    {
        const fs::path a = makeTrack("one", "Uno", "Ana");
        makeTrack("two", "Uno", "Bob");
        const auto idx = indexLibrary(lib_);
        Match m = matchLibraryTrack(idx, { "uno", "ANA", "one" });
        QCOMPARE(m.dir.string(), a.string());
        m = matchLibraryTrack(idx, { "Uno", "", "" });
        QVERIFY(m.dir.empty());
        QCOMPARE(m.candidates.size(), size_t(2));         // ambiguous
        m = matchLibraryTrack(idx, { "Nothing", "Nobody", "zzz" });
        QVERIFY(m.dir.empty() && m.candidates.empty());
    }

    // A grid start moved on a real deck (grid start and hot cue A both put at 443 ms): the deck keeps the beat entries (first at 29 ms) and writes
    // the shift (+414 ms) into the PQTZ header.
    void gridOffsetFromDeck()
    {
        const DeckAnalysis d = readDeckAnalysis(DECK_FIXTURE, "/PIONEER/USBANLZ/P059/00015F91/ANLZ0000.DAT");
        QVERIFY(d.ok && d.hasGrid);
        QCOMPARE(d.grid.firstBeatUs, 443000LL);
        QCOMPARE(d.grid.bpm, 137.0);
        QVERIFY(!d.cues.empty());
        QCOMPARE(d.cues[0].ms, 443.0);
    }

    void diffAndApply()
    {
        const fs::path d = makeTrack("one", "Uno", "Ana");
        DeckAnalysis deck;
        deck.ok = deck.hasGrid = true;
        deck.grid.bpm = 120;
        deck.grid.firstBeatUs = 112000;                   // shifted by 12 ms
        Cuemark moved; moved.ms = 1500; moved.hot = 'A';
        Cuemark added; added.ms = 4000;
        deck.cues = { moved, added };

        Track t;
        QVERIFY(loadTrack(d, &t));
        const GridDiff g = diffGrid(t, deck);
        QVERIFY(g.changed);
        QVERIFY(std::fabs(g.shiftMs - 12.0) < 0.01);
        const CueDiff c = diffCues(t, deck);
        QCOMPARE(c.moved, 1);
        QCOMPARE(c.added, 1);
        QCOMPARE(c.removed, 0);

        // dry run writes nothing
        const std::string before = slurp(d / "beatgrid.txt");
        CopyBackOptions o;
        o.grid = true; o.dryRun = true;
        QVERIFY(applyDeckEdits(d, deck, o).changed);
        QCOMPARE(slurp(d / "beatgrid.txt"), before);

        // grid only: cues untouched
        const std::string cuesBefore = slurp(d / "cuemarks.txt");
        o.dryRun = false;
        CopyBackResult r = applyDeckEdits(d, deck, o);
        QVERIFY(r.ok && r.changed);
        QVERIFY(!r.snapshot.empty() && fs::exists(r.snapshot / "beatgrid.txt"));
        QCOMPARE(slurp(r.snapshot / "beatgrid.txt"), before);
        Track after;
        QVERIFY(loadTrack(d, &after));
        QCOMPARE(beatGridFrom(after.grid).firstBeatUs, 112000LL);
        QCOMPARE(origin(after.source, "beatgrid"), std::string("edited"));
        QCOMPARE(origin(after.source, "cues"), std::string("generated"));
        QCOMPARE(after.cuemarks.marks.size(), size_t(1));
        QCOMPARE(after.cuemarks.marks[0].ms, 1000.0);
        QCOMPARE(slurp(d / "track.wav"), std::string("AUDIO"));

        // cues: replaced by the deck's list
        o.grid = false; o.cues = true;
        r = applyDeckEdits(d, deck, o);
        QVERIFY(r.ok && r.changed);
        QVERIFY(loadTrack(d, &after));
        QCOMPARE(after.cuemarks.marks.size(), size_t(2));
        QCOMPARE(origin(after.source, "cues"), std::string("edited"));

        // nothing left to do: no write, no new snapshot
        r = applyDeckEdits(d, deck, o);
        QVERIFY(r.ok && !r.changed && r.snapshot.empty());
        (void)cuesBefore;
    }
};

QTEST_APPLESS_MAIN(TestDeckEdits)
#include "test_trakcrate_deckedits.moc"
