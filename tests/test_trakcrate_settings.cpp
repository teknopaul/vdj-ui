#include <QtTest/QtTest>

#include "trakcrate/ext_pdb.h"
#include "trakcrate/fsutil.h"
#include "trakcrate/pdb_writer.h"
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
    // rekordcrate's sample files (tests/fixtures/rekordcrate): one file per option value, made by rekordbox 6
    void newerSettingsFilesFromRekordbox6()
    {
        const fs::path root = fs::path(REKORDCRATE_FIXTURES);
        const fs::path demo = root / "complete_export/demo_tracks/PIONEER";
        std::string err;
        QVERIFY2(verifyMySetting2File(slurp(demo / "MYSETTING2.DAT"), &err), err.c_str());
        QVERIFY2(verifyDjmMySettingFile(slurp(demo / "DJMMYSETTING.DAT"), &err), err.c_str());
        QVERIFY(!verifyMySetting2File(slurp(demo / "DJMMYSETTING.DAT")));           // each refuses the other
        QVERIFY(!verifyDjmMySettingFile(slurp(demo / "MYSETTING2.DAT")));
        std::string broken = slurp(demo / "MYSETTING2.DAT");
        broken[0x74] = char(0x86);                                                   // an option changed without the CRC
        QVERIFY(!verifyMySetting2File(broken, &err));
        QCOMPARE(err, std::string("CRC mismatch"));

        // every sample passes its check, and the viewer names the option the file is named after
        struct Case { const char* dir; const char* field; const char* value; } cases[] = {
            {"mysetting2/beatjumpbeatvalue_64", "Beat Jump Beat Value", "64 Beat"},
            {"mysetting2/beatjumpbeatvalue_half", "Beat Jump Beat Value", "1/2 Beat"},
            {"mysetting2/jogdisplaymode_artwork", "Jog Display Mode", "Artwork"},
            {"mysetting2/joglcdbrightness_5", "Jog LCD Brightness", "5"},
            {"mysetting2/padbuttonbrightness_4", "Pad/Button Brightness", "4"},
            {"mysetting2/vinylspeedadjust_release", "Vinyl Speed Adjust", "Release"},
            {"mysetting2/waveform_phasemeter", "Waveform / Phase Meter", "Phase Meter"},
            {"mysetting2/waveformdivisions_timescale", "Waveform Divisions", "Time Scale"},
            {"djmmysetting/chfadercurve_steepbottom", "Channel Fader Curve", "Steep Bottom"},
            {"djmmysetting/chfadercurvelong_linear", "Channel Fader Curve (long fader)", "Linear"},
            {"djmmysetting/crossfadercurve_slowcut", "Crossfader Curve", "Slow Cut"},
            {"djmmysetting/headphones_monosplit_monosplit", "Headphones Mono Split", "Mono Split"},
            {"djmmysetting/headphones_preeq_preeq", "Headphones Pre EQ", "Pre EQ"},
            {"djmmysetting/beatfxquantize_off", "Beat FX Quantize", "Off"},
            {"djmmysetting/miclowcut_off", "Mic Low Cut", "Off"},
            {"djmmysetting/talkover_mode_normal", "Talk Over Mode", "Normal"},
            {"djmmysetting/talkover_level_6", "Talk Over Level", "-6dB"},
            {"djmmysetting/midi_ch_16", "MIDI Channel", "16"},
            {"djmmysetting/midi_buttontype_trigger", "MIDI Button Type", "Trigger"},
            {"djmmysetting/mixer_brightness_display_white", "Mixer Display Brightness", "White"},
            {"djmmysetting/mixer_brightness_indicator_1", "Mixer Indicator Brightness", "1"},
        };
        for (const Case& c : cases) {
            const bool djm = std::string(c.dir).rfind("djm", 0) == 0;
            const fs::path file = root / c.dir / (djm ? "DJMMYSETTING.DAT" : "MYSETTING2.DAT");
            const std::string bytes = slurp(file);
            QVERIFY2(!bytes.empty(), c.dir);
            QVERIFY2(djm ? verifyDjmMySettingFile(bytes, &err) : verifyMySetting2File(bytes, &err), (std::string(c.dir) + ": " + err).c_str());
            bool found = false;
            for (const SettingsRow& r : describeSettingsFile(djm ? SettingsKind::DjmMySetting : SettingsKind::MySetting2, bytes)) {
                if (r.field != c.field) continue;
                found = true;
                QVERIFY2(r.known, c.dir);
                QCOMPARE(r.value, std::string(c.value));
            }
            QVERIFY2(found, c.dir);
        }
        SettingsKind k;
        QVERIFY(settingsKindForName("MYSETTING2.DAT", &k) && k == SettingsKind::MySetting2);
        QVERIFY(settingsKindForName("DJMMYSETTING.DAT", &k) && k == SettingsKind::DjmMySetting);
        QCOMPARE(std::string(settingsKindName(SettingsKind::DjmMySetting)), std::string("DJMMYSETTING.DAT"));

        // the rekordbox 6 demo defaults: nothing unnamed in the option rows
        for (const SettingsRow& r : describeSettingsFile(SettingsKind::MySetting2, slurp(demo / "MYSETTING2.DAT")))
            if (r.field == "Beat Jump Beat Value") QCOMPARE(r.value, std::string("16 Beat"));
    }

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

    static SettingsRow rowOf(const std::vector<SettingsRow>& rows, const std::string& field)
    {
        for (const SettingsRow& r : rows) if (r.field == field) return r;
        return SettingsRow{"<none>", "<none>", "", true};
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
        QCOMPARE(valueOf(dev, "Overview Waveform Type"), std::string("Half Waveform"));
        QCOMPARE(valueOf(dev, "Waveform Color"), std::string("Blue"));
        for (const char* f : {"Key Display Format", "Waveform Current Position"}) {          // the 4.5.0 file has 0x00 where rekordbox 6 writes 0x01
            QCOMPARE(valueOf(dev, f), std::string("unknown option 0x00"));
            QVERIFY(!rowOf(dev, f).known);
            QVERIFY(rowOf(dev, f).note.find("rekordbox 6 writes 0x01") != std::string::npos);
        }
        QVERIFY(rowOf(dev, "Overview Waveform Type").known);
        for (const char* f : {"unknown 0x70", "unknown 0x73", "unknown 0x76-0x87"}) {
            QVERIFY(valueOf(dev, f) != "<none>");
            QVERIFY(!rowOf(dev, f).known);
        }
        QCOMPARE(valueOf(dev, "unknown 0x76-0x87").size(), size_t(18 * 3 - 1));
        for (const SettingsRow& r : dev) QVERIFY(r.field.rfind("payload 0x", 0) != 0);           // no row per byte any more
        auto my = describeSettingsFile(SettingsKind::MySetting, defaultMySetting());
        QCOMPARE(valueOf(my, "crc"), std::string("0x2181"));
        QCOMPARE(valueOf(my, "block id"), std::string("2"));
        const std::pair<const char*, const char*> shipped[] = {
            {"On Air Display", "On"}, {"LCD Brightness", "3"}, {"Quantize", "On"}, {"Auto Cue Level", "-60dB"}, {"Language", "English"},
            {"Jog Ring Brightness", "2 (Bright)"}, {"Jog Ring Indicator", "On"}, {"Slip Flashing", "On"}, {"Disc Slot Illumination", "2 (Bright)"},
            {"Eject/Load Lock", "Unlock"}, {"Sync", "Off"}, {"Play Mode / Auto Play Mode", "Continue / On"}, {"Quantize Beat Value", "1 Beat"},
            {"Hot Cue Auto Load", "rekordbox SETTING"}, {"Hot Cue Color", "Off"}, {"Time Mode", "Remain"}, {"Jog Mode", "CDJ"},
            {"Auto Cue", "Off"}, {"Master Tempo", "Off"}, {"Tempo Range", "\xc2\xb1" "10%"}, {"Phase Meter", "Type 1"}};
        for (const auto& e : shipped) {
            QCOMPARE(valueOf(my, e.first), std::string(e.second));
            QVERIFY(rowOf(my, e.first).known);
        }
        // the shipped file predates the needle lock option: 0x00 is not a code
        QCOMPARE(valueOf(my, "Needle Lock"), std::string("unknown option 0x00"));
        QVERIFY(!rowOf(my, "Needle Lock").known);
        const std::pair<const char*, const char*> spans[] = {{"unknown 0x75", "01"}, {"unknown 0x79-0x7B", "01 01 01"}, {"unknown 0x83-0x84", "00 00"},
                                                             {"unknown 0x86-0x87", "00 00"}, {"unknown 0x8E-0x8F", "00 00"}};
        for (const auto& e : spans) {
            QCOMPARE(valueOf(my, e.first), std::string(e.second));
            QVERIFY(!rowOf(my, e.first).known);
        }
        for (const SettingsRow& r : my) QVERIFY(r.field.rfind("payload 0x", 0) != 0);
        QCOMPARE(my.size(), size_t(1 + 1 + 6 + 2 + 22 + 5));      // size, file check, the header rows, crc and block id, the preferences, the unknown spans
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

    // every code of every field: the byte is put in a copy of the shipped file and the label read back
    void everyOptionCodeDecodes()
    {
        struct Case { SettingsKind kind; size_t at; const char* field; std::vector<std::pair<int, const char*>> codes; };
        const std::vector<std::pair<int, const char*>> offOn = {{0x80, "Off"}, {0x81, "On"}}, unlockLock = {{0x80, "Unlock"}, {0x81, "Lock"}},
            dark = {{0x80, "Off"}, {0x81, "1 (Dark)"}, {0x82, "2 (Bright)"}};
        const SettingsKind M = SettingsKind::MySetting, D = SettingsKind::DevSetting;
        const std::vector<Case> cases = {
            {D, 0x71, "Overview Waveform Type", {{1, "Half Waveform"}, {2, "Full Waveform"}}},
            {D, 0x72, "Waveform Color", {{1, "Blue"}, {3, "RGB"}, {4, "3Band"}}},
            {D, 0x74, "Key Display Format", {{1, "Classic"}, {2, "Alphanumeric"}}},
            {D, 0x75, "Waveform Current Position", {{1, "Center"}, {2, "Left"}}},
            {M, 0x70, "On Air Display", offOn}, {M, 0x71, "LCD Brightness", {{0x81, "1"}, {0x82, "2"}, {0x83, "3"}, {0x84, "4"}, {0x85, "5"}}},
            {M, 0x72, "Quantize", offOn},
            {M, 0x73, "Auto Cue Level", {{0x80, "-36dB"}, {0x81, "-42dB"}, {0x82, "-48dB"}, {0x83, "-54dB"}, {0x84, "-60dB"}, {0x85, "-66dB"},
                                         {0x86, "-72dB"}, {0x87, "-78dB"}, {0x88, "Memory"}}},
            {M, 0x74, "Language", {{0x81, "English"}, {0x82, "Fran\xc3\xa7" "ais"}, {0x83, "Deutsch"}, {0x84, "Italiano"}, {0x85, "Nederlands"},
                                   {0x86, "Espa\xc3\xb1ol"}, {0x87, "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9"},
                                   {0x88, "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4"}, {0x89, "\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87"},
                                   {0x8a, "\xe7\xb9\x81\xe9\xab\x94\xe4\xb8\xad\xe6\x96\x87"}, {0x8b, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"},
                                   {0x8c, "Portugu\xc3\xaa" "s"}, {0x8d, "Svenska"}, {0x8e, "\xc4\x8c" "e\xc5\xa1" "tina"}, {0x8f, "Magyar"}, {0x90, "Dansk"},
                                   {0x91, "\xce\x95\xce\xbb\xce\xbb\xce\xb7\xce\xbd\xce\xb9\xce\xba\xce\xac"}, {0x92, "T\xc3\xbc" "rk\xc3\xa7" "e"}}},
            {M, 0x76, "Jog Ring Brightness", dark}, {M, 0x77, "Jog Ring Indicator", offOn}, {M, 0x78, "Slip Flashing", offOn},
            {M, 0x7c, "Disc Slot Illumination", dark}, {M, 0x7d, "Eject/Load Lock", unlockLock}, {M, 0x7e, "Sync", offOn},
            {M, 0x7f, "Play Mode / Auto Play Mode", {{0x80, "Continue / On"}, {0x81, "Single / Off"}}},
            {M, 0x80, "Quantize Beat Value", {{0x80, "1 Beat"}, {0x81, "1/2 Beat"}, {0x82, "1/4 Beat"}, {0x83, "1/8 Beat"}}},
            {M, 0x81, "Hot Cue Auto Load", {{0x80, "Off"}, {0x81, "On"}, {0x82, "rekordbox SETTING"}}}, {M, 0x82, "Hot Cue Color", offOn},
            {M, 0x85, "Needle Lock", unlockLock}, {M, 0x88, "Time Mode", {{0x80, "Elapsed"}, {0x81, "Remain"}}},
            {M, 0x89, "Jog Mode", {{0x80, "CDJ"}, {0x81, "Vinyl"}}}, {M, 0x8a, "Auto Cue", offOn}, {M, 0x8b, "Master Tempo", offOn},
            {M, 0x8c, "Tempo Range", {{0x80, "\xc2\xb1" "6%"}, {0x81, "\xc2\xb1" "10%"}, {0x82, "\xc2\xb1" "16%"}, {0x83, "Wide"}}},
            {M, 0x8d, "Phase Meter", {{0x80, "Type 1"}, {0x81, "Type 2"}}},
        };
        for (const Case& c : cases) {
            for (const auto& code : c.codes) {
                std::string f = c.kind == M ? defaultMySetting() : defaultDevSetting();
                f[c.at] = char(code.first);
                fixSettingsCrc(&f);
                const auto rows = describeSettingsFile(c.kind, f);
                QCOMPARE(valueOf(rows, c.field), std::string(code.second));
                QVERIFY(rowOf(rows, c.field).known);
            }
            std::string f = c.kind == M ? defaultMySetting() : defaultDevSetting();
            f[c.at] = char(0x99);
            fixSettingsCrc(&f);
            const auto rows = describeSettingsFile(c.kind, f);
            QCOMPARE(valueOf(rows, c.field), std::string("unknown option 0x99"));
            QVERIFY(!rowOf(rows, c.field).known);
        }
    }

    void settingsOfTheWrongShape()
    {
        // a MYSETTING read as a DEVSETTING: a reason, no decoded values
        auto rows = describeSettingsFile(SettingsKind::DevSetting, defaultMySetting());
        QCOMPARE(rowOf(rows, "settings").known, false);
        QVERIFY(rowOf(rows, "settings").note.find("not a DEVSETTING.DAT") != std::string::npos);
        QCOMPARE(valueOf(rows, "Overview Waveform Type"), std::string("<none>"));
        QCOMPARE(valueOf(rows, "On Air Display"), std::string("<none>"));
        // a truncated file never reads outside the string
        const std::string whole = defaultMySetting();
        for (size_t n = 0; n < whole.size(); ++n) QVERIFY(!describeSettingsFile(SettingsKind::MySetting, whole.substr(0, n)).empty());
        // a bad CRC with the right shape: damaged, but the values are still shown
        std::string bad = defaultMySetting();
        bad[0x70] = char(0x80);
        rows = describeSettingsFile(SettingsKind::MySetting, bad);
        QCOMPARE(valueOf(rows, "file check"), std::string("damaged"));
        QVERIFY(rowOf(rows, "crc").note.find("does not match") != std::string::npos);
        QCOMPARE(valueOf(rows, "On Air Display"), std::string("Off"));
        // a wrong magic stops the decoding
        std::string nomagic = defaultMySetting();
        nomagic[0x68] ^= 0xff;
        QCOMPARE(valueOf(describeSettingsFile(SettingsKind::MySetting, nomagic), "Quantize"), std::string("<none>"));
    }

    // The standard exportExt.pdb is as clean as a file can be: the tags and categories, no deleted rows, no links to tracks.
    void defaultExportExtIsClean()
    {
        ExtPdbStats st;
        QVERIFY(extPdbStats(defaultExportExt(), &st));
        QCOMPARE(st.tags, size_t(28));
        QCOMPARE(st.deletedTags, size_t(0));
        QCOMPARE(st.trackLinks, size_t(0));
        std::vector<ExtTag> tags;
        QVERIFY(readExtTags(defaultExportExt(), &tags));
        QCOMPARE(tags.size(), size_t(28));
        std::vector<std::string> cats;
        for (const ExtTag& t : tags) if (t.category) cats.push_back(t.name);
        QCOMPARE(cats, (std::vector<std::string>{"Genre", "Components", "Situation", "Untitled Column"}));
        // the tags of a category follow it, in the order of their positions
        std::vector<std::string> situation;
        bool in = false;
        for (const ExtTag& t : tags) {
            if (t.category) in = t.name == "Situation";
            else if (in) situation.push_back(t.name);
        }
        QCOMPARE(situation, (std::vector<std::string>{"Main Floor", "Second Floor", "Lounge", "Mid Night", "Morning", "Build up", "Peak Time", "Build down"}));
        QCOMPARE(tags[1].parent, tags[0].id);
        // already clean: nothing changes
        std::string again;
        QVERIFY(withoutTrackLinks(defaultExportExt(), &again));
        QVERIFY(again == defaultExportExt());
    }

    void withoutTrackLinksEmptiesTheLinkTable()
    {
        const std::string clean = defaultExportExt();
        PdbFile f;
        QVERIFY(parsePdb(clean, &f));
        PdbTable* links = nullptr;
        for (PdbTable& t : f.tables) if (t.type == 4) links = &t;
        QVERIFY(links);
        // three links on a data page, the way pdb_append adds the first page of an empty table
        const uint32_t no = links->emptyCandidate, nextFree = f.nextUnusedPage;
        QVERIFY(no < f.pages.size());
        PdbPage np;
        np.index = no;
        np.type = 4;
        np.next = nextFree;
        np.flags = 0x24;
        np.u20 = 3;
        np.structured = true;
        for (uint32_t track : {135u, 137u, 140u}) {
            PdbRow r;
            r.bytes.assign(16, '\0');
            r.bytes[4] = char(track);
            r.bytes[8] = char(28);
            r.bytes[12] = char(3);
            np.rows.push_back(r);
        }
        np.groupFlags.assign(1, 0x7);
        f.pages[no] = np;
        PdbPage& idx = f.pages[links->firstPage];
        QVERIFY(!idx.structured);
        for (int i = 0; i < 4; ++i) idx.raw[0x2c + size_t(i)] = char((no >> (8 * i)) & 0xff);
        idx.next = no;
        idx.raw[0x0c] = char(no);
        links->lastPage = no;
        links->emptyCandidate = nextFree;
        f.nextUnusedPage = nextFree + 1;
        std::string linked;
        QVERIFY(serialisePdb(f, &linked));
        ExtPdbStats st;
        QVERIFY(extPdbStats(linked, &st));
        QCOMPARE(st.trackLinks, size_t(3));
        QCOMPARE(st.tags, size_t(28));

        std::string out;
        QVERIFY(withoutTrackLinks(linked, &out));
        QCOMPARE(out.size(), linked.size());
        QVERIFY(extPdbStats(out, &st));
        QCOMPARE(st.trackLinks, size_t(0));
        QCOMPARE(st.tags, size_t(28));
        PdbFile g;
        QVERIFY(parsePdb(out, &g));
        for (const PdbTable& t : g.tables) {
            if (t.type != 4) continue;
            QCOMPARE(t.lastPage, t.firstPage);                                               // an empty table: its index page only
            QCOMPARE(t.emptyCandidate, no);
            QVERIFY(!g.pages[no].structured);
            QCOMPARE(g.pages[no].raw, std::string(4096, '\0'));                              // the page of the links is blank
            QCOMPARE(g.pages[t.firstPage].next, no);
        }
        // the tags are as they were: the page of the tags is byte for byte the clean one
        QCOMPARE(out.substr(8 * 4096, 4096), clean.substr(8 * 4096, 4096));
        QVERIFY(out.substr(9 * 4096, 4096) == clean.substr(9 * 4096, 4096));                  // and so is the (empty) index page
        std::string twice;
        QVERIFY(withoutTrackLinks(out, &twice));
        QVERIFY(twice == out);
    }

    void withoutTrackLinksDropsDeletedTagRows()
    {
        PdbFile f;
        QVERIFY(parsePdb(defaultExportExt(), &f));
        PdbPage& page = f.pages[8];
        QVERIFY(page.structured);
        QCOMPARE(page.rows.size(), size_t(28));
        page.rows[3].present = false;                                    // Techno
        page.rows[5].present = false;                                    // Electro House
        page.u20 = 26;                                                   // the page header counts the live rows
        std::string edited;
        QVERIFY(serialisePdb(f, &edited));
        ExtPdbStats st;
        QVERIFY(extPdbStats(edited, &st));
        QCOMPARE(st.tags, size_t(26));
        QCOMPARE(st.deletedTags, size_t(2));
        std::string out;
        QVERIFY(withoutTrackLinks(edited, &out));
        QVERIFY(extPdbStats(out, &st));
        QCOMPARE(st.deletedTags, size_t(0));
        QCOMPARE(st.tags, size_t(26));
        std::vector<ExtTag> tags;
        QVERIFY(readExtTags(out, &tags));
        QCOMPARE(tags.size(), size_t(26));
        for (const ExtTag& t : tags) QVERIFY(t.name != "Techno" && t.name != "Electro House");
        // the slot numbers in the rows follow the new places
        PdbFile g;
        QVERIFY(parsePdb(out, &g));
        const PdbPage& p = g.pages[8];
        QVERIFY(p.structured);
        for (size_t i = 0; i < p.rows.size(); ++i) {
            QVERIFY(p.rows[i].present);
            QCOMPARE(unsigned(uint8_t(p.rows[i].bytes[2]) | uint8_t(p.rows[i].bytes[3]) << 8), unsigned(0x20 * i));
        }
        QCOMPARE(int(p.u20), 26);
    }

    void extPdbRefusesOtherFiles()
    {
        std::string out, err;
        QVERIFY(!withoutTrackLinks(std::string("nope"), &out, &err));
        QVERIFY(!err.empty());
        ExtPdbStats st;
        QVERIFY(!extPdbStats(std::string(300, '\0'), &st));
        std::vector<ExtTag> tags;
        QVERIFY(!readExtTags(std::string(), &tags));
    }

    void diffFindsTheChangedByte()
    {
        QVERIFY(diffSettingsFiles(SettingsKind::MySetting, defaultMySetting(), defaultMySetting()).empty());
        std::string other = defaultMySetting();
        other[0x70] = char(0x80);
        fixSettingsCrc(&other);
        auto d = diffSettingsFiles(SettingsKind::MySetting, defaultMySetting(), other);
        bool payload = false, crc = false, raw = false;
        for (const auto& r : d) {
            if (r.field == "On Air Display") { payload = true; QCOMPARE(r.a, std::string("On")); QCOMPARE(r.b, std::string("Off")); }
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
