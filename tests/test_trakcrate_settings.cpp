#include <QtTest/QtTest>

#include "trakcrate/fsutil.h"
#include "trakcrate/library.h"
#include "trakcrate/settings_files.h"
#include "trakcrate/settings_sets.h"
#include "trakcrate/settings_view.h"

#include <algorithm>

using namespace trakcrate;

static std::string slurp(const fs::path& p)
{
    std::string s;
    readFile(p, &s);
    return s;
}

class TestTrakcrateSettings : public QObject
{
    Q_OBJECT

private:
    fs::path tmp_;

    Library newLibrary()
    {
        Library lib;
        std::string err;
        fs::path root = tmp_ / "lib";
        if (!Library::create(root, &err) || !Library::open(root, &lib, &err)) QTest::qFail(err.c_str(), __FILE__, __LINE__);
        return lib;
    }

    // A rekordbox drive holding the given files (on disk names), with a recognisable djprofile.
    fs::path makeDrive(const std::vector<std::string>& which, const std::string& tag = "A")
    {
        fs::path d = tmp_ / ("drive" + tag);
        std::error_code ec;
        fs::create_directories(d / "PIONEER/rekordbox", ec);
        for (const std::string& n : which) {
            std::string bytes;
            if (n == "DEVSETTING.DAT") bytes = defaultDevSetting();
            else if (n == "MYSETTING.DAT") bytes = defaultMySetting();
            else if (n == "djprofile.nxs") bytes = "profile-" + tag;
            else if (n == "RBFLTR.DAT") bytes = defaultRbfltr();
            else bytes = defaultExportExt();
            const auto& disk = settingsFileNames();
            size_t i = std::find(disk.begin(), disk.end(), n) - disk.begin();
            atomicWrite(d / settingsDriveFileNames()[i], bytes);
        }
        return d;
    }

private slots:
    void init() { tmp_ = makeTempDir("trakcrate-settings-test"); QVERIFY(!tmp_.empty()); }
    void cleanup() { std::error_code ec; fs::remove_all(tmp_, ec); }

    void createListDefault()
    {
        Library lib = newLibrary();
        QVERIFY(listSettingsSets(lib).empty());
        QCOMPARE(defaultSettingsSet(lib), std::string());
        std::string err;
        QVERIFY2(createSettingsSet(lib, "Home", "Paul", &err), err.c_str());
        QVERIFY2(createSettingsSet(lib, "Club", "", &err), err.c_str());
        std::vector<SettingsSet> sets = listSettingsSets(lib);
        QCOMPARE(sets.size(), size_t(2));
        QCOMPARE(sets[0].name, std::string("Club"));
        QCOMPARE(sets[1].name, std::string("Home"));
        QCOMPARE(defaultSettingsSet(lib), std::string("Home"));          // the first set created is the default
    }

    void firstCreatedIsDefaultAndDefaultCanChange()
    {
        Library lib = newLibrary();
        QVERIFY(createSettingsSet(lib, "Zed", "x"));
        QVERIFY(createSettingsSet(lib, "Abe", "y"));
        QCOMPARE(defaultSettingsSet(lib), std::string("Zed"));
        QVERIFY(listSettingsSets(lib)[1].isDefault);
        QVERIFY(setDefaultSettingsSet(lib, "Abe"));
        QCOMPARE(defaultSettingsSet(lib), std::string("Abe"));
        QVERIFY(!setDefaultSettingsSet(lib, "Nope"));
        // a default.txt naming a set that is gone is ignored
        QVERIFY(deleteSettingsSet(lib, "Abe"));
        QCOMPARE(defaultSettingsSet(lib), std::string("Zed"));
    }

    void createHoldsAllFiles()
    {
        Library lib = newLibrary();
        QVERIFY(createSettingsSet(lib, "Home", "Paul"));
        for (const std::string& f : settingsFileNames()) QVERIFY2(fs::is_regular_file(settingsSetDir(lib, "Home") / f), f.c_str());
        QVERIFY(verifySettingsFile(slurp(settingsSetDir(lib, "Home") / "DEVSETTING.DAT")));
        QCOMPARE(listSettingsSets(lib)[0].source, std::string("built-in"));
        QVERIFY(!createSettingsSet(lib, "Home", ""));                       // exists
    }

    void djprofileParseAndEdit()
    {
        DjProfile p;
        std::string err;
        QVERIFY2(parseDjprofile(defaultDjprofile(), &p, &err), err.c_str());
        QCOMPARE(p.userName, std::string("teknopaul teknopaul"));
        QCOMPARE(p.timestampMs, uint64_t(1700243433449ull));
        QCOMPARE(p.unknownAt1c, uint32_t(0xa23f2b74));
        std::string b = defaultDjprofile();
        QVERIFY2(setDjprofileUserName(&b, "Ana Müller", &err), err.c_str());
        QCOMPARE(b.size(), kDjprofileSize);
        QVERIFY(parseDjprofile(b, &p));
        QCOMPARE(p.userName, std::string("Ana Müller"));
        QCOMPARE(p.unknownAt1c, uint32_t(0xa23f2b74));                       // untouched
        QCOMPARE(b.substr(0, 0x20), defaultDjprofile().substr(0, 0x20));       // header untouched
        QCOMPARE(b.find_first_not_of('\0', kDjprofileNameAt + 11), std::string::npos);   // zero padded after the name
        // a shorter name leaves no remnant of the old one, and a name can change back
        QVERIFY(setDjprofileUserName(&b, "Al", &err));
        QVERIFY(parseDjprofile(b, &p));
        QCOMPARE(p.userName, std::string("Al"));
        QVERIFY(setDjprofileUserName(&b, "teknopaul teknopaul"));
        QCOMPARE(b, defaultDjprofile());
    }

    void djprofileRejectsBadNames()
    {
        std::string b = defaultDjprofile(), err;
        QVERIFY(!setDjprofileUserName(&b, "", &err));
        QVERIFY(!setDjprofileUserName(&b, std::string(kDjprofileNameMax + 1, 'a'), &err));
        QVERIFY(setDjprofileUserName(&b, std::string(kDjprofileNameMax, 'a'), &err));
        QVERIFY(!setDjprofileUserName(&b, "tab\there", &err));
        QVERIFY(!setDjprofileUserName(&b, std::string("nul\0x", 5), &err));
        QVERIFY(!setDjprofileUserName(&b, std::string("bad\xc3"), &err));       // cut UTF-8
        QVERIFY(!setDjprofileUserName(&b, std::string("bad\xff\xfe"), &err));
        std::string small = "short";
        QVERIFY(!setDjprofileUserName(&small, "x", &err));
        DjProfile p;
        QVERIFY(!parseDjprofile(small, &p));
        QCOMPARE(defaultDjprofile().size(), kDjprofileSize);
    }

    void createSetStampsTheUserName()
    {
        Library lib = newLibrary();
        std::string err;
        QVERIFY2(createSettingsSet(lib, "Ana", "Ana Lopez", &err), err.c_str());
        DjProfile p;
        QVERIFY(parseDjprofile(slurp(settingsSetDir(lib, "Ana") / "djprofile.nxs"), &p));
        QCOMPARE(p.userName, std::string("Ana Lopez"));
        QCOMPARE(listSettingsSets(lib)[0].userName, std::string("Ana Lopez"));
        QVERIFY(!createSettingsSet(lib, "Bad", std::string(100, 'x'), &err));      // too long: refused, nothing created
        QVERIFY(!hasSettingsSet(lib, "Bad"));
        // no name given: the stock profile, and the cache says what is really in the file
        QVERIFY(createSettingsSet(lib, "Stock", ""));
        QCOMPARE(slurp(settingsSetDir(lib, "Stock") / "djprofile.nxs"), defaultDjprofile());
    }

    static std::string valueOf(const std::vector<SettingsRow>& rows, const std::string& field)
    {
        for (const SettingsRow& r : rows) if (r.field == field) return r.value;
        return "<none>";
    }

    void describeShippedFiles()
    {
        auto dev = describeSettingsFile(SettingsKind::DevSetting, defaultDevSetting());
        QCOMPARE(valueOf(dev, "file check"), std::string("ok"));
        QCOMPARE(valueOf(dev, "brand"), std::string("PIONEER DJ"));
        QCOMPARE(valueOf(dev, "program"), std::string("rekordbox"));
        QCOMPARE(valueOf(dev, "version"), std::string("4.5.0"));
        QCOMPARE(valueOf(dev, "crc"), std::string("0x3bba"));
        QCOMPARE(valueOf(dev, "payload 0x04"), std::string("0x01"));
        auto my = describeSettingsFile(SettingsKind::MySetting, defaultMySetting());
        QCOMPARE(valueOf(my, "crc"), std::string("0x2181"));
        QCOMPARE(valueOf(my, "block id"), std::string("2"));
        bool coded = false;
        for (const SettingsRow& r : my) if (r.field == "payload 0x04") { coded = r.note.find("option code") != std::string::npos; QVERIFY(!r.known); }
        QVERIFY(coded);
        auto prof = describeSettingsFile(SettingsKind::Djprofile, defaultDjprofile());
        QCOMPARE(valueOf(prof, "name on the players"), std::string("teknopaul teknopaul"));
        QCOMPARE(valueOf(prof, "date"), std::string("2023-11-17 17:50:33 UTC"));
        QCOMPARE(valueOf(prof, "bytes 0x1c-0x1f"), std::string("a2 3f 2b 74"));
        auto fl = describeSettingsFile(SettingsKind::Rbfltr, defaultRbfltr());
        QCOMPARE(valueOf(fl, "player"), std::string("XDJ-700"));
        QCOMPARE(valueOf(fl, "firmware"), std::string("1.13"));
        QCOMPARE(valueOf(fl, "records"), std::string("4"));
        QCOMPARE(valueOf(fl, "record 4").empty(), false);
        QCOMPARE(valueOf(fl, "record 5"), std::string("<none>"));
        auto pdb = describeSettingsFile(SettingsKind::ExportExt, defaultExportExt());
        QCOMPARE(valueOf(pdb, "page size"), std::string("4096"));
        QCOMPARE(valueOf(pdb, "tables"), std::string("9"));
    }

    void describeNeverCrashesOnGarbage()
    {
        for (SettingsKind k : {SettingsKind::DevSetting, SettingsKind::MySetting, SettingsKind::Djprofile, SettingsKind::Rbfltr, SettingsKind::ExportExt})
            for (const std::string& junk : {std::string(), std::string("x"), std::string(200, '\xff'), std::string(300, '\0')})
                QVERIFY(!describeSettingsFile(k, junk).empty());
        std::string bad = defaultMySetting();
        bad[0x70] ^= 1;
        QCOMPARE(valueOf(describeSettingsFile(SettingsKind::MySetting, bad), "file check"), std::string("damaged"));
    }

    void diffFindsTheChangedByte()
    {
        QVERIFY(diffSettingsFiles(SettingsKind::MySetting, defaultMySetting(), defaultMySetting()).empty());
        std::string other = defaultMySetting();
        other[0x70] = char(0x84);
        fixSettingsCrc(&other);
        auto d = diffSettingsFiles(SettingsKind::MySetting, defaultMySetting(), other);
        bool payload = false, crc = false, raw = false;
        for (const auto& r : d) {
            if (r.field == "payload 0x04") { payload = true; QCOMPARE(r.b, std::string("0x84")); }
            if (r.field == "crc") crc = true;
            if (r.field == "raw bytes") raw = true;
        }
        QVERIFY(payload && crc && raw);
        std::string p2 = defaultDjprofile();
        QVERIFY(setDjprofileUserName(&p2, "Ana"));
        d = diffSettingsFiles(SettingsKind::Djprofile, defaultDjprofile(), p2);
        QCOMPARE(d.front().field, std::string("name on the players"));
        QCOMPARE(d.front().b, std::string("Ana"));
    }

    void hexDumpFormat()
    {
        const std::string h = hexDump(std::string("AB\x01", 3));
        QVERIFY(h.rfind("00000000  41 42 01", 0) == 0);
        QVERIFY(h.find("|AB.|") != std::string::npos);
        QVERIFY(hexDump(std::string(100, 'a'), 32).find("68 more bytes") != std::string::npos);
    }

    void kindsByName()
    {
        SettingsKind k;
        for (const std::string& n : settingsFileNames()) { QVERIFY(settingsKindForName(n, &k)); QCOMPARE(std::string(settingsKindName(k)), n); }
        QVERIFY(!settingsKindForName("set.txt", &k));
    }

    void pendriveLinks()
    {
        Library lib = newLibrary();
        QVERIFY(createSettingsSet(lib, "Home", "Ana"));
        QVERIFY(createSettingsSet(lib, "Club", "Bo"));
        QVERIFY(lib.createDrive("Fri"));
        QVERIFY(lib.createDrive("Sat"));
        QCOMPARE(driveSettingsSet(lib, "Fri"), std::string());
        QCOMPARE(effectiveDriveSettingsSet(lib, "Fri"), std::string());
        std::string err;
        QVERIFY(!setDriveSettingsSet(lib, "Fri", "Nope", &err));
        QVERIFY(!setDriveSettingsSet(lib, "Nope", "Club", &err));
        QVERIFY(setDriveSettingsSet(lib, "Fri", "Club"));
        QCOMPARE(driveSettingsSet(lib, "Fri"), std::string("Club"));
        QCOMPARE(effectiveDriveSettingsSet(lib, "Fri"), std::string("Club"));
        QCOMPARE(drivesUsingSettingsSet(lib, "Club"), (std::vector<std::string>{"Fri"}));
        // other keys in drive.txt survive, the link is written once
        QVERIFY(atomicWrite(lib.drivesDir() / "Fri/drive.txt", "note: keep\nsettings: Club\n"));
        QVERIFY(setDriveSettingsSet(lib, "Fri", "Home"));
        QCOMPARE(slurp(lib.drivesDir() / "Fri/drive.txt"), std::string("note: keep\nsettings: Home\n"));
        QVERIFY(setDriveSettingsSet(lib, "Fri", ""));                                  // unlink
        QCOMPARE(slurp(lib.drivesDir() / "Fri/drive.txt"), std::string("note: keep\n"));
        QVERIFY(setDriveSettingsSet(lib, "Sat", "Club"));
        QVERIFY(setDriveSettingsSet(lib, "Sat", ""));
        QVERIFY(!fs::exists(lib.drivesDir() / "Sat/drive.txt"));                     // nothing left to say: no file
        // a pendrive follows its set through a rename, the rename of the pendrive carries the link, a used set cannot be deleted
        QVERIFY(setDriveSettingsSet(lib, "Fri", "Club"));
        QVERIFY(renameSettingsSet(lib, "Club", "Gig"));
        QCOMPARE(driveSettingsSet(lib, "Fri"), std::string("Gig"));
        QVERIFY(lib.renameDrive("Fri", "Friday"));
        QCOMPARE(driveSettingsSet(lib, "Friday"), std::string("Gig"));
        QVERIFY(!deleteSettingsSet(lib, "Gig", &err));
        QVERIFY2(err.find("Friday") != std::string::npos, err.c_str());
        QVERIFY(hasSettingsSet(lib, "Gig"));
        QVERIFY(setDriveSettingsSet(lib, "Friday", ""));
        QVERIFY(deleteSettingsSet(lib, "Gig"));
        // a link to a set that vanished behind our back falls back to the default and says so
        QVERIFY(atomicWrite(lib.drivesDir() / "Friday/drive.txt", "settings: Ghost\n"));
        bool dangling = false;
        QCOMPARE(effectiveDriveSettingsSet(lib, "Friday", &dangling), std::string());
        QVERIFY(dangling);
    }

    void badNamesRefused()
    {
        Library lib = newLibrary();
        std::string err;
        for (const char* bad : {"..", ".", "", "a/b", ".hidden"}) {
            QVERIFY2(!createSettingsSet(lib, bad, "", &err), bad);
            QVERIFY(!hasSettingsSet(lib, bad));
        }
        QVERIFY(createSettingsSet(lib, "ok", ""));
        QVERIFY(!renameSettingsSet(lib, "ok", "../x"));
        QVERIFY(!duplicateSettingsSet(lib, "ok", "a/b"));
        QVERIFY(!deleteSettingsSet(lib, ".."));
    }

    void renameDuplicateDelete()
    {
        Library lib = newLibrary();
        QVERIFY(createSettingsSet(lib, "One", "me"));
        QVERIFY(createSettingsSet(lib, "Two", "me"));
        QVERIFY(renameSettingsSet(lib, "One", "Uno"));
        QCOMPARE(defaultSettingsSet(lib), std::string("Uno"));              // the default follows the rename
        QVERIFY(!renameSettingsSet(lib, "Uno", "Two"));
        QVERIFY(duplicateSettingsSet(lib, "Uno", "Copy"));
        QCOMPARE(slurp(settingsSetDir(lib, "Copy") / "MYSETTING.DAT"), slurp(settingsSetDir(lib, "Uno") / "MYSETTING.DAT"));
        QCOMPARE(listSettingsSets(lib)[0].source, std::string("copy of Uno"));
        QVERIFY(deleteSettingsSet(lib, "Uno"));
        QVERIFY(!hasSettingsSet(lib, "Uno"));
        QVERIFY(defaultSettingsSet(lib) != "Uno");
        QVERIFY(deleteSettingsSet(lib, "Copy"));
        QVERIFY(deleteSettingsSet(lib, "Two"));
        QCOMPARE(defaultSettingsSet(lib), std::string());
    }

    void uniqueNames()
    {
        Library lib = newLibrary();
        QCOMPARE(uniqueSettingsSetName(lib, "Mine"), std::string("Mine"));
        QVERIFY(createSettingsSet(lib, "Mine", ""));
        QCOMPARE(uniqueSettingsSetName(lib, "Mine"), std::string("Mine 2"));
        QCOMPARE(uniqueSettingsSetName(lib, "a/b"), std::string("a_b"));
    }

    void migrationOfFlatFiles()
    {
        fs::path root = tmp_ / "old";
        std::string err;
        QVERIFY(Library::create(root, &err));
        QVERIFY(atomicWrite(root / "user-settings/DEVSETTING.DAT", defaultDevSetting()));
        QVERIFY(atomicWrite(root / "user-settings/djprofile.nxs", "mine"));
        QVERIFY(atomicWrite(root / "user-settings/other.txt", "keep me"));
        Library lib;
        QVERIFY2(Library::open(root, &lib, &err), err.c_str());
        QVERIFY(!fs::exists(root / "user-settings/DEVSETTING.DAT"));
        QCOMPARE(slurp(settingsSetDir(lib, "imported") / "djprofile.nxs"), std::string("mine"));
        QCOMPARE(defaultSettingsSet(lib), std::string("imported"));
        QCOMPARE(listSettingsSets(lib)[0].source, std::string("migrated"));
        QCOMPARE(slurp(root / "user-settings/other.txt"), std::string("keep me"));        // unknown files are never touched
        // idempotent, and a second open does not make another set
        QVERIFY(Library::open(root, &lib, &err));
        QCOMPARE(listSettingsSets(lib).size(), size_t(1));
        // flat files appearing next to existing sets are left alone
        QVERIFY(atomicWrite(root / "user-settings/djprofile.nxs", "late"));
        QVERIFY(Library::open(root, &lib, &err));
        QCOMPARE(slurp(root / "user-settings/djprofile.nxs"), std::string("late"));
    }

    void copyFromDrive()
    {
        Library lib = newLibrary();
        fs::path drive = makeDrive(settingsFileNames());
        std::vector<std::string> found;
        std::string err;
        QVERIFY2(copySettingsSetFromDrive(lib, "FromDeck", drive, &found, &err), err.c_str());
        QCOMPARE(found.size(), size_t(5));
        QCOMPARE(slurp(settingsSetDir(lib, "FromDeck") / "djprofile.nxs"), std::string("profile-A"));
        QCOMPARE(defaultSettingsSet(lib), std::string("FromDeck"));
        QVERIFY(listSettingsSets(lib)[0].source.rfind("rekordbox:", 0) == 0);
        QVERIFY(!copySettingsSetFromDrive(lib, "FromDeck", drive, nullptr, &err));        // name taken
        // two named sets from two drives
        fs::path drive2 = makeDrive(settingsFileNames(), "B");
        QVERIFY(copySettingsSetFromDrive(lib, "Second", drive2));
        QCOMPARE(slurp(settingsSetDir(lib, "Second") / "djprofile.nxs"), std::string("profile-B"));
        // no hidden temp folder is left behind
        for (const auto& e : fs::directory_iterator(lib.settingsDir())) QVERIFY(e.path().filename().string()[0] != '.');
    }

    void copyFromDriveOnlyAndPartial()
    {
        Library lib = newLibrary();
        fs::path drive = makeDrive({"DEVSETTING.DAT", "djprofile.nxs"});
        std::vector<std::string> found;
        QVERIFY(copySettingsSetFromDrive(lib, "Part", drive, &found));
        QCOMPARE(found.size(), size_t(2));
        QVERIFY(!fs::exists(settingsSetDir(lib, "Part") / "MYSETTING.DAT"));
        std::vector<std::string> only = {"djprofile.nxs"};
        QVERIFY(copySettingsSetFromDrive(lib, "Only", drive, &found, nullptr, &only));
        QCOMPARE(found, only);
        std::string err;
        std::vector<std::string> none = {"RBFLTR.DAT"};
        QVERIFY(!copySettingsSetFromDrive(lib, "None", drive, nullptr, &err, &none));
        QVERIFY(!hasSettingsSet(lib, "None"));
        QVERIFY(!copySettingsSetFromDrive(lib, "Empty", tmp_ / "nowhere", nullptr, &err));
        QVERIFY(!hasSettingsSet(lib, "Empty"));
    }

    void damagedSettingsRefusesTheCopy()
    {
        Library lib = newLibrary();
        fs::path drive = makeDrive(settingsFileNames());
        std::string bad = defaultMySetting();
        bad[0x70] ^= 0x01;
        QVERIFY(atomicWrite(drive / "PIONEER/MYSETTING.DAT", bad));
        std::string err;
        QVERIFY(!copySettingsSetFromDrive(lib, "Bad", drive, nullptr, &err));
        QVERIFY2(err.find("MYSETTING.DAT") != std::string::npos, err.c_str());
        QVERIFY(!hasSettingsSet(lib, "Bad"));
        QVERIFY(listSettingsSets(lib).empty());
    }

    void resolveFallsBackToBuiltIn()
    {
        Library lib = newLibrary();
        SettingsFiles f;
        std::vector<std::string> builtin;
        std::string err;
        // no set at all: everything built in
        QVERIFY2(resolveSettingsFiles(lib, "", &f, &builtin, &err), err.c_str());
        QCOMPARE(builtin.size(), size_t(5));
        QCOMPARE(f.devSetting, defaultDevSetting());
        // a partial set: its own profile, built in for the rest
        QVERIFY(copySettingsSetFromDrive(lib, "Part", makeDrive({"djprofile.nxs"})));
        QVERIFY2(resolveSettingsFiles(lib, "Part", &f, &builtin, &err), err.c_str());
        QCOMPARE(f.djprofile, std::string("profile-A"));
        QCOMPARE(f.mySetting, defaultMySetting());
        QCOMPARE(builtin.size(), size_t(4));
        // empty name = the default set
        QVERIFY(resolveSettingsFiles(lib, "", &f));
        QCOMPARE(f.djprofile, std::string("profile-A"));
        QVERIFY(!resolveSettingsFiles(lib, "Missing", &f, nullptr, &err));
    }

    void resolveRefusesDamagedSettings()
    {
        Library lib = newLibrary();
        QVERIFY(createSettingsSet(lib, "Home", ""));
        std::string bad = defaultDevSetting();
        bad[0x6c] ^= 0x01;
        QVERIFY(atomicWrite(settingsSetDir(lib, "Home") / "DEVSETTING.DAT", bad));
        SettingsFiles f;
        std::string err;
        QVERIFY(!resolveSettingsFiles(lib, "Home", &f, nullptr, &err));
        QVERIFY2(err.find("DEVSETTING.DAT") != std::string::npos, err.c_str());
    }
};

QTEST_APPLESS_MAIN(TestTrakcrateSettings)
#include "test_trakcrate_settings.moc"
