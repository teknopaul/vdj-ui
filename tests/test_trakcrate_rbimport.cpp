#include <QtTest/QtTest>

#include "trakcrate_wav.h"
#include "trakcrate/analysis.h"
#include "trakcrate/exporter.h"
#include "trakcrate/importer.h"
#include "trakcrate/rb_import.h"
#include "trakcrate/settings_files.h"

#include <fstream>

using namespace trakcrate;

namespace {

std::string slurp(const fs::path& p)
{
    std::string s;
    readFile(p, &s);
    return s;
}

bool fakeEncoder(const std::string& src, int size, std::string* jpeg, std::string*)
{
    *jpeg = "\xFF\xD8" "fake" + std::to_string(size) + ":" + src;
    return true;
}

} // namespace

// The drive written by our own exporter is read back by the rekordbox importer into a second library.
class TestTrakcrateRbImport : public QObject
{
    Q_OBJECT

    fs::path tmp_, drive_;
    Library src_, dst_;

    void makeDrive()
    {
        for (const char* n : {"one", "two"}) {
            DecodedPcm p = tcwav::clicks(n[0] == 'o' ? 120 : 128, 0.1, 10);
            tcwav::writeWavMono(tmp_ / "audio" / "House" / (std::string(n) + ".wav"), tcwav::mono(p), p.sampleRate);
        }
        ImportOptions io;
        io.analyse = true;
        QVERIFY(importFolder(src_, tmp_ / "audio" / "House", io).ok);
        Track t;
        QVERIFY(loadTrack(src_.trackDir("House", "one"), &t));
        t.meta.set("title", "Uno");
        t.meta.set("artist", "Ana");
        t.meta.setInt("rating", 4);
        t.hasCuemarks = true;
        Cuemark m;
        m.ms = 1000; m.beat = std::nan(""); m.label = "drop"; m.hot = 'A';
        t.cuemarks.marks.push_back(m);
        QVERIFY(saveMeta(t) && saveCuemarks(t));
        std::ofstream(src_.trackDir("House", "one") / "artwork.png") << "COVER";
        ExportOptions o;
        o.playlists = {"House"};
        o.dest = drive_;
        o.templatePdb = slurp(std::string(TRAKCRATE_FIXTURES) + "/pdb/export7.pdb");
        o.stagingBase = tmp_ / "staging";
        o.encodeArtwork = fakeEncoder;
        fs::create_directories(o.stagingBase);
        ExportResult r = exportDrive(src_, o);
        QVERIFY2(r.ok, r.error.c_str());
    }

    uint32_t pls0Id() { return listRekordboxPlaylists(drive_)[0].id; }

private slots:
    void init()
    {
        tmp_ = makeTempDir("trakcrate-rbimport");
        QVERIFY(!tmp_.empty());
        drive_ = tmp_ / "usb";
        QVERIFY(Library::create(tmp_ / "src", nullptr) && Library::open(tmp_ / "src", &src_));
        QVERIFY(Library::create(tmp_ / "dst", nullptr) && Library::open(tmp_ / "dst", &dst_));
    }
    void cleanup() { std::error_code ec; fs::remove_all(tmp_, ec); }

    void detectsADrive()
    {
        QVERIFY(!isRekordboxDrive(tmp_));
        makeDrive();
        QVERIFY(isRekordboxDrive(drive_));
        RbImportReport r = importRekordboxDrive(dst_, tmp_ / "audio", RbImportOptions());
        QVERIFY(!r.ok);
    }

    void importsEverythingTheDriveHolds()
    {
        makeDrive();
        const std::string before = slurp(drive_ / "PIONEER/rekordbox/export.pdb");
        RbImportOptions o;
        o.settings = true;
        RbImportReport r = importRekordboxDrive(dst_, drive_, o);
        QVERIFY2(r.ok, r.error.c_str());
        QCOMPARE(r.tracks, 2);
        QCOMPARE(r.imported, 2);
        QCOMPARE(r.missingAudio, 0);
        QCOMPARE(r.withGrid, 2);
        QCOMPARE(r.withWaveform, 2);
        QCOMPARE(r.withAnlz, 2);
        QCOMPARE(r.withCues, 1);
        QCOMPARE(r.playlists, 1);
        QCOMPARE(r.settingsFiles, 2);
        QVERIFY(verifySettingsFile(slurp(dst_.settingsDir() / "DEVSETTING.DAT")));
        QCOMPARE(slurp(drive_ / "PIONEER/rekordbox/export.pdb"), before);            // the drive is not touched

        QCOMPARE(dst_.listFolders().size(), size_t(1));
        const std::string folder = dst_.listFolders()[0];
        Track uno;
        bool found = false;
        for (const std::string& n : dst_.listTracks(folder)) {
            Track t;
            QVERIFY(loadTrack(dst_.trackDir(folder, n), &t));
            if (t.title() != "Uno") continue;
            uno = t;
            found = true;
        }
        QVERIFY(found);
        QCOMPARE(uno.meta.get("artist"), std::string("Ana"));
        QCOMPARE(uno.rating(), 4);
        QCOMPARE(origin(uno.source, "meta"), std::string("rekordbox"));
        QCOMPARE(origin(uno.source, "beatgrid"), std::string("rekordbox"));
        QVERIFY(uno.hasGrid);
        QVERIFY(std::fabs(beatGridFrom(uno.grid).bpm - 120.0) < 1.0);
        QVERIFY(uno.hasCuemarks);
        QCOMPARE(uno.cuemarks.marks.size(), size_t(1));
        QCOMPARE(uno.cuemarks.marks[0].hot, 'A');
        QVERIFY(std::fabs(uno.cuemarks.marks[0].ms - 1000) < 2);
        QVERIFY(fs::exists(uno.dir / "waveform.bin"));
        Waveform w;
        QVERIFY(loadWaveform(uno.dir, &w));
        QCOMPARE(w.pwav.size(), size_t(400));
        QVERIFY(fs::exists(uno.dir / "anlz" / "ANLZ0000.DAT"));
        QVERIFY(fs::exists(uno.dir / "anlz" / "ANLZ0000.EXT"));
        QVERIFY(fs::exists(uno.dir / "artwork.jpg"));
        QVERIFY(!audioFileOf(uno.dir).empty());
        QCOMPARE(dst_.listPlaylists().size(), size_t(1));
        QCOMPARE(dst_.playlistTracks(dst_.listPlaylists()[0]).size(), size_t(2));
    }

    void analysisLeavesImportedDataAlone()
    {
        makeDrive();
        QVERIFY(importRekordboxDrive(dst_, drive_, RbImportOptions()).ok);
        const std::string folder = dst_.listFolders()[0];
        const fs::path dir = dst_.trackDir(folder, dst_.listTracks(folder)[0]);
        const std::string grid = slurp(dir / "beatgrid.txt");
        AnalyseResult a = analyseTrack(dir, AnalyseOptions(), nullptr);
        QVERIFY(a.ok);
        QCOMPARE(slurp(dir / "beatgrid.txt"), grid);
    }

    void secondImportAddsNothingAndSettingsAreOptIn()
    {
        makeDrive();
        QVERIFY(importRekordboxDrive(dst_, drive_, RbImportOptions()).ok);
        QVERIFY(!fs::exists(dst_.settingsDir() / "DEVSETTING.DAT"));              // not asked for
        RbImportReport again = importRekordboxDrive(dst_, drive_, RbImportOptions());
        QVERIFY(again.ok);
        QCOMPARE(again.imported, 0);
        QCOMPARE(again.duplicates, 2);
        QVERIFY(!again.warnings.empty());                                        // the playlist already exists
        QCOMPARE(dst_.listTracks(dst_.listFolders()[0]).size(), size_t(2));
    }

    void dryRunWritesNothingAndCancelStops()
    {
        makeDrive();
        RbImportOptions o;
        o.dryRun = true;
        RbImportReport r = importRekordboxDrive(dst_, drive_, o);
        QVERIFY(r.ok);
        QCOMPARE(r.imported, 2);
        QVERIFY(dst_.listFolders().empty());
        std::atomic<bool> cancel(true);
        RbImportReport c = importRekordboxDrive(dst_, drive_, RbImportOptions(), nullptr, &cancel);
        QVERIFY(c.cancelled);
        QCOMPARE(c.imported, 0);
    }

    // playlists A = {a, b} and B = {b, c} (b is in both) on a drive
    void makeOverlapDrive()
    {
        for (const char* n : {"a", "b", "c"}) {
            DecodedPcm p = tcwav::clicks(n[0] == 'a' ? 120 : n[0] == 'b' ? 124 : 128, 0.1, 8);
            tcwav::writeWavMono(tmp_ / "audio3" / (std::string(n) + ".wav"), tcwav::mono(p), p.sampleRate);
        }
        ImportOptions io;
        io.analyse = true;
        io.noPlaylist = true;
        QVERIFY(importFolder(src_, tmp_ / "audio3", io).ok);
        auto dir = [&](const char* n) { return src_.trackDir("audio3", n); };
        QVERIFY(src_.createPlaylist("A") && src_.addTracksToPlaylist("A", {dir("a"), dir("b")}));
        QVERIFY(src_.createPlaylist("B") && src_.addTracksToPlaylist("B", {dir("b"), dir("c")}));
        ExportOptions o;
        o.playlists = {"A", "B"};
        o.dest = drive_;
        o.templatePdb = slurp(std::string(TRAKCRATE_FIXTURES) + "/pdb/export7.pdb");
        o.stagingBase = tmp_ / "staging";
        o.encodeArtwork = fakeEncoder;
        fs::create_directories(o.stagingBase);
        ExportResult r = exportDrive(src_, o);
        QVERIFY2(r.ok, r.error.c_str());
    }

    void playlistsBecomeFolders()
    {
        makeOverlapDrive();
        std::vector<RbPlaylistInfo> pls = listRekordboxPlaylists(drive_);
        QCOMPARE(pls.size(), size_t(2));
        QCOMPARE(pls[0].name, std::string("A"));
        QCOMPARE(pls[0].tracks, 2);
        QCOMPARE(pls[1].tracks, 2);

        RbImportReport r = importRekordboxDrive(dst_, drive_, RbImportOptions());
        QVERIFY2(r.ok, r.error.c_str());
        QCOMPARE(r.imported, 3);
        QCOMPARE(r.folders, 2);
        QCOMPARE(dst_.listFolders(), (std::vector<std::string>{"A", "B"}));
        QCOMPARE(dst_.listTracks("A").size(), size_t(2));          // the shared track b is stored once, in A
        QCOMPARE(dst_.listTracks("B").size(), size_t(1));
        QCOMPARE(dst_.playlistTracks("A").size(), size_t(2));      // both playlists are still complete
        QCOMPARE(dst_.playlistTracks("B").size(), size_t(2));
    }

    void playlistsAppearWhileTheImportRuns()
    {
        makeOverlapDrive();
        bool aEarly = false, bEarly = false;
        RbImportReport r = importRekordboxDrive(dst_, drive_, RbImportOptions(), [&](int done, int total, const std::string&) {
            if (done == total - 1) {                                // the last track is about to be handled
                aEarly = dst_.hasPlaylist("A");
                bEarly = dst_.hasPlaylist("B");
            }
        });
        QVERIFY(r.ok);
        QVERIFY(aEarly);                                            // a and b are in: A exists before c is imported
        QVERIFY(!bEarly);                                           // B waits for c
        QVERIFY(dst_.hasPlaylist("B"));
        QCOMPARE(r.playlists, 2);
    }

    void untickedPlaylistIsNotAFolder()
    {
        makeOverlapDrive();
        RbImportOptions o;
        o.folderOff.insert(pls0Id());                              // A only repeats tracks: no folder for it
        RbImportReport r = importRekordboxDrive(dst_, drive_, o);
        QVERIFY(r.ok);
        QCOMPARE(dst_.listFolders(), (std::vector<std::string>{"B", "rekordbox"}));       // a is in no folder playlist: the one loose folder
        QCOMPARE(dst_.listTracks("B").size(), size_t(2));
        QCOMPARE(dst_.listTracks("rekordbox").size(), size_t(1));
        QCOMPARE(dst_.playlistTracks("A").size(), size_t(2));      // still a playlist

        RbImportOptions pOff;                                      // and a playlist can be left out altogether
        pOff.playlistOff.insert(pls0Id());
        Library other;
        QVERIFY(Library::create(tmp_ / "dst2") && Library::open(tmp_ / "dst2", &other));
        QVERIFY(importRekordboxDrive(other, drive_, pOff).ok);
        QVERIFY(!other.hasPlaylist("A"));
        QVERIFY(other.hasPlaylist("B"));
        QCOMPARE(other.listFolders().size(), size_t(2));           // still a folder of its own
    }

    void looseTracksCanFollowTheDrivesFolders()
    {
        makeOverlapDrive();
        RbImportOptions o;
        o.playlistFolders = false;                                 // the old behaviour: folders as on the drive
        o.looseByPath = true;
        RbImportReport r = importRekordboxDrive(dst_, drive_, o);
        QVERIFY(r.ok);
        QVERIFY(dst_.listFolders().size() >= 1);
        for (const std::string& f : dst_.listFolders()) QVERIFY(f != "A" && f != "B");
    }

    void missingAudioIsReportedNotFatal()
    {
        makeDrive();
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(drive_ / "Contents", ec))
            if (e.is_regular_file(ec)) { fs::remove(e.path(), ec); break; }
        RbImportReport r = importRekordboxDrive(dst_, drive_, RbImportOptions());
        QVERIFY(r.ok);
        QCOMPARE(r.missingAudio, 1);
        QCOMPARE(r.imported, 1);
    }
};

QTEST_MAIN(TestTrakcrateRbImport)
#include "test_trakcrate_rbimport.moc"
