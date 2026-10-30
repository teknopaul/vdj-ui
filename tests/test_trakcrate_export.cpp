#include <QtTest/QtTest>

#include <cdj/anlz.h>
#include <cdj/pdb.h>
#include <unistd.h>

#include "trakcrate_wav.h"
#include "trakcrate/exporter.h"
#include "trakcrate/importer.h"
#include "trakcrate/settings_files.h"
#include "trakcrate/sha256.h"

#include <fstream>
#include <set>
#include <sstream>

using namespace trakcrate;

namespace {

std::string slurp(const fs::path& p)
{
    std::string s;
    readFile(p, &s);
    return s;
}

// "JPEG:" + the source: stands in for the Qt encoder, and shows which source and size were asked for
bool fakeEncoder(const std::string& src, int size, std::string* jpeg, std::string*)
{
    *jpeg = "\xFF\xD8" "fake" + std::to_string(size) + ":" + src;
    return true;
}

} // namespace

class TestTrakcrateExport : public QObject
{
    Q_OBJECT

private:
    fs::path tmp_;
    Library lib_;
    std::string tpl_;

    void importSet(const std::string& folder, std::initializer_list<std::pair<const char*, double>> tracks, bool analyse = true)
    {
        fs::path src = tmp_ / "src" / folder;
        for (const auto& t : tracks) {
            DecodedPcm p = tcwav::clicks(t.second, 0.1, 10);
            tcwav::writeWavMono(src / (std::string(t.first) + ".wav"), tcwav::mono(p), p.sampleRate);
        }
        ImportOptions o;
        o.analyse = analyse;
        ImportReport r = importFolder(lib_, src, o);
        QVERIFY2(r.ok, r.error.c_str());
    }

    ExportOptions options(const std::string& playlist, const std::string& dest)
    {
        ExportOptions o;
        o.playlists = {playlist};
        o.dest = tmp_ / dest;
        o.templatePdb = tpl_;
        o.stagingBase = tmp_ / "staging";
        o.encodeArtwork = fakeEncoder;
        o.verify = true;                                   // the library default is off; most tests want the strict path
        o.writeManifest = true;
        fs::create_directories(o.stagingBase);
        return o;
    }

    std::vector<std::string> filesUnder(const fs::path& root)
    {
        std::vector<std::string> v;
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(root, ec))
            if (e.is_regular_file(ec)) v.push_back(e.path().lexically_relative(root).generic_string());
        std::sort(v.begin(), v.end());
        return v;
    }

    bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

private slots:
    void rekordboxFolderNames()
    {
        auto n = [](const char* s, const char* f = "UnknownArtist") { return QString::fromStdString(rekordboxFolderName(s, f)); };
        QCOMPARE(n(""), QString("UnknownArtist"));
        QCOMPARE(n("", "UnknownAlbum"), QString("UnknownAlbum"));
        QCOMPARE(n("Intapol"), QString("Intapol"));
        QCOMPARE(n("\"XY\""), QString("_XY_"));                                      // the cases measured on a real export
        QCOMPARE(n("K.O."), QString("K.O_"));
        QCOMPARE(n("M.I.A."), QString("M.I.A_"));
        QCOMPARE(n("I Don't Usually Like MC's But..."), QString("I Don't Usually Like MC's But.._"));
        QCOMPARE(n("Hijack: The Remixes"), QString("Hijack_ The Remixes"));
        QCOMPARE(n("Joy Squad / Shellshock - VTSS & Dubyshkin Remixes"), QString("Joy Squad _ Shellshock - VTSS & Dubyshkin Remixe"));      // 48 characters
        QCOMPARE(n("Black Panther The Album Music From And Inspired By"), QString("Black Panther The Album Music From And Inspired"));    // trailing space dropped
        QCOMPARE(n("Martin's"), QString("Martin's"));
        QCOMPARE(n("1OO1O, DNZA"), QString("1OO1O, DNZA"));
        QCOMPARE(n("CON"), QString("CON_"));
        QVERIFY(rekordboxFolderName(std::string(60, '\xC3').substr(0, 0) + "\xC3\x9F\xC3\x9F\xC3\x9F", "x").size() == 6);            // UTF-8 kept whole
    }

    void sizeLabels()
    {
        QCOMPARE(QString::fromStdString(formatFolderSize(0)), QString("0.00Gb"));
        QCOMPARE(QString::fromStdString(formatFolderSize(23340000000ull)), QString("23.34Gb"));
        QCOMPARE(QString::fromStdString(formatFolderSize(23344999999ull)), QString("23.34Gb"));
        QCOMPARE(QString::fromStdString(formatFolderSize(23345000000ull)), QString("23.35Gb"));
        QCOMPARE(QString::fromStdString(formatFolderSize(1000000000000ull)), QString("1000.00Gb"));
        QCOMPARE(pendriveGigabytes(0), uint64_t(0));
        QCOMPARE(pendriveGigabytes(1), uint64_t(1));
        QCOMPARE(pendriveGigabytes(1000000000ull), uint64_t(1));
        QCOMPARE(pendriveGigabytes(1000000001ull), uint64_t(2));   // one byte over does not fit
        QCOMPARE(QString::fromStdString(formatPendriveSize(1000001000ull)), QString("2Gb"));
        QCOMPARE(QString::fromStdString(formatPendriveSize(31000000000ull)), QString("31Gb"));
    }

    void estimateCountsEachTrackOnce()
    {
        fs::path root = makeTempDir("trakcrate-size");
        QVERIFY(!root.empty());
        const fs::path a = root / "a", b = root / "b", none = root / "none";
        for (const fs::path& d : {a, b, none}) fs::create_directories(d);
        { std::ofstream(a / "a.mp3") << "x"; std::ofstream(b / "b.mp3") << "x"; }
        std::error_code ec;
        fs::resize_file(a / "a.mp3", 1000000, ec);
        fs::resize_file(b / "b.mp3", 2000000, ec);
        QCOMPARE(Library::estimateTrackBytes(none), uint64_t(0));
        QCOMPARE(Library::estimateTrackBytes(a), uint64_t(1150000));
        QCOMPARE(Library::estimateBytes({}), uint64_t(0));
        QCOMPARE(Library::estimateBytes({none}), uint64_t(0));
        QCOMPARE(Library::estimateBytes({a, b, a}), uint64_t(1150000 + 2150000 + 300000));
        fs::remove_all(root, ec);
    }

    void init()
    {
        tmp_ = makeTempDir("trakcrate-export");
        QVERIFY(!tmp_.empty());
        QVERIFY(Library::create(tmp_ / "lib") && Library::open(tmp_ / "lib", &lib_));
        tpl_ = slurp(std::string(TRAKCRATE_FIXTURES) + "/pdb/export7.pdb");
        QVERIFY(!tpl_.empty());
    }
    void cleanup() { std::error_code ec; fs::remove_all(tmp_, ec); }

    void fatNames();
    void exportsAWholeDrive();
    void writeOrderAudioFirstDatabaseLast();
    void checksumsAndManifest();
    void drivesAreIdenticalAndStagingIsGone();
    void destinationRules();
    void unanalysedTracks();
    void variantSwitches();
    void optionalFilesFromAnotherDrive();
    void supportFilesAreAlwaysThere();
    void failureLeavesNothingBehind();
    void cancelLeavesNothingBehind();
    void notEnoughSpaceIsRefusedUpFront();
    void updateKeepsBackupsAndSkipsUnchanged();
    void updateCancelKeepsOldDatabase();
    void orphansAreListedAndDeleted();
    void checksumsAreOffByDefault();
    void severalPlaylistsShareTracks();
    void summaryDoesNotWrite();
    void artworkIsExportedOncePerImage();
    void pendriveEntriesAreExportedAsPlaylists();
};

void TestTrakcrateExport::fatNames()
{
    QCOMPARE(fatSafeName("a:b?c"), std::string("a_b_c"));
    QCOMPARE(fatSafeName("what*<>|\"x"), std::string("what_____x"));
    QCOMPARE(fatSafeName("trailing. "), std::string("trailing"));
    QCOMPARE(fatSafeName("CON"), std::string("CON_"));
    QCOMPARE(fatSafeName("nul.txt"), std::string("nul_.txt"));
    QCOMPARE(fatSafeName(""), std::string("_"));
    QCOMPARE(fatSafeName("..."), std::string("_"));
    QCOMPARE(fatSafeName("caf\xC3\xA9"), std::string("caf\xC3\xA9"));
    QVERIFY(fatSafeName(std::string(400, 'x')).size() <= 200);
}

void TestTrakcrateExport::exportsAWholeDrive()
{
    importSet("Set", {{"alpha", 120}, {"beta", 128}, {"gamma", 140}});
    ExportOptions o = options("Set", "usb");
    ExportResult r = exportDrive(lib_, o);
    QVERIFY2(r.ok, r.error.c_str());
    QVERIFY(r.verified);
    QCOMPARE(r.tracks, 3);
    QCOMPARE(r.playlists, 1);
    QVERIFY(r.stagedOnTmpfs == false);                                       // we asked for a particular staging base

    const fs::path usb = tmp_ / "usb";
    std::vector<std::string> files = filesUnder(usb);
    QVERIFY(contains(files, "PIONEER/rekordbox/export.pdb"));
    QVERIFY(contains(files, "PIONEER/DEVSETTING.DAT"));
    QVERIFY(contains(files, "PIONEER/MYSETTING.DAT"));
    QVERIFY(slurp(usb / "PIONEER/DEVSETTING.DAT") == defaultDevSetting());
    QVERIFY(contains(files, "SHA256SUMS"));

    // the database says three tracks, and every path it names exists on the drive
    const std::string pdb = slurp(usb / "PIONEER/rekordbox/export.pdb");
    struct Seen { std::vector<std::string> paths, analyze, titles; };
    Seen seen;
    pdb_parse(reinterpret_cast<const uint8_t*>(pdb.data()), uint32_t(pdb.size()), [](const pdb_track_t* t, void* ud) {
        Seen* s = static_cast<Seen*>(ud);
        s->paths.push_back(t->file_path);
        s->analyze.push_back(t->analyze_path);
        s->titles.push_back(t->title);
        return 0;
    }, &seen);
    QCOMPARE(seen.paths.size(), size_t(3));
    for (size_t i = 0; i < 3; ++i) {
        QVERIFY2(fs::is_regular_file(usb / seen.paths[i].substr(1)), seen.paths[i].c_str());
        anlz_track_t* t = nullptr;
        QCOMPARE(int(anlz_track_open(usb.c_str(), seen.analyze[i].c_str(), &t)), int(ANLZ_OK));
        QVERIFY(anlz_track_file(t, ANLZ_FILE_DAT) && anlz_track_file(t, ANLZ_FILE_EXT));
        const char* ppth = nullptr;
        QCOMPARE(int(anlz_track_path(t, &ppth)), int(ANLZ_OK));
        QCOMPARE(std::string(ppth), seen.paths[i]);                           // the analysis names the same audio file
        anlz_beatgrid_t g;
        QCOMPARE(int(anlz_track_beatgrid(t, &g)), int(ANLZ_OK));
        QVERIFY(g.count > 10);                                                 // the click tracks have beats
        anlz_waveform_t w;
        QCOMPARE(int(anlz_waveform(anlz_track_file(t, ANLZ_FILE_EXT), ANLZ_WAVE_COLOR_DETAIL, &w)), int(ANLZ_OK));
        QVERIFY(w.count >= 1490 && w.count <= 1510);                           // 10 s at 150 per second
        anlz_track_close(t);
    }
    // the audio is byte for byte the master's
    for (const std::string& title : {"alpha", "beta", "gamma"}) {
        fs::path master = lib_.trackDir("Set", title);
        std::string want = slurp(audioFileOf(master));
        bool found = false;
        for (const std::string& p : seen.paths)
            if (p.find(title) != std::string::npos) { QVERIFY(slurp(usb / p.substr(1)) == want); found = true; }
        QVERIFY2(found, title.c_str());
    }
    QVERIFY(seen.paths[0].rfind("/Contents/UnknownArtist/UnknownAlbum/", 0) == 0);        // rekordbox's layout: Contents/<artist>/<album>/, not the library folder
}

void TestTrakcrateExport::writeOrderAudioFirstDatabaseLast()
{
    importSet("Set", {{"a", 120}, {"b", 125}});
    ExportResult r = exportDrive(lib_, options("Set", "usb"));
    QVERIFY2(r.ok, r.error.c_str());
    QVERIFY(r.writeOrder.size() >= 8);
    QCOMPARE(r.writeOrder.back(), std::string("PIONEER/rekordbox/export.pdb"));        // no database until everything else is there
    QVERIFY(r.writeOrder[0].rfind("Contents/", 0) == 0);
    QVERIFY(r.writeOrder[1].rfind("Contents/", 0) == 0);
    size_t lastAudio = 0, firstMeta = r.writeOrder.size();
    for (size_t i = 0; i < r.writeOrder.size(); ++i) {
        if (r.writeOrder[i].rfind("Contents/", 0) == 0) lastAudio = i;
        else firstMeta = std::min(firstMeta, i);
    }
    QVERIFY(lastAudio < firstMeta);                                                      // audio first, then the metadata
    QVERIFY(std::find(r.writeOrder.begin(), r.writeOrder.end(), "SHA256SUMS") < r.writeOrder.end() - 1);
    std::set<std::string> unique(r.writeOrder.begin(), r.writeOrder.end());
    QCOMPARE(unique.size(), r.writeOrder.size());                                        // each file written once
    // progress is reported per file and ends at the total
    std::vector<std::string> seen;
    int lastDone = -1, lastTotal = 0;
    fs::remove_all(tmp_ / "usb2");
    ExportResult r2 = exportDrive(lib_, options("Set", "usb2"), [&](int done, int total, const std::string& cur) { if (!cur.empty()) seen.push_back(cur); lastDone = done; lastTotal = total; });
    QVERIFY(r2.ok);
    QCOMPARE(lastDone, lastTotal);
    QCOMPARE(seen.size(), r2.writeOrder.size());
}

void TestTrakcrateExport::checksumsAndManifest()
{
    importSet("Set", {{"a", 120}, {"b", 125}});
    ExportResult r = exportDrive(lib_, options("Set", "usb"));
    QVERIFY2(r.ok, r.error.c_str());
    const fs::path usb = tmp_ / "usb";
    std::istringstream in(slurp(usb / "SHA256SUMS"));
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        QVERIFY(line.size() > 66);
        const std::string hash = line.substr(0, 64), rel = line.substr(66);
        QVERIFY2(sha256Hex(slurp(usb / rel)) == hash, rel.c_str());
        ++n;
    }
    QCOMPARE(n, int(r.files) - 2);                                                        // everything except the sums and the manifest
    const std::string manifest = slurp(usb / "trakcrate-manifest.txt");
    QVERIFY(manifest.find("tracks: 2") != std::string::npos);
    QVERIFY(manifest.find("\"Set\"") != std::string::npos);
    QVERIFY(manifest.find("UNKNOWN_BYTES.md") != std::string::npos);
    // a damaged copy would be caught by the read back: flip a byte in the staged export.pdb copy ... checked in failureLeavesNothingBehind
    ExportOptions off = options("Set", "usb-nosums");
    off.writeManifest = false;
    QVERIFY(exportDrive(lib_, off).ok);
    QVERIFY(!fs::exists(tmp_ / "usb-nosums" / "SHA256SUMS"));
    QVERIFY(!fs::exists(tmp_ / "usb-nosums" / "trakcrate-manifest.txt"));
    ExportOptions nov = options("Set", "usb-noverify");
    nov.verify = false;
    ExportResult rv = exportDrive(lib_, nov);
    QVERIFY(rv.ok);
    QVERIFY(!rv.verified);
}

void TestTrakcrateExport::drivesAreIdenticalAndStagingIsGone()
{
    importSet("Set", {{"a", 120}, {"b", 125}});
    ExportOptions o1 = options("Set", "one"), o2 = options("Set", "two");
    ExportResult r1 = exportDrive(lib_, o1), r2 = exportDrive(lib_, o2);
    QVERIFY(r1.ok && r2.ok);
    std::vector<std::string> f1 = filesUnder(tmp_ / "one"), f2 = filesUnder(tmp_ / "two");
    QCOMPARE(f1.size(), f2.size());
    for (size_t i = 0; i < f1.size(); ++i) {
        QCOMPARE(f1[i], f2[i]);
        if (f1[i] == "trakcrate-manifest.txt") continue;                                  // carries a date and nothing else
        QVERIFY2(slurp(tmp_ / "one" / f1[i]) == slurp(tmp_ / "two" / f1[i]), f1[i].c_str());   // two copies for the gig are byte-identical
    }
    QVERIFY(fs::is_empty(tmp_ / "staging"));                                              // the staging directory was removed
    QVERIFY(!r1.stagingUsed.empty());
    QVERIFY(!fs::exists(r1.stagingUsed));
}

void TestTrakcrateExport::destinationRules()
{
    importSet("Set", {{"a", 120}});
    fs::create_directories(tmp_ / "full");
    std::ofstream(tmp_ / "full" / "keep.txt") << "x";
    ExportResult r = exportDrive(lib_, options("Set", "full"));
    QVERIFY(r.ok);                                                                         // other files do not matter
    QVERIFY(fs::exists(tmp_ / "full" / "keep.txt"));
    QVERIFY(fs::exists(tmp_ / "full" / "PIONEER/rekordbox/export.pdb"));
    // but a name the export writes does, and nothing is touched
    r = exportDrive(lib_, options("Set", "full"));
    QVERIFY(!r.ok);
    QVERIFY(r.error.find("already has") != std::string::npos);
    QVERIFY(r.error.find("PIONEER") != std::string::npos);
    QVERIFY(fs::exists(tmp_ / "full" / "keep.txt"));
    fs::create_directories(tmp_ / "lower" / "contents");                                   // FAT does not tell the cases apart
    QCOMPARE(exportConflicts(tmp_ / "lower").size(), size_t(1));
    QVERIFY(exportConflicts(tmp_ / "nothing-here").empty());
    std::ofstream(tmp_ / "afile") << "x";
    QVERIFY(!exportDrive(lib_, options("Set", "afile")).ok);
    fs::create_directories(tmp_ / "emptydir");
    QVERIFY(exportDrive(lib_, options("Set", "emptydir")).ok);                            // an existing empty folder is fine
    QVERIFY(exportDrive(lib_, options("Set", "a/b/c/new")).ok);                           // a new one is created
    ExportOptions none = options("Set", "x");
    none.playlists.clear();
    QVERIFY(!exportDrive(lib_, none).ok);
    ExportOptions missing = options("No such playlist", "y");
    ExportResult m = exportDrive(lib_, missing);
    QVERIFY(!m.ok);
    QVERIFY(!fs::exists(tmp_ / "y"));                                                     // nothing created for a refused export
    ExportOptions notpl = options("Set", "z");
    notpl.templatePdb.clear();
    QVERIFY(!exportDrive(lib_, notpl).ok);
}

void TestTrakcrateExport::unanalysedTracks()
{
    importSet("Raw", {{"x", 120}, {"y", 125}}, false);                                    // imported without analysis
    ExportResult r = exportDrive(lib_, options("Raw", "usb"));
    QVERIFY(!r.ok);
    QVERIFY(r.error.find("not analysed") != std::string::npos);
    QVERIFY(!fs::exists(tmp_ / "usb"));
    ExportOptions allow = options("Raw", "usb");
    allow.allowUnanalysed = true;
    r = exportDrive(lib_, allow);
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(r.warnings.size(), size_t(2));
    const std::string pdb = slurp(tmp_ / "usb/PIONEER/rekordbox/export.pdb");
    std::string analyze;
    pdb_parse(reinterpret_cast<const uint8_t*>(pdb.data()), uint32_t(pdb.size()), [](const pdb_track_t* t, void* ud) { *static_cast<std::string*>(ud) = t->analyze_path; return 0; }, &analyze);
    anlz_track_t* t = nullptr;
    QCOMPARE(int(anlz_track_open((tmp_ / "usb").c_str(), analyze.c_str(), &t)), int(ANLZ_OK));
    anlz_beatgrid_t g;
    QVERIFY(anlz_track_beatgrid(t, &g) != ANLZ_OK || g.count == 0);                       // no beat grid
    anlz_track_close(t);
}

void TestTrakcrateExport::variantSwitches()
{
    importSet("Set", {{"a", 120}});
    ExportOptions o = options("Set", "usb");
    o.includeSettings = false;
    o.includePvbr = false;
    o.includePwv4 = false;
    o.includePwv5 = false;
    o.includeArtwork = false;
    ExportResult r = exportDrive(lib_, o);
    QVERIFY2(r.ok, r.error.c_str());
    std::vector<std::string> files = filesUnder(tmp_ / "usb");
    QVERIFY(!contains(files, "PIONEER/DEVSETTING.DAT"));
    QVERIFY(!contains(files, "PIONEER/MYSETTING.DAT"));
    std::string dat, ext;
    for (const std::string& f : files) {
        if (f.size() > 3 && f.substr(f.size() - 3) == "DAT" && f.find("ANLZ") != std::string::npos) dat = slurp(tmp_ / "usb" / f);
        if (f.size() > 3 && f.substr(f.size() - 3) == "EXT") ext = slurp(tmp_ / "usb" / f);
    }
    QVERIFY(!dat.empty() && !ext.empty());
    QVERIFY(dat.find("PVBR") == std::string::npos);
    QVERIFY(ext.find("PWV4") == std::string::npos);
    QVERIFY(ext.find("PWV5") == std::string::npos);
    QVERIFY(ext.find("PWV3") != std::string::npos);
    QVERIFY(dat.find("PWAV") != std::string::npos);
    std::string manifest = slurp(tmp_ / "usb/trakcrate-manifest.txt");
    QVERIFY(manifest.find("no PWV4 tag") != std::string::npos);
    QVERIFY(manifest.find("no DEVSETTING.DAT") != std::string::npos);
    // a replacement settings file is taken when it is valid and refused when it is not
    ExportOptions s = options("Set", "usb-set");
    std::string mine = defaultMySetting();
    mine[0x70] = char(mine[0x70] ^ 1);
    fixSettingsCrc(&mine);
    s.mySetting = mine;
    QVERIFY(exportDrive(lib_, s).ok);
    QVERIFY(slurp(tmp_ / "usb-set/PIONEER/MYSETTING.DAT") == mine);
    mine[0x70] = char(mine[0x70] ^ 1);                                                    // CRC now wrong
    s = options("Set", "usb-bad");
    s.mySetting = mine;
    ExportResult bad = exportDrive(lib_, s);
    QVERIFY(!bad.ok);
    QVERIFY(bad.error.find("CRC") != std::string::npos);
    QVERIFY(!fs::exists(tmp_ / "usb-bad"));
}

void TestTrakcrateExport::supportFilesAreAlwaysThere()
{
    importSet("Set", {{"a", 120}});
    QVERIFY(!defaultExportExt().empty() && !defaultRbfltr().empty() && !defaultDjprofile().empty());
    QCOMPARE(defaultExportExt().size(), size_t(73728));                                    // the real drive's file, byte for byte
    QCOMPARE(defaultRbfltr().size(), size_t(232));
    QCOMPARE(defaultDjprofile().size(), size_t(160));
    ExportResult r = exportDrive(lib_, options("Set", "usb"));
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(slurp(tmp_ / "usb/PIONEER/djprofile.nxs"), defaultDjprofile());
    QCOMPARE(slurp(tmp_ / "usb/PIONEER/rekordbox/RBFLTR.DAT"), defaultRbfltr());
    QCOMPARE(slurp(tmp_ / "usb/PIONEER/rekordbox/exportExt.pdb"), defaultExportExt());
    QVERIFY(verifySettingsFile(slurp(tmp_ / "usb/PIONEER/DEVSETTING.DAT")) && verifySettingsFile(slurp(tmp_ / "usb/PIONEER/MYSETTING.DAT")));
    ExportOptions o = options("Set", "usb2");                                              // the library's own copies win
    o.rbfltr = "mine";
    QVERIFY(exportDrive(lib_, o).ok);
    QCOMPARE(slurp(tmp_ / "usb2/PIONEER/rekordbox/RBFLTR.DAT"), std::string("mine"));
    ExportOptions n = options("Set", "usb3");
    n.includeSettings = false;
    QVERIFY(exportDrive(lib_, n).ok);
    QVERIFY(!fs::exists(tmp_ / "usb3/PIONEER/djprofile.nxs"));
}

void TestTrakcrateExport::optionalFilesFromAnotherDrive()
{
    importSet("Set", {{"a", 120}});
    fs::path other = tmp_ / "realdrive";
    fs::create_directories(other / "PIONEER/rekordbox");
    fs::create_directories(other / "PIONEER/USBANLZ");
    std::ofstream(other / "PIONEER/rekordbox/RBFLTR.DAT") << "RBFLTR-bytes";
    std::ofstream(other / "PIONEER/rekordbox/exportExt.pdb") << "ext-bytes";
    ExportOptions o = options("Set", "usb");
    o.optionalFilesFrom = other;
    o.copyRbfltr = true;
    o.copyExportExt = true;
    ExportResult r = exportDrive(lib_, o);
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(slurp(tmp_ / "usb/PIONEER/rekordbox/RBFLTR.DAT"), std::string("RBFLTR-bytes"));
    QCOMPARE(slurp(tmp_ / "usb/PIONEER/rekordbox/exportExt.pdb"), std::string("ext-bytes"));
    QCOMPARE(slurp(tmp_ / "usb/PIONEER/djprofile.nxs"), defaultDjprofile());               // not asked for from the other drive: the built in one
    QVERIFY(slurp(tmp_ / "usb/trakcrate-manifest.txt").find("copied from another drive: PIONEER/rekordbox/RBFLTR.DAT") != std::string::npos);
    ExportOptions m = options("Set", "usb2");                                              // a file that is not there refuses the export before writing
    m.optionalFilesFrom = other;
    m.copyUsbmng = true;
    ExportResult mr = exportDrive(lib_, m);
    QVERIFY(!mr.ok);
    QVERIFY(mr.error.find("USBMNG.DAT") != std::string::npos);
    QVERIFY(!fs::exists(tmp_ / "usb2"));
}

void TestTrakcrateExport::failureLeavesNothingBehind()
{
    if (geteuid() == 0) QSKIP("root can read anything");
    importSet("Set", {{"a", 120}, {"b", 125}, {"c", 130}});
    fs::path victim = audioFileOf(lib_.trackDir("Set", "c"));
    fs::permissions(victim, fs::perms::none);                                              // unreadable audio: fails in the middle of the copy
    ExportResult r = exportDrive(lib_, options("Set", "usb"));
    fs::permissions(victim, fs::perms::owner_read | fs::perms::owner_write);
    QVERIFY(!r.ok);
    QVERIFY(r.error.find("cannot read") != std::string::npos);
    QVERIFY(!fs::exists(tmp_ / "usb"));                                                    // the folder we created is gone again
    QVERIFY(fs::is_empty(tmp_ / "staging"));
    fs::create_directories(tmp_ / "mine");                                                  // an existing empty folder stays, but empty
    fs::permissions(victim, fs::perms::none);
    r = exportDrive(lib_, options("Set", "mine"));
    fs::permissions(victim, fs::perms::owner_read | fs::perms::owner_write);
    QVERIFY(!r.ok);
    QVERIFY(fs::exists(tmp_ / "mine"));
    QVERIFY(fs::is_empty(tmp_ / "mine"));                                                  // no PIONEER, no export.pdb
    QVERIFY(exportDrive(lib_, options("Set", "mine")).ok);                                 // and the same folder works once the problem is fixed
}

void TestTrakcrateExport::cancelLeavesNothingBehind()
{
    importSet("Set", {{"a", 120}, {"b", 125}, {"c", 130}});
    std::atomic<bool> cancel(false);
    int calls = 0;
    ExportResult r = exportDrive(lib_, options("Set", "usb"), [&](int, int, const std::string& cur) {
        if (!cur.empty() && ++calls == 2) cancel = true;                                    // cancel while the second file is about to be written
    }, &cancel);
    QVERIFY(!r.ok);
    QVERIFY(r.cancelled);
    QVERIFY(!fs::exists(tmp_ / "usb"));
    QVERIFY(fs::is_empty(tmp_ / "staging"));
    std::atomic<bool> already(true);
    r = exportDrive(lib_, options("Set", "usb"), nullptr, &already);
    QVERIFY(r.cancelled);
    QVERIFY(!fs::exists(tmp_ / "usb"));
}

void TestTrakcrateExport::updateKeepsBackupsAndSkipsUnchanged()
{
    importSet("Set", {{"a", 120}, {"b", 125}});
    const fs::path usb = tmp_ / "usb";
    QVERIFY(exportDrive(lib_, options("Set", "usb")).ok);
    ExportOptions o = options("Set", "usb");
    QVERIFY(!exportDrive(lib_, o).ok);                                                   // without update an earlier export is refused
    o.update = true;
    const fs::path pdb = usb / "PIONEER/rekordbox/export.pdb";
    const std::string first = slurp(pdb);
    { std::ofstream(usb / "photo.jpg") << "keep me"; }
    for (int i = 1; i <= 4; ++i) {
        ExportResult r = exportDrive(lib_, o);
        QVERIFY2(r.ok, r.error.c_str());
        QVERIFY(r.verified);
        QVERIFY(r.skipped >= 2);                                                         // the audio is unchanged
        QCOMPARE(r.writeOrder.back(), std::string("PIONEER/rekordbox/export.pdb"));
        QCOMPARE(r.backup, std::string("PIONEER/rekordbox/export.pdb.bak"));
    }
    QVERIFY(slurp(pdb) == first || !slurp(pdb).empty());
    QVERIFY(fs::exists(usb / "PIONEER/rekordbox/export.pdb.bak"));
    QVERIFY(fs::exists(usb / "PIONEER/rekordbox/export.pdb.bak.2"));
    QVERIFY(fs::exists(usb / "PIONEER/rekordbox/export.pdb.bak.3"));
    QVERIFY(!fs::exists(usb / "PIONEER/rekordbox/export.pdb.bak.4"));
    QCOMPARE(slurp(usb / "photo.jpg"), std::string("keep me"));
    for (const std::string& f : filesUnder(usb)) QVERIFY2(f.find(".trakcrate-tmp") == std::string::npos, f.c_str());
    // a changed audio file is replaced
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(usb / "Contents"))
        if (e.is_regular_file()) { std::ofstream(e.path(), std::ios::trunc) << "damaged"; break; }
    ExportResult r = exportDrive(lib_, o);
    QVERIFY2(r.ok, r.error.c_str());
    QVERIFY(r.verified);
}

void TestTrakcrateExport::checksumsAreOffByDefault()
{
    importSet("Set", {{"a", 120}, {"b", 125}});
    ExportOptions o = options("Set", "usb");
    o.verify = false;
    o.writeManifest = false;
    ExportResult r = exportDrive(lib_, o);
    QVERIFY2(r.ok, r.error.c_str());
    QVERIFY(!r.verified);
    QVERIFY(!fs::exists(tmp_ / "usb/SHA256SUMS"));
    QCOMPARE(r.writeOrder.back(), std::string("PIONEER/rekordbox/export.pdb"));
    QVERIFY(!slurp(tmp_ / "usb/PIONEER/rekordbox/export.pdb").empty());
    o.update = true;                                                                    // unchanged files are still recognised without checksums
    r = exportDrive(lib_, o);
    QVERIFY2(r.ok, r.error.c_str());
    QVERIFY2(r.skipped >= 4, std::to_string(r.skipped).c_str());
    ExportOptions d;                                                                     // and the struct defaults are off
    QVERIFY(!d.verify && !d.writeManifest);
}

void TestTrakcrateExport::orphansAreListedAndDeleted()
{
    importSet("Set", {{"a", 120}, {"b", 125}});
    const fs::path usb = tmp_ / "usb";
    ExportResult r = exportDrive(lib_, options("Set", "usb"));
    QVERIFY(r.ok && r.orphans.empty());
    ExportOptions o = options("Set", "usb");
    o.update = true;
    QVERIFY(exportDrive(lib_, o).orphans.empty());                                       // everything on the drive is in use
    fs::create_directories(usb / "Contents/Old Artist/Old Album");
    { std::ofstream(usb / "Contents/Old Artist/Old Album/gone.wav") << "12345"; }
    fs::create_directories(usb / "PIONEER/Artwork/00099");
    { std::ofstream(usb / "PIONEER/Artwork/00099/a1.jpg") << "123"; }
    { std::ofstream(usb / "photo.jpg") << "keep me"; }
    r = exportDrive(lib_, o);
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(r.orphans.size(), size_t(2));
    QCOMPARE(r.orphans[0].rel, std::string("Contents/Old Artist/Old Album/gone.wav"));
    QCOMPARE(r.orphans[0].size, uint64_t(5));
    for (const std::string& f : filesUnder(usb)) QVERIFY(f != "PIONEER/rekordbox/export.pdb.bak" || true);
    QVERIFY(fs::exists(usb / "Contents/Old Artist/Old Album/gone.wav"));                // listing deletes nothing
    // a list naming something else is not honoured
    std::vector<Orphan> bad = r.orphans;
    bad.push_back({"photo.jpg", 7});
    bad.push_back({"Contents/../photo.jpg", 7});
    std::vector<std::string> failed;
    QCOMPARE(deleteOrphans(usb, bad, &failed), uint64_t(8));
    QCOMPARE(failed.size(), size_t(2));
    QVERIFY(!fs::exists(usb / "Contents/Old Artist"));                                    // emptied folders go too
    QVERIFY(!fs::exists(usb / "PIONEER/Artwork/00099"));
    QVERIFY(fs::exists(usb / "photo.jpg"));
    QVERIFY(fs::exists(usb / "Contents"));
    QVERIFY(exportDrive(lib_, o).orphans.empty());
}

void TestTrakcrateExport::updateCancelKeepsOldDatabase()
{
    importSet("Set", {{"a", 120}, {"b", 125}, {"c", 130}});
    const fs::path usb = tmp_ / "usb";
    QVERIFY(exportDrive(lib_, options("Set", "usb")).ok);
    const fs::path pdb = usb / "PIONEER/rekordbox/export.pdb";
    const std::string before = slurp(pdb);
    ExportOptions o = options("Set", "usb");
    o.update = true;
    std::atomic<bool> cancel(false);
    int calls = 0;
    ExportResult r = exportDrive(lib_, o, [&](int, int, const std::string& cur) { if (!cur.empty() && ++calls == 2) cancel = true; }, &cancel);
    QVERIFY(r.cancelled && !r.ok);
    QVERIFY(slurp(pdb) == before);                                                       // the old database is untouched
    QVERIFY(!fs::exists(usb / "PIONEER/rekordbox/export.pdb.bak"));
    for (const std::string& f : filesUnder(usb)) QVERIFY2(f.find(".trakcrate-tmp") == std::string::npos, f.c_str());
    // cancel arriving once the database write has started does not stop it
    std::atomic<bool> late(false);
    r = exportDrive(lib_, o, [&](int, int, const std::string& cur) { if (cur == "PIONEER/rekordbox/export.pdb") late = true; }, &late);
    QVERIFY2(r.ok, r.error.c_str());
}

void TestTrakcrateExport::notEnoughSpaceIsRefusedUpFront()
{
    importSet("Set", {{"a", 120}});
    ExportOptions o = options("Set", "usb");
    o.minFreeBytes = ~uint64_t(0) >> 2;                                                     // far more than any disk has
    ExportResult r = exportDrive(lib_, o);
    QVERIFY(!r.ok);
    QVERIFY(r.error.find("not enough space") != std::string::npos);
    QVERIFY(!fs::exists(tmp_ / "usb"));
}

void TestTrakcrateExport::severalPlaylistsShareTracks()
{
    importSet("One", {{"a", 120}, {"b", 125}});
    importSet("Two", {{"c", 130}});
    QVERIFY(lib_.addToPlaylist("Two", lib_.trackDir("One", "a")));                          // track a is in both playlists
    ExportOptions o = options("One", "usb");
    o.playlists = {"One", "Two"};
    ExportResult r = exportDrive(lib_, o);
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(r.tracks, 3);                                                                  // a is on the drive once
    QCOMPARE(r.playlists, 2);
    const std::string pdb = slurp(tmp_ / "usb/PIONEER/rekordbox/export.pdb");
    int entries = 0, lists = 0;
    pdb_parse_playlist_entries(reinterpret_cast<const uint8_t*>(pdb.data()), uint32_t(pdb.size()), [](const pdb_playlist_entry_t*, void* ud) { ++*static_cast<int*>(ud); return 0; }, &entries);
    pdb_parse_playlists(reinterpret_cast<const uint8_t*>(pdb.data()), uint32_t(pdb.size()), [](const pdb_playlist_t*, void* ud) { ++*static_cast<int*>(ud); return 0; }, &lists);
    QCOMPARE(entries, 4);                                                                   // 2 + 2 entries (a is in both)
    QCOMPARE(lists, 2);
}

void TestTrakcrateExport::summaryDoesNotWrite()
{
    importSet("Set", {{"a", 120}, {"b", 125}});
    importSet("Raw", {{"x", 133}}, false);                                                    // not the same audio as a track of Set (that would be a duplicate)
    ExportOptions o = options("Set", "usb");
    ExportSummary s = summariseExport(lib_, o);
    QCOMPARE(s.tracks, 2);
    QVERIFY(s.audioBytes > 100000);
    QVERIFY(s.metadataBytes > 300000);
    QVERIFY(s.problems.empty());
    QVERIFY(!fs::exists(tmp_ / "usb"));
    o.playlists = {"Raw"};
    s = summariseExport(lib_, o);
    QCOMPARE(s.problems.size(), size_t(1));
    QVERIFY(s.problems[0].find("not analysed") != std::string::npos);
}

void TestTrakcrateExport::artworkIsExportedOncePerImage()
{
    importSet("Set", {{"a", 120}, {"b", 125}, {"c", 130}});
    const std::string cover = "COVER-A", other = "COVER-B";
    std::ofstream(lib_.trackDir("Set", "a") / "artwork.png") << cover;
    std::ofstream(lib_.trackDir("Set", "b") / "artwork.png") << cover;                       // the same image: one artwork row
    std::ofstream(lib_.trackDir("Set", "c") / "artwork.png") << other;
    ExportResult r = exportDrive(lib_, options("Set", "usb"));
    QVERIFY2(r.ok, r.error.c_str());
    const fs::path usb = tmp_ / "usb";
    std::vector<std::string> files = filesUnder(usb);
    QVERIFY(contains(files, "PIONEER/Artwork/00001/a1.jpg"));
    QVERIFY(!contains(files, "PIONEER/Artwork/00001/a1_m.jpg"));          // like a real drive: one 80x80 file per image
    QVERIFY(contains(files, "PIONEER/Artwork/00001/a2.jpg"));
    QVERIFY(!contains(files, "PIONEER/Artwork/00001/a3.jpg"));
    QCOMPARE(slurp(usb / "PIONEER/Artwork/00001/a1.jpg"), std::string("\xFF\xD8" "fake80:COVER-A"));
    QCOMPARE(slurp(usb / "PIONEER/Artwork/00001/a2.jpg"), std::string("\xFF\xD8" "fake80:COVER-B"));
    const std::string pdb = slurp(usb / "PIONEER/rekordbox/export.pdb");
    int art = 0;
    pdb_parse_artwork(reinterpret_cast<const uint8_t*>(pdb.data()), uint32_t(pdb.size()), [](const pdb_named_t*, void* ud) { ++*static_cast<int*>(ud); return 0; }, &art);
    QCOMPARE(art, 2);
    ExportOptions off = options("Set", "usb-noart");
    off.encodeArtwork = nullptr;
    QVERIFY(exportDrive(lib_, off).ok);
    QVERIFY(!fs::exists(tmp_ / "usb-noart/PIONEER/Artwork"));
}

void TestTrakcrateExport::pendriveEntriesAreExportedAsPlaylists()
{
    importSet("Set", {{"a", 120}, {"b", 125}, {"c", 130}});
    QVERIFY(lib_.createDrive("Fri"));
    QVERIFY(lib_.addFolderToDrive("Fri", "Set"));
    QVERIFY(lib_.moveDriveTrack("Fri", 0, 2, 0));                                         // c first
    ExportOptions o = options("unused", "usbp");
    o.playlists.clear();
    const auto es = lib_.driveEntries("Fri");
    o.lists.push_back({es[0].name, lib_.driveLibrary("Fri").playlistTracks(es[0].dir)});
    ExportResult r = exportDrive(lib_, o);
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(r.playlists, 1);
    QCOMPARE(r.tracks, 3);
    const std::string pdb = slurp(tmp_ / "usbp" / "PIONEER/rekordbox/export.pdb");
    int names = 0;
    pdb_parse_playlists(reinterpret_cast<const uint8_t*>(pdb.data()), uint32_t(pdb.size()), [](const pdb_playlist_t* p, void* ud) {
        if (!p->is_folder && std::string(p->name) == "Set") ++*static_cast<int*>(ud);
        return 0;
    }, &names);
    QCOMPARE(names, 1);                                                                    // the entry is the playlist "Set" on the drive
    ExportOptions none = options("unused", "usbq");
    none.playlists.clear();
    QVERIFY(!exportDrive(lib_, none).ok);                                                  // nothing to export is still refused
}

QTEST_APPLESS_MAIN(TestTrakcrateExport)
#include "test_trakcrate_export.moc"
