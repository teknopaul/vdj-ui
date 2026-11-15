#include <QtTest/QtTest>

#include "trakcrate/mix_file.h"

#include <fstream>

using namespace trakcrate;

namespace {

using Pts = std::vector<std::pair<double, double>>;

Pts pts(const Curve& c)
{
    Pts p;
    for (const CurvePoint& q : c.points) p.push_back({q.beat, q.value});
    return p;
}

void touch(const fs::path& p, const std::string& s = "x")
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

struct Lib {
    fs::path root;
    Library lib;
    Lib()
    {
        root = makeTempDir("mixfile");
        Library::create(root, nullptr);
        Library::open(root, &lib, nullptr);
        root = lib.root();
    }
    ~Lib() { std::error_code ec; fs::remove_all(root, ec); }
};

Mix fullMix(const fs::path& root)
{
    Mix m;
    m.name = "Fish & <Chips> \"1\"";
    m.author = "Alice 'A' ü";
    m.image = "covers/x.jpg";
    m.playlist = "House";
    m.masterBpm = 123.456789012345;
    m.stretch.engine = "rubberband-r3";
    m.stretch.options = {{"formant", "preserved"}, {"transients", "crisp"}};
    m.bassHz = 150.5;
    m.highHz = 3000;
    m.slopeDb = 48;
    m.filters.clear();
    MixFilter c;
    c.params = {{"threshold", -6}, {"ratio", 4.5}, {"attack", 2}, {"release", 120}};
    c.on = false;
    m.filters.push_back(c);
    MixFilter l;
    l.kind = MixFilterKind::Ladspa;
    l.plugin = "caps.so|Compress";
    l.params = {{"Gain (dB)", 3.25}, {"Ratio", 0.1}};
    m.filters.push_back(l);
    for (int i = 0; i < 3; ++i) {
        MixTrack t;
        t.file = root / "track-master-copy" / "House" / ("T" + std::to_string(i)) / "track.mp3";
        t.trim = 90 + i * 10;
        t.gridNudgeMs = 1.25 * i;
        t.stretchOff = i == 1;
        if (i == 2) {
            t.stretch.engine = "wsola";
            t.stretch.options = {{"window", "46"}};
        }
        EditRegion r;
        r.kind = EditKind::Loop;
        r.startBeat = 8 + i;
        r.lenBeats = 8;
        r.algo = EditAlgo::Cross;
        r.algoParam = 2;
        r.loopTimes = 3;
        t.regions.push_back(r);
        EditRegion e;
        e.kind = EditKind::Cut;
        e.startBeat = 64;
        e.lenBeats = 16;
        e.algo = EditAlgo::EarlyCut;
        e.earlyShift = 2;
        e.earlyAfter = true;
        e.earlyJoinAlgo = EditAlgo::Cross;
        e.earlyJoinParam = 1;
        t.regions.push_back(e);
        setPoint(&t.volume, 0.1, 33.3);
        setPoint(&t.volume, 5, 100);
        setPoint(&t.volume, 5, 0);
        setPoint(&t.bass, 1.0 / 3, 20);
        setPoint(&t.high, 7, 55);
        t.next = {224.5 + i, 3.25, 16, MixType(i + 2)};
        m.tracks.push_back(t);
    }
    return m;
}

void compare(const Mix& a, const Mix& b)
{
    QCOMPARE(a.name, b.name);
    QCOMPARE(a.author, b.author);
    QCOMPARE(a.image, b.image);
    QCOMPARE(a.playlist, b.playlist);
    QCOMPARE(a.masterBpm, b.masterBpm);
    QCOMPARE(a.stretch.engine, b.stretch.engine);
    QVERIFY(a.stretch.options == b.stretch.options);
    QCOMPARE(a.bassHz, b.bassHz);
    QCOMPARE(a.highHz, b.highHz);
    QCOMPARE(a.slopeDb, b.slopeDb);
    QCOMPARE(a.filters.size(), b.filters.size());
    for (size_t i = 0; i < a.filters.size(); ++i) {
        QCOMPARE(int(a.filters[i].kind), int(b.filters[i].kind));
        QCOMPARE(a.filters[i].on, b.filters[i].on);
        QCOMPARE(a.filters[i].plugin, b.filters[i].plugin);
        QVERIFY(a.filters[i].params == b.filters[i].params);
    }
    QCOMPARE(a.tracks.size(), b.tracks.size());
    for (size_t i = 0; i < a.tracks.size(); ++i) {
        const MixTrack &x = a.tracks[i], &y = b.tracks[i];
        QCOMPARE(x.file.string(), y.file.string());
        QCOMPARE(x.trim, y.trim);
        QCOMPARE(x.gridNudgeMs, y.gridNudgeMs);
        QCOMPARE(x.stretchOff, y.stretchOff);
        QCOMPARE(x.stretch.engine, y.stretch.engine);
        QVERIFY(x.stretch.options == y.stretch.options);
        QCOMPARE(x.regions.size(), y.regions.size());
        for (size_t k = 0; k < x.regions.size(); ++k) {
            const EditRegion &r = x.regions[k], &s = y.regions[k];
            QCOMPARE(int(r.kind), int(s.kind));
            QCOMPARE(r.startBeat, s.startBeat);
            QCOMPARE(r.lenBeats, s.lenBeats);
            QCOMPARE(int(r.algo), int(s.algo));
            QCOMPARE(r.algoParam, s.algoParam);
            QCOMPARE(r.loopTimes, s.loopTimes);
            QCOMPARE(r.earlyShift, s.earlyShift);
            QCOMPARE(r.earlyAfter, s.earlyAfter);
            QCOMPARE(int(r.earlyJoinAlgo), int(s.earlyJoinAlgo));
            QCOMPARE(r.earlyJoinParam, s.earlyJoinParam);
        }
        QVERIFY(pts(x.volume) == pts(y.volume));
        QVERIFY(pts(x.bass) == pts(y.bass));
        QVERIFY(pts(x.high) == pts(y.high));
        if (i + 1 < a.tracks.size()) {
            QCOMPARE(x.next.outStartBeat, y.next.outStartBeat);
            QCOMPARE(x.next.inStartBeat, y.next.inStartBeat);
            QCOMPARE(x.next.lenBeats, y.next.lenBeats);
            QCOMPARE(int(x.next.type), int(y.next.type));
        }
    }
}

} // namespace

class TestMixFile : public QObject
{
    Q_OBJECT
private slots:
    void fullRoundTrip()
    {
        Lib l;
        Mix m = fullMix(l.root);
        for (const MixTrack& t : m.tracks) touch(t.file);
        const std::string xml = mixToXml(m, l.root);
        QVERIFY(xml.find(l.root.string()) == std::string::npos);        // stored relative
        QVERIFY(xml.find("track-master-copy/House/T0/track.mp3") != std::string::npos);
        Mix back;
        std::vector<std::string> warn;
        std::string err;
        QVERIFY2(mixFromXml(xml, l.root, &back, &warn, &err), err.c_str());
        QVERIFY(warn.empty());
        compare(m, back);
        // and the text is stable
        QCOMPARE(mixToXml(back, l.root), xml);
    }

    void fileOutsideRootStaysAbsolute()
    {
        Lib l;
        Lib other;
        Mix m;
        MixTrack t;
        t.file = other.root / "a.mp3";
        touch(t.file);
        m.tracks.push_back(t);
        Mix back;
        QVERIFY(mixFromXml(mixToXml(m, l.root), l.root, &back));
        QCOMPARE(back.tracks[0].file.string(), t.file.string());
        QVERIFY(!back.tracks[0].missing);
    }

    void missingTrackWarnsAndIsKept()
    {
        Lib l;
        Mix m = fullMix(l.root);
        touch(m.tracks[0].file);
        touch(m.tracks[2].file);
        Mix back;
        std::vector<std::string> warn;
        QVERIFY(mixFromXml(mixToXml(m, l.root), l.root, &back, &warn));
        QCOMPARE(back.tracks.size(), size_t(3));
        QVERIFY(!back.tracks[0].missing);
        QVERIFY(back.tracks[1].missing);
        QVERIFY(!back.tracks[2].missing);
        QCOMPARE(warn.size(), size_t(1));
        QVERIFY(warn[0].find("track 2") != std::string::npos);
    }

    void unknownFutureElementsIgnoredNewerVersionRefused()
    {
        Lib l;
        const std::string ok = "<mix version='1'><name>n</name><future a='1'><deep/></future><tempo masterBpm='100' extra='z'/>"
                               "<tracks><track file='a.mp3' wow='1'><hologram/></track></tracks></mix>";
        Mix m;
        std::string err;
        QVERIFY2(mixFromXml(ok, l.root, &m, nullptr, &err), err.c_str());
        QCOMPARE(m.masterBpm, 100.0);
        QCOMPARE(m.tracks.size(), size_t(1));
        QCOMPARE(m.filters.size(), size_t(1));                         // no <filters>: the default compressor
        QVERIFY(!mixFromXml("<mix version='2'/>", l.root, &m, nullptr, &err));
        QVERIFY(err.find("newer") != std::string::npos);
        QVERIFY(!mixFromXml("<other/>", l.root, &m, nullptr, &err));
        QVERIFY(!mixFromXml("<mix", l.root, &m, nullptr, &err));
    }

    void saveLoadAndNames()
    {
        Lib l;
        Mix m = fullMix(l.root);
        for (const MixTrack& t : m.tracks) touch(t.file);
        QVERIFY(!fs::exists(l.lib.mixesDir() / "x"));
        std::string err;
        QVERIFY2(saveMix(l.lib, "Friday", m, &err), err.c_str());
        QVERIFY2(saveMix(l.lib, "Sets/2026/Warmup", m, &err), err.c_str());
        QVERIFY(fs::exists(l.lib.mixesDir() / "Sets" / "2026" / "Warmup.mix"));
        for (const char* bad : {"../x", "a/../b", "/abs", "", "a//b", "a/", ".hidden", "a/.b", ".."})
            QVERIFY2(!saveMix(l.lib, bad, m, &err), bad);
        QVERIFY(!fs::exists(l.root / "x.mix"));
        Mix back;
        QVERIFY2(loadMix(l.lib.mixesDir() / "Friday.mix", l.root, &back, nullptr, &err), err.c_str());
        compare(m, back);
        QVERIFY(!loadMix(l.lib.mixesDir() / "nope.mix", l.root, &back));
        // atomic: no temp files left behind
        for (const auto& e : fs::recursive_directory_iterator(l.lib.mixesDir()))
            QVERIFY(e.path().extension() == ".mix" || e.is_directory());
        // overwrite works
        m.name = "changed";
        QVERIFY(saveMix(l.lib, "Friday", m));
        QVERIFY(loadMix(l.lib.mixesDir() / "Friday.mix", l.root, &back));
        QCOMPARE(back.name, std::string("changed"));
    }

    void treeOfNestedFolders()
    {
        Lib l;
        Mix m;
        QVERIFY(listMixes(l.lib).children.empty());
        for (const char* n : {"b", "a10", "a2", "Zeta/inner", "Alpha/x/deep", "Alpha/y"}) QVERIFY(saveMix(l.lib, n, m));
        touch(l.lib.mixesDir() / ".hidden.mix");
        touch(l.lib.mixesDir() / "notes.txt");
        fs::create_directories(l.lib.mixesDir() / ".git");
        const MixTreeEntry t = listMixes(l.lib);
        QVERIFY(t.folder);
        std::vector<std::string> names;
        for (const auto& c : t.children) names.push_back(c.name + (c.folder ? "/" : ""));
        QCOMPARE(names, (std::vector<std::string>{"Alpha/", "Zeta/", "a2", "a10", "b"}));
        const MixTreeEntry& alpha = t.children[0];
        QCOMPARE(alpha.children.size(), size_t(2));
        QCOMPARE(alpha.children[0].name, std::string("x"));
        QVERIFY(alpha.children[0].folder);
        QCOMPARE(alpha.children[0].children[0].name, std::string("deep"));
        QCOMPARE(alpha.children[1].name, std::string("y"));
        QVERIFY(fs::exists(alpha.children[1].path));
    }

    void uniqueNames()
    {
        Lib l;
        Mix m;
        QCOMPARE(uniqueMixName(l.lib, "Set"), std::string("Set"));
        QVERIFY(saveMix(l.lib, "Set", m));
        QCOMPARE(uniqueMixName(l.lib, "Set"), std::string("Set 2"));
        QVERIFY(saveMix(l.lib, "Set 2", m));
        QCOMPARE(uniqueMixName(l.lib, "Set"), std::string("Set 3"));
    }

    void oldLibraryWithoutMixesFolderWorks()
    {
        Lib l;
        fs::remove_all(l.lib.mixesDir());
        Library again;
        QVERIFY(Library::open(l.root, &again));
        QVERIFY(listMixes(again).children.empty());
        QVERIFY(saveMix(again, "First", Mix()));
        QVERIFY(fs::exists(again.mixesDir() / "First.mix"));
    }
};

QTEST_APPLESS_MAIN(TestMixFile)
#include "test_trakcrate_mix_file.moc"
