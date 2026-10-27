#include <QtTest/QtTest>

#include <cdj/anlz.h>
#include <cdj/pdb.h>

#include "trakcrate_wav.h"
#include "trakcrate/cuemark_edit.h"
#include "trakcrate/cuemark_render.h"
#include "trakcrate/exporter.h"
#include "trakcrate/importer.h"

using namespace trakcrate;

namespace {

Waveform quiet(size_t seconds)
{
    Waveform w;
    w.pwv3.assign(seconds * 150, 5);
    w.pwv5.assign(seconds * 150, uint16_t(5 << 2));
    w.pwav.assign(Waveform::kPreviewColumns, 5);
    w.pwv2.assign(Waveform::kTinyColumns, 3);
    return w;
}

Cuemark mark(double ms, char hot = 0)
{
    Cuemark c;
    c.ms = ms;
    c.hot = hot;
    return c;
}

std::string slurp(const fs::path& p)
{
    std::string s;
    readFile(p, &s);
    return s;
}

bool fakeEncoder(const std::string& src, int, std::string* jpeg, std::string*)
{
    *jpeg = "\xFF\xD8" + src;
    return true;
}

} // namespace

class TestTrakcrateCuemarks : public QObject
{
    Q_OBJECT

private slots:
    void spikeAtTheRightColumns()
    {
        const Waveform w = quiet(10);                                          // 10 s
        const Waveform r = renderCuemarks(w, {mark(2000)}, 10000);
        QCOMPARE(int(r.pwv3[299]), 5);
        QCOMPARE(int(r.pwv3[300]), int(kSpikeMono));                           // 2 s x 150
        QCOMPARE(int(r.pwv3[301]), int(kSpikeMono));                           // two columns wide
        QCOMPARE(int(r.pwv3[302]), 5);
        QCOMPARE(int(r.pwv5[300]), int(kSpikeWhite5));
        QCOMPARE(int(r.pwv5[301]), int(kSpikeWhite5));
        QCOMPARE(int(r.pwv5[302]), 5 << 2);
        QCOMPARE(int(r.pwav[80]), int(kSpikeMono));                            // 2 s of 10 s over 400 columns, one wide
        QCOMPARE(int(r.pwav[81]), 5);
        QCOMPARE(int(r.pwv2[20]), int(kSpikeTiny));
        QCOMPARE(int(r.pwv2[21]), 3);
    }
    void theSpikeIsMaximal()
    {
        QCOMPARE(int(kSpikeMono & 0x1F), 31);
        QCOMPARE(int(kSpikeMono >> 5), 7);
        QCOMPARE(int((kSpikeWhite5 >> 2) & 0x1F), 31);
        QCOMPARE(int((kSpikeWhite5 >> 13) & 7), 7);
        QCOMPARE(int((kSpikeWhite5 >> 10) & 7), 7);
        QCOMPARE(int((kSpikeWhite5 >> 7) & 7), 7);
        QCOMPARE(int((kSpikeGreen5 >> 13) & 7), 0);
        QCOMPARE(int((kSpikeGreen5 >> 10) & 7), 7);
        QCOMPARE(int((kSpikeGreen5 >> 7) & 7), 0);
    }
    void clampsAtTheEnds()
    {
        const Waveform w = quiet(10);
        const Waveform r = renderCuemarks(w, {mark(-50), mark(10000), mark(99999)}, 10000);
        QCOMPARE(int(r.pwv3[0]), int(kSpikeMono));
        QCOMPARE(int(r.pwv3[1]), int(kSpikeMono));
        QCOMPARE(int(r.pwv3[1498]), int(kSpikeMono));
        QCOMPARE(int(r.pwv3[1499]), int(kSpikeMono));
        QCOMPARE(r.pwv3.size(), w.pwv3.size());
        QCOMPARE(int(r.pwav[0]), int(kSpikeMono));
        QCOMPARE(int(r.pwav[399]), int(kSpikeMono));
        QCOMPARE(r.pwav.size(), w.pwav.size());
    }
    void deterministicAndTheMasterIsUntouched()
    {
        const Waveform w = quiet(10);
        const Waveform copy = w;
        std::vector<Cuemark> m = {mark(1000), mark(5000), mark(7000)};
        const Waveform a = renderCuemarks(w, m, 10000), b = renderCuemarks(w, m, 10000);
        QVERIFY(a == b);
        QVERIFY(w == copy);
        QVERIFY(!(a == w));
        std::reverse(m.begin(), m.end());
        QVERIFY(renderCuemarks(w, m, 10000) == a);                             // the order of the marks does not matter
    }
    void optionsAndHotCues()
    {
        const Waveform w = quiet(10);
        CuemarkRenderOptions off;
        off.enabled = false;
        QVERIFY(renderCuemarks(w, {mark(2000)}, 10000, off) == w);
        QVERIFY(renderCuemarks(w, {mark(2000, 'A')}, 10000) == w);             // a hot cue is a real cue, not drawn by default
        CuemarkRenderOptions hot;
        hot.drawHot = true;
        QVERIFY(!(renderCuemarks(w, {mark(2000, 'A')}, 10000, hot) == w));
        CuemarkRenderOptions green;
        green.green = true;
        const Waveform g = renderCuemarks(w, {mark(2000)}, 10000, green);
        QCOMPARE(int(g.pwv5[300]), int(kSpikeGreen5));
        QCOMPARE(int(g.pwv3[300]), int(kSpikeMono));                           // the mono tags stay white
        CuemarkRenderOptions one;
        one.widthColumns = 1;
        QCOMPARE(int(renderCuemarks(w, {mark(2000)}, 10000, one).pwv3[301]), 5);
        QVERIFY(renderCuemarks(Waveform(), {mark(2000)}, 10000) == Waveform());  // an empty waveform stays empty
    }
    void snapping()
    {
        BeatGrid g;
        g.bpm = 120;                                                           // a beat every 500 ms
        g.firstBeatUs = 100000;                                                // first beat at 100 ms
        QCOMPARE(snapMs(g, 640, Snap::Beat, 10000), 600.0);
        QCOMPARE(snapMs(g, 860, Snap::Beat, 10000), 1100.0);
        QCOMPARE(snapMs(g, 1900, Snap::Bar, 10000), 2100.0);                   // bars are 2000 ms apart: 100, 2100, ...
        QCOMPARE(snapMs(g, 1000, Snap::Bar, 10000), 100.0);
        QCOMPARE(snapMs(g, 1234.5, Snap::None, 10000), 1234.5);
        QCOMPARE(snapMs(g, -30, Snap::None, 10000), 0.0);
        QCOMPARE(snapMs(g, 99999, Snap::None, 10000), 10000.0);
        QCOMPARE(snapMs(g, 20, Snap::Beat, 10000), 100.0);                     // before the first beat: the first beat
        QCOMPARE(snapMs(BeatGrid(), 640, Snap::Beat, 10000), 640.0);           // no grid: free
        QVERIFY(snapMs(g, 9990, Snap::Beat, 10000) <= 10000.0);
    }
    void editingKeepsTheFileSortedAndConsistent()
    {
        BeatGrid g;
        g.bpm = 120;
        g.firstBeatUs = 0;
        CuemarkFile f;
        QCOMPARE(addCuemark(&f, g, 3000, "b"), 0);
        QCOMPARE(addCuemark(&f, g, 1000, "a"), 0);                             // sorted: the new one is first
        QCOMPARE(addCuemark(&f, g, 1000.4, "dup"), -1);                        // within 1 ms of a mark
        QCOMPARE(int(f.marks.size()), 2);
        QCOMPARE(f.marks[0].beat, 3.0);                                        // 1 s at 120 bpm is beat 3
        QCOMPARE(f.marks[1].beat, 7.0);
        QVERIFY(setCuemarkHot(&f, 0, 'A'));
        QVERIFY(setCuemarkHot(&f, 1, 'A'));                                    // one mark per letter: A moves
        QCOMPARE(int(f.marks[0].hot), 0);
        QCOMPARE(int(f.marks[1].hot), int('A'));
        QVERIFY(!setCuemarkHot(&f, 0, 'D'));
        QVERIFY(setCuemarkLabel(&f, 0, "x\ty"));
        QCOMPARE(f.marks[0].label, std::string("x y"));
        QCOMPARE(moveCuemark(&f, g, 0, 5000), 1);                              // moving past the other re-sorts
        QCOMPARE(int(f.marks[0].hot), int('A'));
        QCOMPARE(moveCuemark(&f, g, 1, 3000.2), -1);                           // onto the other mark
        QVERIFY(removeCuemark(&f, 0));
        QVERIFY(!removeCuemark(&f, 5));
        QCOMPARE(int(f.marks.size()), 1);
        recomputeBeats(&f, BeatGrid());
        QVERIFY(std::isnan(f.marks[0].beat));                                  // no grid: "-" in the file
        QVERIFY(serialiseCuemarks(f).find("-\t") != std::string::npos);
    }
    void savingMarksTheCuesEdited()
    {
        fs::path tmp = makeTempDir("trakcrate-cuemarks-save");
        fs::create_directories(tmp / "t");
        Track t;
        t.dir = tmp / "t";
        CuemarkFile f;
        addCuemark(&f, BeatGrid(), 1500, "drop");
        QVERIFY(saveCuemarkEdit(&t, f));
        Track back;
        QVERIFY(loadTrack(tmp / "t", &back));
        QVERIFY(back.hasCuemarks);
        QCOMPARE(back.cuemarks.marks.size(), size_t(1));
        QCOMPARE(back.cuemarks.marks[0].label, std::string("drop"));
        QCOMPARE(origin(back.source, "cues"), std::string("edited"));
        std::error_code ec;
        fs::remove_all(tmp, ec);
    }
    void exportedFilesCarryTheSpikesAndTheHotCues()
    {
        fs::path tmp = makeTempDir("trakcrate-cuemarks");
        QVERIFY(!tmp.empty());
        Library lib;
        QVERIFY(Library::create(tmp / "lib") && Library::open(tmp / "lib", &lib));
        DecodedPcm p = tcwav::clicks(120, 0.1, 10);
        tcwav::writeWavMono(tmp / "src/Set/alpha.wav", tcwav::mono(p), p.sampleRate);
        ImportOptions io;
        io.analyse = true;
        QVERIFY(importFolder(lib, tmp / "src/Set", io).ok);
        const fs::path dir = lib.trackDir("Set", "alpha");
        Track t;
        QVERIFY(loadTrack(dir, &t));
        t.cuemarks.marks = {mark(2000), mark(4000, 'B')};
        t.hasCuemarks = true;
        QVERIFY(saveCuemarks(t));
        const std::string masterWave = slurp(dir / "waveform.bin");

        auto run = [&](const char* dest, bool spikes) {
            ExportOptions o;
            o.playlists = {"Set"};
            o.dest = tmp / dest;
            o.templatePdb = slurp(std::string(TRAKCRATE_FIXTURES) + "/pdb/export7.pdb");
            o.stagingBase = tmp / "staging";
            o.encodeArtwork = fakeEncoder;
            o.cuemarks.enabled = spikes;
            fs::create_directories(o.stagingBase);
            ExportResult r = exportDrive(lib, o);
            return r;
        };
        { ExportResult r1 = run("with", true); QVERIFY2(r1.ok, r1.error.c_str()); }
        { ExportResult r2 = run("without", false); QVERIFY2(r2.ok, r2.error.c_str()); }
        QVERIFY(slurp(dir / "waveform.bin") == masterWave);                    // the master never changes

        auto open = [&](const char* dest, anlz_track_t** t) {
            const std::string pdb = slurp(tmp / dest / "PIONEER/rekordbox/export.pdb");
            std::string analyze;
            pdb_parse(reinterpret_cast<const uint8_t*>(pdb.data()), uint32_t(pdb.size()), [](const pdb_track_t* tr, void* ud) { *static_cast<std::string*>(ud) = tr->analyze_path; return 0; }, &analyze);
            return anlz_track_open((tmp / dest).c_str(), analyze.c_str(), t) == ANLZ_OK;
        };
        anlz_track_t *with = nullptr, *without = nullptr;
        QVERIFY(open("with", &with) && open("without", &without));
        anlz_waveform_t a, b;
        QCOMPARE(int(anlz_track_waveform(with, ANLZ_WAVE_DETAIL, &a)), int(ANLZ_OK));
        QCOMPARE(int(anlz_track_waveform(without, ANLZ_WAVE_DETAIL, &b)), int(ANLZ_OK));
        QCOMPARE(a.count, b.count);
        QCOMPARE(int(a.data[300]), int(kSpikeMono));                            // 2 s, the plain mark
        QVERIFY(b.data[300] != kSpikeMono);
        QVERIFY(a.data[600] != kSpikeMono);                                     // 4 s is hot cue B: a real cue, no spike
        anlz_waveform_t c5;
        QCOMPARE(int(anlz_track_waveform(with, ANLZ_WAVE_COLOR_DETAIL, &c5)), int(ANLZ_OK));
        anlz_wave_column_t col;
        QVERIFY(anlz_wave_column(&c5, 300, &col));
        QCOMPARE(int(col.height), 31);
        anlz_waveform_t pv, pvo;                                                // the colour preview has no spikes
        QCOMPARE(int(anlz_track_waveform(with, ANLZ_WAVE_COLOR_PREVIEW, &pv)), int(ANLZ_OK));
        QCOMPARE(int(anlz_track_waveform(without, ANLZ_WAVE_COLOR_PREVIEW, &pvo)), int(ANLZ_OK));
        QVERIFY(std::equal(pv.data, pv.data + pv.count * pv.entry_bytes, pvo.data));
        anlz_cue_list_t cues;
        QCOMPARE(int(anlz_track_cues(with, ANLZ_CUES_HOT, &cues)), int(ANLZ_OK));
        QCOMPARE(int(cues.count), 1);
        QCOMPARE(int(cues.cues[0].hot_cue), 2);
        QCOMPARE(int(cues.cues[0].time_ms), 4000);
        anlz_cue_list_free(&cues);
        anlz_track_close(with);
        anlz_track_close(without);
        std::error_code ec;
        fs::remove_all(tmp, ec);
    }
};

QTEST_APPLESS_MAIN(TestTrakcrateCuemarks)
#include "test_trakcrate_cuemarks.moc"
