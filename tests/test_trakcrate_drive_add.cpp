#include <QtTest/QtTest>

#include <cdj/pdb.h>
#include <sys/stat.h>
#include <unistd.h>

#include "trakcrate_wav.h"
#include "trakcrate/exporter.h"
#include "trakcrate/importer.h"
#include "trakcrate/pdb_append.h"

#include <fstream>
#include <map>
#include <regex>
#include <set>

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

// every regular file below root: relative path -> size and modification time
std::map<std::string, std::string> snapshot(const fs::path& root)
{
    std::map<std::string, std::string> m;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(root, ec))
        if (e.is_regular_file(ec))
            m[e.path().lexically_relative(root).generic_string()] = std::to_string(fs::file_size(e.path(), ec)) + "/" + std::to_string(fs::last_write_time(e.path(), ec).time_since_epoch().count());
    return m;
}

std::map<std::string, std::string> contents(const fs::path& root)      // relative path -> the bytes
{
    std::map<std::string, std::string> m;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(root, ec))
        if (e.is_regular_file(ec)) m[e.path().lexically_relative(root).generic_string()] = slurp(e.path());
    return m;
}

} // namespace

class TestTrakcrateDriveAdd : public QObject
{
    Q_OBJECT

private:
    fs::path tmp_;
    Library lib_;
    std::string tpl_;

    void importSet(const std::string& folder, std::initializer_list<std::pair<const char*, double>> tracks, bool analyse = true, const char* artwork = nullptr)
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
        if (artwork)
            for (const std::string& t : lib_.listTracks(folder)) { std::ofstream(lib_.trackDir(folder, t) / "artwork.png", std::ios::binary) << artwork; }
    }

    ExportOptions exportOptions(const std::string& playlist, const std::string& dest)
    {
        ExportOptions o;
        o.playlists = {playlist};
        o.dest = tmp_ / dest;
        o.templatePdb = tpl_;
        o.stagingBase = tmp_ / "staging";
        o.encodeArtwork = fakeEncoder;
        fs::create_directories(o.stagingBase);
        return o;
    }

    AddOptions addOptions(const std::string& playlist, const fs::path& usb)
    {
        AddOptions a;
        a.base.playlists = {playlist};
        a.base.dest = usb;
        a.base.templatePdb = tpl_;
        a.base.stagingBase = tmp_ / "staging";
        a.base.encodeArtwork = fakeEncoder;
        a.backupDir = tmp_ / "backups";
        fs::create_directories(a.base.stagingBase);
        return a;
    }

    // a stick made by a real export of "A" (two tracks)
    fs::path makeStick(const char* name = "usb", const char* artwork = nullptr)
    {
        if (!lib_.hasPlaylist("A")) importSet("A", {{"a1", 120}, {"a2", 128}}, true, artwork);
        ExportResult r = exportDrive(lib_, exportOptions("A", name));
        if (!r.ok) QTest::qFail(r.error.c_str(), __FILE__, __LINE__);
        return tmp_ / name;
    }

    int tracksOf(const fs::path& usb)
    {
        const std::string b = slurp(usb / "PIONEER/rekordbox/export.pdb");
        int n = 0;
        pdb_parse(reinterpret_cast<const uint8_t*>(b.data()), uint32_t(b.size()), [](const pdb_track_t*, void* ud) { ++*static_cast<int*>(ud); return 0; }, &n);
        return n;
    }

    bool loads(const fs::path& usb, DriveDb* db = nullptr)
    {
        DriveDb local;
        std::vector<std::string> problems;
        return loadDriveDb(slurp(usb / "PIONEER/rekordbox/export.pdb"), db ? db : &local, &problems);
    }

private slots:
    void init()
    {
        tmp_ = makeTempDir("trakcrate-add");
        QVERIFY(!tmp_.empty());
        QVERIFY(Library::create(tmp_ / "lib") && Library::open(tmp_ / "lib", &lib_));
        tpl_ = slurp(std::string(TRAKCRATE_FIXTURES) + "/pdb/export7.pdb");
        QVERIFY(!tpl_.empty());
    }
    void cleanup()
    {
        std::error_code ec;
        fs::permissions(tmp_ / "usb" / "Contents" / "UnknownArtist" / "UnknownAlbum", fs::perms::owner_all, ec);
        fs::remove_all(tmp_, ec);
    }

    void addsANewPlaylist();
    void tracksAlreadyOnTheStickAreReferenced();
    void aPathTakenByAnotherFileGetsANewName();
    void refusesWithoutChangingAnything();
    void unanalysedTracksNeedPermission();
    void cancelLeavesTheStickAsItWas();
    void ejectAtEachStepNeverLeavesAnUnusableStick();
    void aFailingCopyIsRolledBack();
    void artworkNumberingContinues();
    void repeatedAdditionsAndBackups();
};

void TestTrakcrateDriveAdd::addsANewPlaylist()
{
    const fs::path usb = makeStick();
    importSet("B", {{"b1", 100}, {"b2", 110}, {"b3", 140}});
    const auto libBefore = snapshot(tmp_ / "lib");
    const auto stickBefore = contents(usb);
    const std::string oldPdb = stickBefore.at("PIONEER/rekordbox/export.pdb");
    AddResult r = addToDrive(lib_, addOptions("B", usb));
    QVERIFY2(r.ok, r.error.c_str());
    QVERIFY(r.switched);
    QCOMPARE(r.newTracks, 3);
    QCOMPARE(r.existingTracks, 0);
    QCOMPARE(r.filesCopied, 3 + 6);                                           // 3 audio files, DAT + EXT for each
    QCOMPARE(r.created.size(), size_t(9));
    QVERIFY(r.orphans.empty());
    QCOMPARE(tracksOf(usb), 5);
    DriveDb db;
    QVERIFY(loads(usb, &db));
    QVERIFY(db.playlistByName.count("a") && db.playlistByName.count("b"));
    // the old database: copied outside the stick, and kept as .bak
    QVERIFY(fs::path(r.backupCopy).filename().string().rfind("rekordbox-edit-backup-export.", 0) == 0);
    QVERIFY(slurp(r.backupCopy) == oldPdb);
    QVERIFY(slurp(usb / "PIONEER/rekordbox/export.pdb.bak") == oldPdb);
    QCOMPARE(r.backupOnDrive, std::string("PIONEER/rekordbox/export.pdb.bak"));
    QVERIFY(slurp(usb / "PIONEER/rekordbox/export.pdb") != oldPdb);
    QVERIFY(!fs::exists(usb / "PIONEER/rekordbox/export.pdb.new"));
    // everything the database names is on the stick
    for (const DriveTrack& t : db.tracks) {
        QVERIFY2(fs::is_regular_file(usb / t.filePath.substr(1)), t.filePath.c_str());
        QVERIFY2(fs::is_regular_file(usb / t.analyzePath.substr(1)), t.analyzePath.c_str());
        QVERIFY2(fs::is_regular_file(usb / (t.analyzePath.substr(1, t.analyzePath.size() - 4) + "EXT")), t.analyzePath.c_str());
    }
    // nothing that was there changed, only files were added; the settings files and USBMNG.DAT are byte identical
    const auto stickAfter = contents(usb);
    for (const auto& kv : stickBefore)
        if (kv.first != "PIONEER/rekordbox/export.pdb") QVERIFY2(stickAfter.at(kv.first) == kv.second, kv.first.c_str());
    for (const char* f : {"PIONEER/DEVSETTING.DAT", "PIONEER/MYSETTING.DAT", "PIONEER/djprofile.nxs", "PIONEER/rekordbox/RBFLTR.DAT", "PIONEER/rekordbox/exportExt.pdb", "PIONEER/USBANLZ/USBMNG.DAT"})
        QVERIFY2(stickAfter.at(f) == stickBefore.at(f), f);
    // the trakcrate library is not touched
    QVERIFY(snapshot(tmp_ / "lib") == libBefore);
    QVERIFY(!fs::exists(tmp_ / "staging" / "x"));
    QVERIFY(stagingReserved() == false);
}

void TestTrakcrateDriveAdd::tracksAlreadyOnTheStickAreReferenced()
{
    const fs::path usb = makeStick();
    importSet("B", {{"b1", 100}});
    // "Again" holds the very tracks of A: nothing but a playlist row and its entries is added, and no file is written
    QVERIFY(lib_.createPlaylist("Again"));
    std::vector<fs::path> dirs;
    for (const std::string& t : lib_.listTracks("A")) dirs.push_back(lib_.trackDir("A", t));
    QVERIFY(lib_.addTracksToPlaylist("Again", dirs));
    const auto before = contents(usb);
    AddResult r = addToDrive(lib_, addOptions("Again", usb));
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(r.newTracks, 0);
    QCOMPARE(r.existingTracks, 2);
    QCOMPARE(r.filesCopied, 0);
    QVERIFY(r.created.empty());
    QCOMPARE(tracksOf(usb), 2);
    DriveDb db;
    QVERIFY(loads(usb, &db));
    QVERIFY(db.playlistByName.count("again"));
    const auto after = contents(usb);
    for (const auto& kv : after)
        if (before.count(kv.first)) { if (kv.first != "PIONEER/rekordbox/export.pdb") QVERIFY2(kv.second == before.at(kv.first), kv.first.c_str()); }
        else QVERIFY2(kv.first == "PIONEER/rekordbox/export.pdb.bak", kv.first.c_str());
    // a mixture: a1 is there, b1 is new
    QVERIFY(lib_.createPlaylist("Mix"));
    QVERIFY(lib_.addTracksToPlaylist("Mix", {dirs[0], lib_.trackDir("B", lib_.listTracks("B")[0])}));
    AddResult m = addToDrive(lib_, addOptions("Mix", usb));
    QVERIFY2(m.ok, m.error.c_str());
    QCOMPARE(m.newTracks, 1);
    QCOMPARE(m.existingTracks, 1);
    QCOMPARE(m.filesCopied, 1 + 2);
    QCOMPARE(tracksOf(usb), 3);
}

void TestTrakcrateDriveAdd::aPathTakenByAnotherFileGetsANewName()
{
    const fs::path usb = makeStick();
    importSet("B", {{"b1", 100}});
    const fs::path taken = usb / "Contents/UnknownArtist/UnknownAlbum/b1.wav";
    fs::create_directories(taken.parent_path());
    std::ofstream(taken, std::ios::binary) << "somebody else's file";
    AddResult r = addToDrive(lib_, addOptions("B", usb));
    QVERIFY2(r.ok, r.error.c_str());
    QVERIFY(slurp(taken) == "somebody else's file");
    DriveDb db;
    QVERIFY(loads(usb, &db));
    bool found = false;
    for (const DriveTrack& t : db.tracks) found = found || t.filePath == "/Contents/UnknownArtist/UnknownAlbum/b1 (2).wav";
    QVERIFY(found);
    QVERIFY(fs::is_regular_file(usb / "Contents/UnknownArtist/UnknownAlbum/b1 (2).wav"));
}

void TestTrakcrateDriveAdd::refusesWithoutChangingAnything()
{
    const fs::path usb = makeStick();
    importSet("B", {{"b1", 100}});
    const auto before = contents(usb);
    auto unchanged = [&]() { QVERIFY(contents(usb) == before); QVERIFY(!fs::exists(tmp_ / "backups") || fs::is_empty(tmp_ / "backups")); };
    {   // a playlist with this name is there already (any case)
        importSet("a", {{"x", 150}});
        AddResult r = addToDrive(lib_, addOptions("a", usb));
        QVERIFY(!r.ok);
        QVERIFY2(r.error.find("already has a playlist") != std::string::npos, r.error.c_str());
        unchanged();
    }
    {   // exportLibrary.db
        std::ofstream(usb / "PIONEER/rekordbox/exportLibrary.db") << "x";
        AddResult r = addToDrive(lib_, addOptions("B", usb));
        QVERIFY(!r.ok);
        QVERIFY2(r.error.find("exportLibrary.db") != std::string::npos, r.error.c_str());
        fs::remove(usb / "PIONEER/rekordbox/exportLibrary.db");
    }
    {   // no database
        AddResult r = addToDrive(lib_, addOptions("B", tmp_ / "lib"));
        QVERIFY(!r.ok);
        QVERIFY2(r.error.find("export.pdb") != std::string::npos, r.error.c_str());
    }
    {   // a database that cannot be added to
        const std::string good = slurp(usb / "PIONEER/rekordbox/export.pdb");
        std::string bad = good;
        bad[0x1c + 12] = char(bad[0x1c + 12] + 1);                          // table 0 names the wrong last page
        { std::ofstream f(usb / "PIONEER/rekordbox/export.pdb", std::ios::binary); f << bad; }
        const auto stick = contents(usb);
        AddResult r = addToDrive(lib_, addOptions("B", usb));
        QVERIFY(!r.ok);
        QVERIFY2(r.error.find("cannot be added to") != std::string::npos, r.error.c_str());
        QVERIFY(contents(usb) == stick);
        { std::ofstream f(usb / "PIONEER/rekordbox/export.pdb", std::ios::binary); f << good; }
    }
    {   // a playlist that is not there, and one that is empty
        AddResult r = addToDrive(lib_, addOptions("Nope", usb));
        QVERIFY(!r.ok);
        QVERIFY(lib_.createPlaylist("Empty"));
        QVERIFY(!addToDrive(lib_, addOptions("Empty", usb)).ok);
    }
    QVERIFY(contents(usb) == before);
}

void TestTrakcrateDriveAdd::unanalysedTracksNeedPermission()
{
    const fs::path usb = makeStick();
    importSet("U", {{"u1", 100}}, false);
    const auto before = contents(usb);
    AddResult r = addToDrive(lib_, addOptions("U", usb));
    QVERIFY(!r.ok);
    QVERIFY2(r.error.find("not analysed") != std::string::npos, r.error.c_str());
    QVERIFY(contents(usb) == before);
    AddOptions a = addOptions("U", usb);
    a.base.allowUnanalysed = true;
    AddResult ok = addToDrive(lib_, a);
    QVERIFY2(ok.ok, ok.error.c_str());
    QVERIFY(!ok.warnings.empty());
    QCOMPARE(tracksOf(usb), 3);
}

void TestTrakcrateDriveAdd::cancelLeavesTheStickAsItWas()
{
    const fs::path usb = makeStick();
    importSet("B", {{"b1", 100}, {"b2", 110}, {"b3", 140}});
    const auto before = contents(usb);
    std::atomic<bool> cancel(false);
    int calls = 0;
    AddResult r = addToDrive(lib_, addOptions("B", usb), [&](int, int, const std::string&) { if (++calls == 2) cancel = true; }, &cancel);
    QVERIFY(!r.ok);
    QVERIFY(r.cancelled);
    QVERIFY(!r.switched);
    QVERIFY(contents(usb) == before);                                          // what was copied is taken off again; the old database is untouched
    QVERIFY(r.orphans.empty());
    QVERIFY(stagingReserved() == false);
    // and the same addition afterwards works
    AddResult again = addToDrive(lib_, addOptions("B", usb));
    QVERIFY2(again.ok, again.error.c_str());
}

void TestTrakcrateDriveAdd::ejectAtEachStepNeverLeavesAnUnusableStick()
{
    const fs::path usb = makeStick();
    importSet("B", {{"b1", 100}, {"b2", 110}});
    const std::string oldPdb = slurp(usb / "PIONEER/rekordbox/export.pdb");
    int stopped = 0;
    for (const char* step : {"backup", "planned", "audio", "analysis", "database", "switch", "between-renames", "done"}) {
        const fs::path copy = tmp_ / (std::string("stick-") + step);
        fs::copy(usb, copy, fs::copy_options::recursive);
        AddOptions a = addOptions("B", copy);
        a.stepHook = [&](const char* s) { return std::string(s) != step; };
        AddResult r = addToDrive(lib_, a);
        QVERIFY2(!r.ok || std::string(step) == "done", step);
        if (std::string(step) == "done") continue;                            // "done" is after everything: the run is complete either way
        QVERIFY2(r.error == std::string("stopped at ") + step, (std::string(step) + ": " + r.error).c_str());
        ++stopped;
        if (std::string(step) == "between-renames") {
            // export.pdb is gone for a moment: the new database and the old one are both on the stick, and one rename repairs it
            QVERIFY(!fs::exists(copy / "PIONEER/rekordbox/export.pdb"));
            QVERIFY(slurp(copy / "PIONEER/rekordbox/export.pdb.bak") == oldPdb);
            fs::rename(copy / "PIONEER/rekordbox/export.pdb.new", copy / "PIONEER/rekordbox/export.pdb");
            QCOMPARE(tracksOf(copy), 4);
        } else {
            QVERIFY2(slurp(copy / "PIONEER/rekordbox/export.pdb") == oldPdb, step);        // the old database is what the decks still see
            QVERIFY2(loads(copy), step);
            QCOMPARE(tracksOf(copy), 2);
            // what was added is listed, and everything else is as it was
            auto now = contents(copy);
            auto was = contents(usb);
            std::set<std::string> extra(r.orphans.begin(), r.orphans.end());
            for (const auto& kv : now)
                if (!was.count(kv.first)) QVERIFY2(extra.count(kv.first) || kv.first.find(".new") != std::string::npos || kv.first.find(".trakcrate-tmp") != std::string::npos, (std::string(step) + ": " + kv.first).c_str());
            for (const auto& kv : was) QVERIFY2(now.at(kv.first) == kv.second, kv.first.c_str());
        }
        // adding again to the interrupted stick works
        { std::error_code e2; fs::remove_all(tmp_ / "backups2", e2); }
        AddOptions again = addOptions("B", copy);
        again.backupDir = tmp_ / "backups2";
        AddResult second = addToDrive(lib_, again);
        if (std::string(step) == "between-renames") {                         // repaired by hand it already has B: a second addition is refused
            QVERIFY(!second.ok);
            QVERIFY(second.error.find("already has a playlist") != std::string::npos);
        } else {
            QVERIFY2(second.ok, (std::string(step) + ": " + second.error).c_str());
        }
        QCOMPARE(tracksOf(copy), 4);
    }
    QCOMPARE(stopped, 7);
}

void TestTrakcrateDriveAdd::aFailingCopyIsRolledBack()
{
    if (::geteuid() == 0) QSKIP("permissions do not stop root");
    const fs::path usb = makeStick();
    importSet("B", {{"b1", 100}});
    const auto before = contents(usb);
    const fs::path album = usb / "Contents/UnknownArtist/UnknownAlbum";                       // where the new audio would go
    fs::permissions(album, fs::perms::owner_read | fs::perms::owner_exec);                     // no new files there
    AddResult r = addToDrive(lib_, addOptions("B", usb));
    fs::permissions(album, fs::perms::owner_all);
    QVERIFY(!r.ok);
    QVERIFY(!r.switched);
    QVERIFY(!r.error.empty());
    QVERIFY(contents(usb) == before);
    QVERIFY(stagingReserved() == false);
}

void TestTrakcrateDriveAdd::artworkNumberingContinues()
{
    const fs::path usb = makeStick("usb", "first picture");
    importSet("B", {{"b1", 100}, {"b2", 110}}, true, "second picture");
    DriveDb before;
    QVERIFY(loads(usb, &before));
    QCOMPARE(before.artwork.size(), size_t(1));
    AddResult r = addToDrive(lib_, addOptions("B", usb));
    QVERIFY2(r.ok, r.error.c_str());
    DriveDb db;
    QVERIFY(loads(usb, &db));
    QCOMPARE(db.artwork.size(), size_t(2));
    QVERIFY(db.artwork.count("/PIONEER/Artwork/00001/a2.jpg"));
    QVERIFY(fs::is_regular_file(usb / "PIONEER/Artwork/00001/a2.jpg"));
    QVERIFY(slurp(usb / "PIONEER/Artwork/00001/a1.jpg").find("first picture") != std::string::npos);        // the old picture is untouched
    QVERIFY(slurp(usb / "PIONEER/Artwork/00001/a2.jpg").find("second picture") != std::string::npos);
}

void TestTrakcrateDriveAdd::repeatedAdditionsAndBackups()
{
    const fs::path usb = makeStick();
    importSet("B", {{"b1", 100}});
    importSet("C", {{"c1", 130}});
    importSet("D", {{"d1", 135}});
    std::set<std::string> backups;
    for (const char* p : {"B", "C", "D"}) {
        AddResult r = addToDrive(lib_, addOptions(p, usb));
        QVERIFY2(r.ok, r.error.c_str());
        backups.insert(r.backupCopy);                                           // three additions in one second still get three different files
    }
    QCOMPARE(backups.size(), size_t(3));
    int n = 0;
    for (const auto& e : fs::directory_iterator(tmp_ / "backups")) { QVERIFY(e.path().filename().string().rfind("rekordbox-edit-backup-export.", 0) == 0); ++n; }
    QCOMPARE(n, 3);
    DriveDb db;
    QVERIFY(loads(usb, &db));
    QCOMPARE(db.playlistByName.size(), size_t(4));
    QCOMPARE(tracksOf(usb), 5);
    // the three newest old databases are kept as .bak, .bak.2, .bak.3
    QVERIFY(fs::exists(usb / "PIONEER/rekordbox/export.pdb.bak"));
    QVERIFY(fs::exists(usb / "PIONEER/rekordbox/export.pdb.bak.2"));
    QVERIFY(fs::exists(usb / "PIONEER/rekordbox/export.pdb.bak.3"));
}

QTEST_MAIN(TestTrakcrateDriveAdd)
#include "test_trakcrate_drive_add.moc"
