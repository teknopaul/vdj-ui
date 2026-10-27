#include <QtTest/QtTest>

#include <cdj/anlz.h>
#include <cdj/pdb.h>

#include "trakcrate/anlz_writer.h"
#include "trakcrate/artwork_paths.h"
#include "trakcrate/fsutil.h"
#include "trakcrate/settings_files.h"

#include <map>

using namespace trakcrate;

namespace {

std::string slurp(const std::string& path)
{
    std::string s;
    readFile(path, &s);
    return s;
}

struct Tag { std::string name; uint32_t headerLen, tagLen; std::string headerBody, body; };

std::vector<Tag> splitTags(const std::string& f)
{
    std::vector<Tag> tags;
    auto be32 = [&](size_t at) { return uint32_t(uint8_t(f[at])) << 24 | uint32_t(uint8_t(f[at + 1])) << 16 | uint32_t(uint8_t(f[at + 2])) << 8 | uint32_t(uint8_t(f[at + 3])); };
    for (size_t at = 28; at + 12 <= f.size();) {
        Tag t;
        t.name = f.substr(at, 4);
        t.headerLen = be32(at + 4);
        t.tagLen = be32(at + 8);
        if (t.tagLen < t.headerLen || at + t.tagLen > f.size()) break;
        t.headerBody = f.substr(at + 12, t.headerLen - 12);
        t.body = f.substr(at + t.headerLen, t.tagLen - t.headerLen);
        tags.push_back(t);
        at += t.tagLen;
    }
    return tags;
}

AnlzCue toCue(const anlz_cue_t& c)
{
    AnlzCue o;
    o.hotCue = c.hot_cue;
    o.isLoop = c.is_loop;
    o.timeMs = c.time_ms;
    o.loopTimeMs = c.is_loop ? c.loop_time_ms : 0;
    o.marker = c.marker;
    o.loopNum = c.loop_num;
    o.loopDen = c.loop_den;
    return o;
}

// Reads a real pair with libcdj into the data our writer takes.
bool readReal(const std::string& dat, const std::string& ext, AnlzTrackData* d)
{
    anlz_t *a = nullptr, *x = nullptr;
    if (anlz_open_mem(reinterpret_cast<const uint8_t*>(dat.data()), uint32_t(dat.size()), &a) != ANLZ_OK) return false;
    if (anlz_open_mem(reinterpret_cast<const uint8_t*>(ext.data()), uint32_t(ext.size()), &x) != ANLZ_OK) { anlz_close(a); return false; }
    const char* path = nullptr;
    anlz_path(a, &path);
    d->audioPath = path ? path : "";
    anlz_beatgrid_t g;
    if (anlz_beatgrid(a, &g) == ANLZ_OK) {
        anlz_beatgrid_attach_sub_ms(&g, x);
        for (uint32_t i = 0; i < g.count; ++i) {
            anlz_beat_t b;
            anlz_beat_at(&g, i, &b);
            AnlzBeat o;
            o.number = b.beat_number; o.bpmX100 = b.bpm_x100; o.timeMs = b.time_ms;
            d->beats.push_back(o);
            d->beatSubUs.push_back(b.sub_ms_us);
        }
        if (!g.has_sub_ms) d->beatSubUs.clear();
    }
    auto wave = [&](const anlz_t* f, anlz_wave_kind_t k, std::vector<uint8_t>* out) {
        anlz_waveform_t w;
        if (anlz_waveform(f, k, &w) == ANLZ_OK) out->assign(w.data, w.data + size_t(w.count) * w.entry_bytes);
    };
    wave(a, ANLZ_WAVE_PREVIEW, &d->pwav);
    wave(a, ANLZ_WAVE_TINY, &d->pwv2);
    wave(x, ANLZ_WAVE_DETAIL, &d->pwv3);
    wave(x, ANLZ_WAVE_COLOR_PREVIEW, &d->pwv4);
    std::vector<uint8_t> v5;
    wave(x, ANLZ_WAVE_COLOR_DETAIL, &v5);
    for (size_t i = 0; i + 1 < v5.size(); i += 2) d->pwv5.push_back(uint16_t(v5[i] << 8 | v5[i + 1]));
    anlz_cue_list_t hot = {}, mem = {};
    if (anlz_cues_from(x, ANLZ_CUES_HOT, 1, &hot) == ANLZ_OK) for (uint32_t i = 0; i < hot.count; ++i) d->hotCues.push_back(toCue(hot.cues[i]));
    if (anlz_cues_from(x, ANLZ_CUES_MEMORY, 1, &mem) == ANLZ_OK) for (uint32_t i = 0; i < mem.count; ++i) d->memoryCues.push_back(toCue(mem.cues[i]));
    anlz_cue_list_free(&hot);
    anlz_cue_list_free(&mem);
    anlz_close(a);
    anlz_close(x);
    return true;
}

} // namespace

class TestTrakcrateAnlz : public QObject
{
    Q_OBJECT

private:
    std::string root_ = std::string(TRAKCRATE_FIXTURES) + "/dbserver/export7/PIONEER/USBANLZ/";
    std::string root2_ = std::string(TRAKCRATE_FIXTURES) + "/dbserver/export/PIONEER/USBANLZ/";

    void checkPair(const std::string& dir)
    {
        const std::string dat = slurp(dir + "/ANLZ0000.DAT"), ext = slurp(dir + "/ANLZ0000.EXT");
        QVERIFY2(!dat.empty() && !ext.empty(), dir.c_str());
        AnlzTrackData d;
        QVERIFY(readReal(dat, ext, &d));
        AnlzFiles ours;
        std::string err;
        QVERIFY2(buildAnlz(d, &ours, &err), err.c_str());
        for (int which = 0; which < 2; ++which) {
            const std::string& real = which ? ext : dat;
            const std::string& mine = which ? ours.ext : ours.dat;
            const std::string label = dir + (which ? " EXT" : " DAT");
            const QString qlabel = QString::fromStdString(label);
            QCOMPARE(mine.substr(0, 28).size(), size_t(28));
            std::vector<Tag> a = splitTags(real), b = splitTags(mine);
            QVERIFY2(a.size() == b.size(), qPrintable(QString::fromStdString(label) + ": tag count"));
            for (size_t i = 0; i < a.size(); ++i) {
                const QString where = QString::fromStdString(label + " " + a[i].name);
                QVERIFY2(a[i].name == b[i].name, qPrintable(where + ": order, got " + QString::fromStdString(b[i].name)));
                QVERIFY2(a[i].headerLen == b[i].headerLen, qPrintable(where + ": header length"));
                QVERIFY2(a[i].tagLen == b[i].tagLen, qPrintable(where + ": tag length"));
                if (a[i].name == "PVBR") continue;                    // body unknown: the real one has two stray bytes, ours is zero
                if (a[i].name == "PWV4") { QVERIFY(a[i].headerBody == b[i].headerBody); continue; }   // our PWV4 data comes from the real one here, but is rebuilt elsewhere
                if (a[i].name == "PQT2") {                            // the word at 0x2c is a per-file value we do not know
                    std::string ha = a[i].headerBody, hb = b[i].headerBody;
                    ha.replace(0x2c - 12, 4, 4, '\0');
                    QVERIFY2(ha == hb, qPrintable(where + ": header"));
                    QVERIFY2(a[i].body == b[i].body, qPrintable(where + ": body"));
                    continue;
                }
                if (a[i].name == "PQTZ") {                            // the low half of the second header word varies in 2 of 14 real files (0xff1a, 0xff5a)
                    std::string ha = a[i].headerBody;
                    ha.replace(6, 2, 2, '\0');
                    QVERIFY2(ha == b[i].headerBody, qPrintable(where + ": header words"));
                    QVERIFY2(a[i].body == b[i].body, qPrintable(where + ": body"));
                    continue;
                }
                QVERIFY2(a[i].headerBody == b[i].headerBody, qPrintable(where + ": header words"));
                QVERIFY2(a[i].body == b[i].body, qPrintable(where + ": body"));
            }
            QCOMPARE(mine.size(), real.size());
            QVERIFY(anlz_file_len_ok != nullptr);
        }
        // libcdj reads our files and sees the same track
        anlz_t *a = nullptr, *x = nullptr;
        QCOMPARE(int(anlz_open_mem(reinterpret_cast<const uint8_t*>(ours.dat.data()), uint32_t(ours.dat.size()), &a)), int(ANLZ_OK));
        QCOMPARE(int(anlz_open_mem(reinterpret_cast<const uint8_t*>(ours.ext.data()), uint32_t(ours.ext.size()), &x)), int(ANLZ_OK));
        QCOMPARE(anlz_is_damaged(a), 0);
        QCOMPARE(anlz_is_damaged(x), 0);
        QCOMPARE(anlz_file_len_ok(a), 1);
        QCOMPARE(anlz_file_len_ok(x), 1);
        const char* path = nullptr;
        QCOMPARE(int(anlz_path(a, &path)), int(ANLZ_OK));
        QCOMPARE(std::string(path), d.audioPath);
        anlz_beatgrid_t g;
        QCOMPARE(int(anlz_beatgrid(a, &g)), int(ANLZ_OK));
        QCOMPARE(size_t(g.count), d.beats.size());
        anlz_close(a);
        anlz_close(x);
    }

private slots:
    void sevenRealPairsTagByTag();
    void unanalysedTrackHasNoGrid();
    void beatsFromAGridMatchRekordbox();
    void cuesArePlacedLikeRekordbox();
    void invalidDataIsRefused();
    void waveformsFromAnalysis();
    void crcOfTheSettingsFiles();
    void settingsFilesAreValidated();
    void artworkPathsMatchTheBigExport();
};

void TestTrakcrateAnlz::sevenRealPairsTagByTag()
{
    int pairs = 0;
    for (const char* sub : {"P004/00028148", "P00C/0000D1E2", "P01D/0002D3C1", "P022/0002380C", "P033/0002AF15", "P038/0002A7A8", "P064/00013152"}) {
        checkPair(root_ + sub);
        ++pairs;
    }
    QCOMPARE(pairs, 7);
}

void TestTrakcrateAnlz::unanalysedTrackHasNoGrid()
{
    // P02A of the larger fixture set: rekordbox never analysed it (empty grid, no cues, no PQT2)
    const std::string dir = root2_ + "P02A/0000ECBE";
    const std::string dat = slurp(dir + "/ANLZ0000.DAT"), ext = slurp(dir + "/ANLZ0000.EXT");
    AnlzTrackData d;
    QVERIFY(readReal(dat, ext, &d));
    QVERIFY(d.beats.empty());
    AnlzFiles ours;
    QVERIFY(buildAnlz(d, &ours));
    std::vector<Tag> a = splitTags(ext), b = splitTags(ours.ext);
    bool pqt2Real = false, pqt2Ours = false;
    for (const Tag& t : a) pqt2Real |= (t.name == "PQT2");
    for (const Tag& t : b) pqt2Ours |= (t.name == "PQT2");
    QVERIFY(!pqt2Real);
    QVERIFY(!pqt2Ours);
    std::vector<Tag> da = splitTags(dat), db = splitTags(ours.dat);
    QCOMPARE(da.size(), db.size());
    for (size_t i = 0; i < da.size(); ++i) {
        QCOMPARE(da[i].name, db[i].name);
        if (da[i].name == "PQTZ") { QCOMPARE(da[i].tagLen, 24u); QCOMPARE(db[i].tagLen, 24u); QCOMPARE(da[i].headerBody, db[i].headerBody); }
    }
}

void TestTrakcrateAnlz::beatsFromAGridMatchRekordbox()
{
    const std::string dir = root_ + "P00C/0000D1E2";
    AnlzTrackData real;
    QVERIFY(readReal(slurp(dir + "/ANLZ0000.DAT"), slurp(dir + "/ANLZ0000.EXT"), &real));
    QCOMPARE(real.beats.size(), size_t(521));
    BeatGrid g;
    g.bpm = real.beats[0].bpmX100 / 100.0;
    g.firstBeatUs = (long long)real.beats[0].timeMs * 1000 + real.beatSubUs[0];
    std::vector<uint16_t> sub;
    const double durationMs = real.beats.back().timeMs + 1.0;
    std::vector<AnlzBeat> mine = beatsFromGrid(g, durationMs, &sub);
    QCOMPARE(mine.size(), real.beats.size());
    QCOMPARE(sub.size(), mine.size());
    int off = 0;
    for (size_t i = 0; i < mine.size(); ++i) {
        QCOMPARE(mine[i].number, real.beats[i].number);
        QCOMPARE(mine[i].bpmX100, real.beats[i].bpmX100);
        QVERIFY(sub[i] < 1000);
        const long long diffMs = (long long)mine[i].timeMs - (long long)real.beats[i].timeMs;
        QVERIFY2(std::llabs(diffMs) <= 1, qPrintable(QString("beat %1 differs by %2 ms").arg(i).arg(diffMs)));
        off += diffMs != 0;
    }
    qInfo("rebuilt a constant grid: %d of %zu beats differ by 1 ms (rekordbox rounds its own way)", off, mine.size());
    QVERIFY(off < int(mine.size()) / 4);                                  // the real period is a hair below 60/bpm of the rounded tempo
    QVERIFY(beatsFromGrid(BeatGrid(), 10000, &sub).empty());
    QVERIFY(sub.empty());
    QVERIFY(beatsFromGrid(g, 0, &sub).empty());
    QVERIFY(beatsFromGrid(g, 600.0, &sub).size() <= 2);                    // a very short track
}

void TestTrakcrateAnlz::cuesArePlacedLikeRekordbox()
{
    // hot cues A (marker 150), B loop, C: the three patterns of the 7 track drive; the .DAT lists three, the .EXT all
    AnlzTrackData d;
    d.audioPath = "/Contents/x/y.mp3";
    d.pwav.assign(400, 0); d.pwv2.assign(100, 0);
    AnlzCue a; a.hotCue = 1; a.timeMs = 5; a.marker = 150;
    AnlzCue b; b.hotCue = 2; b.isLoop = true; b.timeMs = 14820; b.loopTimeMs = 22205; b.loopNum = 16; b.loopDen = 1;
    AnlzCue c; c.hotCue = 3; c.timeMs = 73000;
    AnlzCue e; e.hotCue = 4; e.timeMs = 90000;
    AnlzCue m; m.hotCue = 0; m.timeMs = 1234;
    d.hotCues = {a, b, c, e};
    d.memoryCues = {m};
    AnlzFiles f;
    QVERIFY(buildAnlz(d, &f));
    anlz_t *dat = nullptr, *ext = nullptr;
    QCOMPARE(int(anlz_open_mem(reinterpret_cast<const uint8_t*>(f.dat.data()), uint32_t(f.dat.size()), &dat)), int(ANLZ_OK));
    QCOMPARE(int(anlz_open_mem(reinterpret_cast<const uint8_t*>(f.ext.data()), uint32_t(f.ext.size()), &ext)), int(ANLZ_OK));
    anlz_cue_list_t l = {};
    QCOMPARE(int(anlz_cues_from(dat, ANLZ_CUES_HOT, 0, &l)), int(ANLZ_OK));
    QCOMPARE(int(l.count), 3);                                              // only the first three in the .DAT
    QCOMPARE(int(l.cues[0].marker), 150);
    QCOMPARE(int(l.cues[1].is_loop), 1);
    QCOMPARE(int(l.cues[1].loop_time_ms), 22205);
    anlz_cue_list_free(&l);
    QCOMPARE(int(anlz_cues_from(dat, ANLZ_CUES_MEMORY, 0, &l)), int(ANLZ_OK));
    QCOMPARE(int(l.count), 1);
    QCOMPARE(int(l.cues[0].time_ms), 1234);
    anlz_cue_list_free(&l);
    QCOMPARE(int(anlz_cues_from(ext, ANLZ_CUES_HOT, 1, &l)), int(ANLZ_OK));
    QCOMPARE(int(l.count), 4);
    QCOMPARE(int(l.cues[1].loop_num), 16);
    QCOMPARE(int(l.cues[1].loop_den), 1);
    QCOMPARE(int(l.cues[3].time_ms), 90000);
    anlz_cue_list_free(&l);
    anlz_close(dat);
    anlz_close(ext);
}

void TestTrakcrateAnlz::invalidDataIsRefused()
{
    AnlzTrackData d;
    AnlzFiles f;
    std::string err;
    QVERIFY(!buildAnlz(d, &f, &err));                                       // no waveforms
    d.pwav.assign(400, 0); d.pwv2.assign(100, 0);
    QVERIFY(buildAnlz(d, &f, &err));                                        // an empty track: allowed
    d.pwv3.assign(10, 0);
    QVERIFY(!buildAnlz(d, &f, &err));                                       // PWV3 and PWV5 must agree
    d.pwv5.assign(10, 0);
    QVERIFY(buildAnlz(d, &f, &err));
    d.pwv4.assign(5, 0);
    QVERIFY(!buildAnlz(d, &f, &err));
    d.pwv4.clear();
    d.beats.assign(1, AnlzBeat());
    d.beatSubUs.assign(2, 0);
    QVERIFY(!buildAnlz(d, &f, &err));
    d.pwav.assign(399, 0);
    d.beatSubUs.clear();
    QVERIFY(!buildAnlz(d, &f, &err));
}

void TestTrakcrateAnlz::waveformsFromAnalysis()
{
    Waveform w;
    w.pwav.assign(400, 0x25);
    w.pwv2.assign(100, 5);
    w.pwv3.assign(300, 0x2a);
    w.pwv5.assign(300, uint16_t((7 << 13) | (3 << 10) | (1 << 7) | (20 << 2)));
    AnlzTrackData d;
    d.audioPath = "/Contents/a.mp3";
    setWaveforms(&d, w, true);
    QCOMPARE(d.pwv4.size(), size_t(7200));
    QCOMPARE(int(d.pwv4[0]), 20 * 127 / 31);
    QCOMPARE(int(d.pwv4[1]), 255 - 20 * 127 / 31);
    AnlzFiles f;
    std::string err;
    QVERIFY2(buildAnlz(d, &f, &err), err.c_str());
    anlz_t* x = nullptr;
    QCOMPARE(int(anlz_open_mem(reinterpret_cast<const uint8_t*>(f.ext.data()), uint32_t(f.ext.size()), &x)), int(ANLZ_OK));
    anlz_waveform_t wf;
    QCOMPARE(int(anlz_waveform(x, ANLZ_WAVE_DETAIL, &wf)), int(ANLZ_OK));
    QCOMPARE(int(wf.count), 300);
    QCOMPARE(int(anlz_waveform(x, ANLZ_WAVE_COLOR_DETAIL, &wf)), int(ANLZ_OK));
    QCOMPARE(int(wf.count), 300);
    anlz_wave_column_t col;
    anlz_wave_column(&wf, 0, &col);
    QCOMPARE(int(col.height), 20);
    QCOMPARE(int(anlz_waveform(x, ANLZ_WAVE_COLOR_PREVIEW, &wf)), int(ANLZ_OK));
    QCOMPARE(int(wf.count), 1200);
    anlz_close(x);
    setWaveforms(&d, w, false);                                             // PWV4 left out on request
    QVERIFY(d.pwv4.empty());
    QVERIFY(buildAnlz(d, &f));
    bool has4 = false;
    for (const Tag& t : splitTags(f.ext)) has4 |= (t.name == "PWV4");
    QVERIFY(!has4);
    Waveform none;                                                          // a track with no audio columns
    none.pwav.assign(400, 0); none.pwv2.assign(100, 0);
    AnlzTrackData e;
    setWaveforms(&e, none, true);
    QCOMPARE(e.pwv4.size(), size_t(7200));
    QVERIFY(buildAnlz(e, &f));
}

void TestTrakcrateAnlz::crcOfTheSettingsFiles()
{
    QCOMPARE(int(crc16Xmodem("123456789", 9)), 0x31C3);                    // the standard check value of CRC-16/XMODEM
    QCOMPARE(int(crc16Xmodem("", 0)), 0);
    std::string err;
    QVERIFY2(verifySettingsFile(defaultDevSetting(), &err), err.c_str());
    QVERIFY2(verifySettingsFile(defaultMySetting(), &err), err.c_str());
    QCOMPARE(defaultDevSetting().size(), size_t(140));
    QCOMPARE(defaultMySetting().size(), size_t(148));
    // identical to the files of the real drive
    const std::string base = std::string(TRAKCRATE_REFERENCES) + "/rekordbox/PIONEER/";
    const std::string realDev = slurp(base + "DEVSETTING.DAT"), realMy = slurp(base + "MYSETTING.DAT");
    if (!realDev.empty()) QVERIFY(realDev == defaultDevSetting());
    if (!realMy.empty()) QVERIFY(realMy == defaultMySetting());
    QCOMPARE(int(defaultDevSetting()[0]), 0x60);
    QVERIFY(defaultDevSetting().find("PIONEER DJ") != std::string::npos);
    QVERIFY(defaultMySetting().find("rekordbox") != std::string::npos);
}

void TestTrakcrateAnlz::settingsFilesAreValidated()
{
    std::string err;
    std::string bad = defaultMySetting();
    bad[0x70] = char(bad[0x70] ^ 0x01);                                     // flip a payload bit
    QVERIFY(!verifySettingsFile(bad, &err));
    QCOMPARE(err, std::string("CRC mismatch"));
    QVERIFY(fixSettingsCrc(&bad));                                          // a changed payload can be given a new CRC
    QVERIFY2(verifySettingsFile(bad, &err), err.c_str());
    QVERIFY(bad != defaultMySetting());
    std::string magic = defaultDevSetting();
    magic[0x68] = 0;
    QVERIFY(!verifySettingsFile(magic, &err));
    QVERIFY(!verifySettingsFile(defaultDevSetting().substr(0, 100), &err));
    QVERIFY(!verifySettingsFile("", &err));
    std::string len = defaultDevSetting();
    len[0x64] = 0x21;
    QVERIFY(!verifySettingsFile(len, &err));
    std::string pad = defaultDevSetting();
    pad[pad.size() - 1] = 1;
    QVERIFY(!verifySettingsFile(pad, &err));
    std::string unchanged = defaultDevSetting();
    QVERIFY(fixSettingsCrc(&unchanged));
    QVERIFY(unchanged == defaultDevSetting());                              // recomputing a good CRC changes nothing
    std::string notSettings(200, 'x');
    QVERIFY(!fixSettingsCrc(&notSettings));

    // a user's template is read through the same check
    fs::path dir = makeTempDir("trakcrate-settings");
    QVERIFY(atomicWrite(dir / "good.DAT", defaultMySetting()));
    QVERIFY(atomicWrite(dir / "bad.DAT", bad.substr(0, bad.size() - 3)));
    std::string loaded;
    QVERIFY2(loadSettingsFile((dir / "good.DAT").string(), &loaded, &err), err.c_str());
    QVERIFY(loaded == defaultMySetting());
    QVERIFY(!loadSettingsFile((dir / "bad.DAT").string(), &loaded, &err));
    QVERIFY(err.find("bad.DAT") != std::string::npos);
    QVERIFY(!loadSettingsFile((dir / "missing.DAT").string(), &loaded, &err));
    std::error_code ec;
    fs::remove_all(dir, ec);
}

void TestTrakcrateAnlz::artworkPathsMatchTheBigExport()
{
    QCOMPARE(artworkPath(1), std::string("/PIONEER/Artwork/00001/a1.jpg"));
    QCOMPARE(artworkPath(19), std::string("/PIONEER/Artwork/00001/a19.jpg"));
    QCOMPARE(artworkPath(20), std::string("/PIONEER/Artwork/00002/a20.jpg"));
    QCOMPARE(artworkPath(21), std::string("/PIONEER/Artwork/00002/a21.jpg"));
    QCOMPARE(artworkPath(2061), std::string("/PIONEER/Artwork/00104/a2061.jpg"));
    QCOMPARE(artworkPath(1, true), std::string("/PIONEER/Artwork/00001/a1_m.jpg"));
    const std::string big = slurp(std::string(TRAKCRATE_REFERENCES) + "/src/test/fixtures/export.pdb");
    if (big.empty()) QSKIP("references/libcdj fixtures not available");
    struct Acc { int n = 0, wrong = 0; };
    Acc acc;
    pdb_parse_artwork(reinterpret_cast<const uint8_t*>(big.data()), uint32_t(big.size()), [](const pdb_named_t* r, void* ud) {
        Acc* a = static_cast<Acc*>(ud);
        ++a->n;
        if (artworkPath(r->id) != (r->name ? r->name : "")) ++a->wrong;
        return 0;
    }, &acc);
    QCOMPARE(acc.n, 2087);
    QCOMPARE(acc.wrong, 0);                                                 // the 20 ids per directory rule holds for every row
}

QTEST_APPLESS_MAIN(TestTrakcrateAnlz)
#include "test_trakcrate_anlz_roundtrip.moc"
