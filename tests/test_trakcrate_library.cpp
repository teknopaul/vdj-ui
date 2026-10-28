#include <QtTest/QtTest>

#include "trakcrate/fsutil.h"
#include "trakcrate/library.h"
#include "trakcrate/natsort.h"
#include "trakcrate/sha256.h"
#include "trakcrate/track.h"

#include <cmath>
#include <fstream>

using namespace trakcrate;

static std::string slurp(const fs::path& p)
{
    std::string s;
    readFile(p, &s);
    return s;
}

class TestTrakcrateLibrary : public QObject
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

private slots:
    void init() { tmp_ = makeTempDir("trakcrate-test"); QVERIFY(!tmp_.empty()); }
    void cleanup() { std::error_code ec; fs::remove_all(tmp_, ec); }

    void sha256Known();
    void naturalOrder();
    void sanitizing();
    void atomicWriteReplaces();
    void kvRoundTripKeepsUnknown();
    void kvEscapes();
    void beatGridRoundTrip();
    void cuemarksRoundTrip();
    void cuemarksHandWritten();
    void vocalsRoundTrip();
    void trackFiles();
    void libraryLayout();
    void trackDirCollisions();
    void playlistOrderAndRenumber();
    void playlistWidthGrows();
    void playlistRemoveAndDelete();
    void unsafeNamesRefused();
    void pendriveEntries();
    void moveEntriesAsABlock();
    void removalImpactAndForget();
};

void TestTrakcrateLibrary::sha256Known()
{
    QCOMPARE(QString::fromStdString(sha256Hex("")), QStringLiteral("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    QCOMPARE(QString::fromStdString(sha256Hex("abc")), QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    std::string million(1000000, 'a');
    QCOMPARE(QString::fromStdString(sha256Hex(million)), QStringLiteral("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    Sha256 inc;                                                      // incremental equals one shot, across the 64 byte block boundary
    inc.update(million.data(), 63);
    inc.update(million.data() + 63, million.size() - 63);
    QCOMPARE(QString::fromStdString(inc.finishHex()), QStringLiteral("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

void TestTrakcrateLibrary::naturalOrder()
{
    QVERIFY(naturalLess("track2", "track10"));
    QVERIFY(!naturalLess("track10", "track2"));
    QVERIFY(naturalLess("apple", "Banana"));                         // case-insensitive
    QVERIFY(naturalLess("a01", "a1") != naturalLess("a1", "a01"));   // total order even for equal numbers
    QVERIFY(naturalLess("a", "ab"));
    std::vector<std::string> v = {"b", "Track10", "track2", "A", "track1"};
    std::sort(v.begin(), v.end(), naturalLess);
    QCOMPARE(QString::fromStdString(v[0]), QStringLiteral("A"));
    QCOMPARE(QString::fromStdString(v[2]), QStringLiteral("track1"));
    QCOMPARE(QString::fromStdString(v[3]), QStringLiteral("track2"));
    QCOMPARE(QString::fromStdString(v[4]), QStringLiteral("Track10"));
}

void TestTrakcrateLibrary::sanitizing()
{
    QCOMPARE(QString::fromStdString(sanitizeName("a/b")), QStringLiteral("a_b"));
    QCOMPARE(QString::fromStdString(sanitizeName("..")), QStringLiteral("_"));
    QCOMPARE(QString::fromStdString(sanitizeName("  .hidden ")), QStringLiteral("hidden"));
    QCOMPARE(QString::fromStdString(sanitizeName("")), QStringLiteral("_"));
    QCOMPARE(QString::fromStdString(sanitizeName("tab\there")), QStringLiteral("tab_here"));
    std::string longName;
    for (int i = 0; i < 150; ++i) longName += "\xC3\xA9";            // 300 bytes of e-acute: must be cut on a character boundary
    std::string s = sanitizeName(longName);
    QVERIFY(s.size() <= 200);
    QVERIFY(s.size() % 2 == 0);
    QVERIFY(isSafeName("ok name"));
    QVERIFY(!isSafeName(""));
    QVERIFY(!isSafeName(".."));
    QVERIFY(!isSafeName("a/b"));
}

void TestTrakcrateLibrary::atomicWriteReplaces()
{
    fs::path f = tmp_ / "x.txt";
    QVERIFY(atomicWrite(f, "one"));
    QVERIFY(atomicWrite(f, "two"));
    QCOMPARE(QString::fromStdString(slurp(f)), QStringLiteral("two"));
    int files = 0;
    for (const auto& e : fs::directory_iterator(tmp_)) { (void)e; ++files; }
    QCOMPARE(files, 1);                                              // no temp file left behind
    std::string err;
    QVERIFY(!atomicWrite(tmp_ / "missing-dir" / "x.txt", "x", &err));
    QVERIFY(!err.empty());
}

void TestTrakcrateLibrary::kvRoundTripKeepsUnknown()
{
    std::string text = "# trakcrate meta\ntitle: Song\n\nfuture_key: keep me\n# note\nrating: 4\nweird line without colon\n";
    KvDoc d = KvDoc::parse(text);
    QCOMPARE(QString::fromStdString(d.get("title")), QStringLiteral("Song"));
    QCOMPARE(d.getInt("rating"), 4LL);
    QCOMPARE(QString::fromStdString(d.serialise()), QString::fromStdString(text));      // byte for byte
    d.set("rating", "5");
    d.set("artist", "Me");
    QCOMPARE(QString::fromStdString(d.get("future_key")), QStringLiteral("keep me"));
    QVERIFY(d.serialise().find("# note\nrating: 5\n") != std::string::npos);            // updated in place
    QVERIFY(d.serialise().find("artist: Me\n") != std::string::npos);
    d.remove("future_key");
    QVERIFY(!d.has("future_key"));
}

void TestTrakcrateLibrary::kvEscapes()
{
    KvDoc d;
    d.set("comment", "line one\nline two with \\ backslash: and colon");
    KvDoc back = KvDoc::parse(d.serialise());
    QCOMPARE(QString::fromStdString(back.get("comment")), QStringLiteral("line one\nline two with \\ backslash: and colon"));
    std::string once = d.serialise();
    QCOMPARE(std::count(once.begin(), once.end(), '\n'), 1);                            // still one line
}

void TestTrakcrateLibrary::beatGridRoundTrip()
{
    KvDoc d;
    d.addComment("trakcrate beatgrid v1");
    BeatGrid g;
    g.bpm = 128.0;
    g.firstBeatUs = 51972;
    setBeatGrid(d, g);
    BeatGrid r = beatGridFrom(KvDoc::parse(d.serialise()));
    QCOMPARE(r.bpm, 128.0);
    QCOMPARE(r.firstBeatUs, 51972LL);
    QVERIFY(!r.empty());
    QVERIFY(BeatGrid().empty());
}

void TestTrakcrateLibrary::cuemarksRoundTrip()
{
    CuemarkFile f;
    f.comments = {"# trakcrate cuemarks v1"};
    Cuemark a;
    a.beat = 17; a.ms = 7551.6; a.label = "drop one"; a.hot = 'A';
    Cuemark b;
    b.beat = std::nan(""); b.ms = 30051.4; b.extra = {"future"};
    f.marks = {a, b};
    CuemarkFile r = parseCuemarks(serialiseCuemarks(f));
    QCOMPARE(r.comments.size(), size_t(1));
    QCOMPARE(r.marks.size(), size_t(2));
    QCOMPARE(r.marks[0].beat, 17.0);
    QCOMPARE(r.marks[0].ms, 7551.6);
    QCOMPARE(QString::fromStdString(r.marks[0].label), QStringLiteral("drop one"));    // spaces survive (tab separated)
    QCOMPARE(r.marks[0].hot, 'A');
    QVERIFY(std::isnan(r.marks[1].beat));
    QCOMPARE(r.marks[1].hot, char(0));
    QCOMPARE(r.marks[1].extra.size(), size_t(1));
    QCOMPARE(serialiseCuemarks(r), serialiseCuemarks(f));
}

void TestTrakcrateLibrary::cuemarksHandWritten()
{
    // the example in doc/trakcrate/cuemarks.md is space aligned
    CuemarkFile f = parseCuemarks("# beat ms label hot colour\n17.0   7551.6   drop-1   A\n  33.0 15051.8 breakdown\n\nnot a mark\n65.0 30051.4 build b\n");
    QCOMPARE(f.marks.size(), size_t(3));
    QCOMPARE(f.marks[0].hot, 'A');
    QCOMPARE(QString::fromStdString(f.marks[1].label), QStringLiteral("breakdown"));
    QCOMPARE(f.marks[2].hot, 'B');                                   // lower case accepted
    QCOMPARE(f.comments.size(), size_t(1));
}

void TestTrakcrateLibrary::vocalsRoundTrip()
{
    VocalsFile f = parseVocals("# trakcrate vocals v1\n33.0\t65.0\t15210.5\t30211.2\tvocal\t0.84\n-\t-\t60213.0\t68000.0\thook\t0.4\textra\n");
    QCOMPARE(f.regions.size(), size_t(2));
    QCOMPARE(f.regions[0].confidence, 0.84);
    QVERIFY(std::isnan(f.regions[1].startBeat));
    QCOMPARE(QString::fromStdString(f.regions[1].kind), QStringLiteral("hook"));
    VocalsFile again = parseVocals(serialiseVocals(f));
    QCOMPARE(serialiseVocals(again), serialiseVocals(f));
}

void TestTrakcrateLibrary::trackFiles()
{
    fs::path dir = tmp_ / "t1";
    fs::create_directories(dir);
    { std::ofstream(dir / "song.MP3") << "audio"; }
    QCOMPARE(audioFileOf(dir).filename().string(), std::string("song.MP3"));
    Track t;
    std::string err;
    QVERIFY(loadTrack(dir, &t, &err));                               // only audio: loads with empty documents
    QVERIFY(!t.hasGrid && !t.hasCuemarks && t.meta.empty());

    t.meta.set("title", "Song");
    t.meta.set("rating", "3");
    t.meta.set("comment", "good\nvocal");
    setOrigin(t.source, "meta", "edited");
    t.hasGrid = true;
    setBeatGrid(t.grid, BeatGrid{125.5, 1000});
    t.hasCuemarks = true;
    t.cuemarks.marks.push_back(Cuemark{4, 1900, "start", 'B', "", {}});
    QVERIFY(saveMeta(t, &err));
    QVERIFY(saveSource(t, &err));
    QVERIFY(saveBeatGrid(t, &err));
    QVERIFY(saveCuemarks(t, &err));
    QVERIFY(saveVocals(t, &err));                                    // hasVocals false: writes nothing
    QVERIFY(!fs::exists(dir / "vocals.txt"));

    Track r;
    QVERIFY(loadTrack(dir, &r, &err));
    QCOMPARE(QString::fromStdString(r.title()), QStringLiteral("Song"));
    QCOMPARE(r.rating(), 3);
    QCOMPARE(QString::fromStdString(r.comment()), QStringLiteral("good\nvocal"));
    QCOMPARE(QString::fromStdString(origin(r.source, "meta")), QStringLiteral("edited"));
    QCOMPARE(QString::fromStdString(origin(r.source, "waveform")), QStringLiteral("generated"));   // default
    QCOMPARE(beatGridFrom(r.grid).bpm, 125.5);
    QCOMPARE(r.cuemarks.marks.size(), size_t(1));
    QCOMPARE(r.cuemarks.marks[0].hot, 'B');
    QVERIFY(!loadTrack(tmp_ / "nope", &r, &err));
}

void TestTrakcrateLibrary::libraryLayout()
{
    Library lib = newLibrary();
    QVERIFY(lib.isOpen());
    QVERIFY(fs::is_directory(lib.masterDir()));
    QVERIFY(fs::is_directory(lib.pendrivesDir()));
    QVERIFY(fs::is_directory(lib.settingsDir()));
    Library again;
    QVERIFY(Library::create(lib.root()));                            // creating again is harmless
    QVERIFY(Library::open(lib.root(), &again));
    Library bad;
    std::string err;
    QVERIFY(!Library::open(tmp_ / "empty", &bad, &err));
    QVERIFY(!err.empty());

    std::string e;
    for (const char* f : {"Zed", "alpha", "Folder10", "Folder2"}) QVERIFY(!lib.createTrackDir(f, "t", &e).empty());
    std::vector<std::string> folders = lib.listFolders();
    QCOMPARE(folders.size(), size_t(4));
    QCOMPARE(QString::fromStdString(folders[0]), QStringLiteral("alpha"));
    QCOMPARE(QString::fromStdString(folders[1]), QStringLiteral("Folder2"));      // natural order
    QCOMPARE(QString::fromStdString(folders[2]), QStringLiteral("Folder10"));
    fs::create_directories(lib.masterDir() / ".trash");              // hidden folders are not listed
    QCOMPARE(lib.listFolders().size(), size_t(4));
    QCOMPARE(lib.listTracks("alpha").size(), size_t(1));
    QVERIFY(lib.listTracks("../etc").empty());
}

void TestTrakcrateLibrary::trackDirCollisions()
{
    Library lib = newLibrary();
    fs::path a = lib.createTrackDir("F", "Song");
    fs::path b = lib.createTrackDir("F", "Song");
    fs::path c = lib.createTrackDir("F", "a/b");
    QCOMPARE(a.filename().string(), std::string("Song"));
    QCOMPARE(b.filename().string(), std::string("Song 2"));
    QCOMPARE(c.filename().string(), std::string("a_b"));
    QCOMPARE(lib.listTracks("F").size(), size_t(3));
}

void TestTrakcrateLibrary::playlistOrderAndRenumber()
{
    Library lib = newLibrary();
    std::string err;
    QVERIFY(lib.createPlaylist("mix", &err));
    QVERIFY(!lib.createPlaylist("mix", &err));                       // exists
    QVERIFY(lib.hasPlaylist("mix"));
    fs::path t1 = lib.createTrackDir("F", "one"), t2 = lib.createTrackDir("F", "two"), t3 = lib.createTrackDir("F", "three");
    QVERIFY(lib.addToPlaylist("mix", t1, -1, &err));
    QVERIFY(lib.addToPlaylist("mix", t2, -1, &err));
    QVERIFY(lib.addToPlaylist("mix", t3, 0, &err));                  // insert first
    std::vector<fs::path> order = lib.playlistTracks("mix");
    QCOMPARE(order.size(), size_t(3));
    QCOMPARE(order[0].filename().string(), std::string("three"));
    QCOMPARE(order[1].filename().string(), std::string("one"));
    QCOMPARE(order[2].filename().string(), std::string("two"));
    QVERIFY(fs::exists(lib.pendrivesDir() / "mix" / "symlinks" / "000_three"));
    QVERIFY(fs::exists(lib.pendrivesDir() / "mix" / "symlinks" / "001_one"));
    QVERIFY(fs::exists(lib.pendrivesDir() / "mix" / "symlinks" / "002_two"));
    QVERIFY(fs::read_symlink(lib.pendrivesDir() / "mix" / "symlinks" / "000_three").is_relative());   // library stays movable
    QVERIFY(lib.addToPlaylist("mix", t1, 1, &err));                  // the same track twice is allowed
    QCOMPARE(lib.playlistTracks("mix").size(), size_t(4));
    QVERIFY(lib.renumberPlaylist("mix", &err));
    QVERIFY(!lib.addToPlaylist("nope", t1, -1, &err));
    QVERIFY(!lib.addToPlaylist("mix", tmp_ / "not-a-track", -1, &err));
}

void TestTrakcrateLibrary::playlistWidthGrows()
{
    Library lib = newLibrary();
    QVERIFY(lib.createPlaylist("big"));
    fs::path t = lib.createTrackDir("F", "t");
    fs::path links = lib.pendrivesDir() / "big" / "symlinks";
    // 1200 links created by hand with a 3 digit prefix, then renumbered
    for (int i = 0; i < 1200; ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "%03d_t", i);
        fs::create_directory_symlink(fs::relative(t, links), links / name);
    }
    QCOMPARE(lib.playlistTracks("big").size(), size_t(1200));
    std::string err;
    QVERIFY(lib.renumberPlaylist("big", &err));
    QVERIFY(fs::exists(links / "0000_t"));
    QVERIFY(fs::exists(links / "1199_t"));
    QVERIFY(!fs::exists(links / "000_t"));
    std::vector<fs::path> order = lib.playlistTracks("big");
    QCOMPARE(order.size(), size_t(1200));
    QVERIFY(lib.addToPlaylist("big", t, 0, &err));
    QVERIFY(fs::exists(links / "0000_t"));
    QVERIFY(fs::exists(links / "1200_t"));
}

void TestTrakcrateLibrary::playlistRemoveAndDelete()
{
    Library lib = newLibrary();
    std::string err;
    QVERIFY(lib.createPlaylist("p"));
    fs::path t1 = lib.createTrackDir("F", "one"), t2 = lib.createTrackDir("F", "two");
    QVERIFY(lib.addToPlaylist("p", t1));
    QVERIFY(lib.addToPlaylist("p", t2));
    QVERIFY(lib.removeFromPlaylist("p", 0, &err));
    std::vector<fs::path> order = lib.playlistTracks("p");
    QCOMPARE(order.size(), size_t(1));
    QCOMPARE(order[0].filename().string(), std::string("two"));
    QVERIFY(fs::exists(lib.pendrivesDir() / "p" / "symlinks" / "000_two"));
    QVERIFY(fs::exists(t1));                                         // the track itself is untouched
    QVERIFY(!lib.removeFromPlaylist("p", 5, &err));
    QVERIFY(lib.deletePlaylist("p", &err));
    QVERIFY(!lib.hasPlaylist("p"));
    QVERIFY(fs::exists(t2));
    QVERIFY(!lib.deletePlaylist("p", &err));
}

void TestTrakcrateLibrary::unsafeNamesRefused()
{
    Library lib = newLibrary();
    std::string err;
    QVERIFY(!lib.createPlaylist("../escape", &err));
    QVERIFY(!lib.createPlaylist("..", &err));
    QVERIFY(!lib.createPlaylist("", &err));
    QVERIFY(!lib.hasPlaylist("../x"));
    QVERIFY(lib.listPlaylists().empty());
    QVERIFY(!fs::exists(tmp_ / "escape"));
}

static fs::path mk(const Library& lib, const char* folder, const char* name)
{
    fs::path d = lib.createTrackDir(folder, name);
    std::ofstream(d / "track.wav") << "x";
    return d;
}

void TestTrakcrateLibrary::moveEntriesAsABlock()
{
    Library lib;
    QVERIFY(Library::create(tmp_ / "lib") && Library::open(tmp_ / "lib", &lib));
    std::vector<fs::path> t;
    for (const char* n : {"a", "b", "c", "d", "e", "f"}) t.push_back(mk(lib, "F", n));
    QVERIFY(lib.createPlaylist("P") && lib.addTracksToPlaylist("P", t));
    auto names = [&]() { std::string s; for (const fs::path& p : lib.playlistTracks("P")) s += p.filename().string(); return s; };
    QCOMPARE(names(), std::string("abcdef"));
    QVERIFY(lib.moveEntries("P", {4, 5}, 0));                    // to the start, keeping their order
    QCOMPARE(names(), std::string("efabcd"));
    QVERIFY(lib.moveEntries("P", {0, 1}, 6));                    // to the end
    QCOMPARE(names(), std::string("abcdef"));
    QVERIFY(lib.moveEntries("P", {1, 3}, 5));                    // not contiguous: they gather before the old row 5
    QCOMPARE(names(), std::string("acebdf"));
    QVERIFY(lib.moveEntries("P", {5}, 2));
    QCOMPARE(names(), std::string("acfebd"));
    QVERIFY(lib.moveEntries("P", {2}, 2) && lib.moveEntries("P", {2}, 3));      // dropped where it already is: nothing changes
    QCOMPARE(names(), std::string("acfebd"));
    QVERIFY(!lib.moveEntries("P", {}, 1));
    QVERIFY(!lib.moveEntries("P", {9}, 1));
    QVERIFY(!lib.moveEntries("P", {1}, 7));
    QVERIFY(!lib.moveEntries("P", {-1}, 1));
    QCOMPARE(names(), std::string("acfebd"));
    QVERIFY(lib.moveEntries("P", {1, 1, 0}, 6));                 // duplicates are ignored
    QCOMPARE(names(), std::string("febdac"));
}

void TestTrakcrateLibrary::pendriveEntries()
{
    Library lib;
    QVERIFY(Library::create(tmp_ / "lib") && Library::open(tmp_ / "lib", &lib));
    fs::path a = mk(lib, "House", "a"), b = mk(lib, "House", "b"), c = mk(lib, "Dub", "c");
    QVERIFY(lib.createPlaylist("Peak") && lib.addTracksToPlaylist("Peak", {c, a}));
    QVERIFY(lib.listDrives().empty());
    QVERIFY(lib.createDrive("Friday"));
    QVERIFY(!lib.createDrive("Friday"));
    QVERIFY(!lib.createDrive("a/b"));
    QVERIFY(lib.hasDrive("Friday"));
    QVERIFY(lib.createDrive("Other"));
    QVERIFY(!lib.renameDrive("Friday", "Other"));                    // the name is taken
    QVERIFY(!lib.renameDrive("Friday", "a/b"));
    QVERIFY(lib.renameDrive("Friday", "Saturday") && lib.hasDrive("Saturday") && !lib.hasDrive("Friday"));
    QVERIFY(lib.renameDrive("Saturday", "Friday"));
    QVERIFY(lib.deleteDrive("Other"));
    QVERIFY(lib.addFolderToDrive("Friday", "House"));
    QVERIFY(lib.addPlaylistToDrive("Friday", "Peak"));
    QVERIFY(lib.addFolderToDrive("Friday", "Dub"));
    QVERIFY(!lib.addFolderToDrive("Friday", "Nothing"));
    QVERIFY(!lib.addPlaylistToDrive("Nope", "Peak"));
    auto es = lib.driveEntries("Friday");
    QCOMPARE(es.size(), size_t(3));
    QCOMPARE(es[0].name, std::string("House"));
    QCOMPARE(es[0].kind, std::string("folder"));
    QCOMPARE(es[0].tracks, 2);
    QCOMPARE(es[1].name, std::string("Peak"));
    QCOMPARE(es[1].kind, std::string("playlist"));
    QCOMPARE(es[1].source, std::string("Peak"));
    QVERIFY(lib.addFolderToDrive("Friday", "House"));              // same name again: made unique
    QCOMPARE(lib.driveEntries("Friday")[3].name, std::string("House 2"));
    QVERIFY(lib.removeDriveEntry("Friday", 3));

    // reorder the entries; the tracks follow their entry
    QVERIFY(lib.moveDriveEntry("Friday", 2, 0));
    es = lib.driveEntries("Friday");
    QCOMPARE(es[0].name, std::string("Dub"));
    QCOMPARE(es[1].name, std::string("House"));
    QCOMPARE(es[2].name, std::string("Peak"));
    QCOMPARE(es[0].dir, std::string("000_Dub"));
    QVERIFY(!lib.moveDriveEntry("Friday", 0, 9));

    // reorder and remove tracks inside an entry: the library's own playlist is untouched
    Library d = lib.driveLibrary("Friday");
    QCOMPARE(d.playlistTracks(es[2].dir), (std::vector<fs::path>{c, a}));
    QVERIFY(lib.moveDriveTrack("Friday", 2, 1, 0));
    QCOMPARE(d.playlistTracks(es[2].dir), (std::vector<fs::path>{a, c}));
    QCOMPARE(lib.playlistTracks("Peak"), (std::vector<fs::path>{c, a}));
    QVERIFY(lib.removeDriveTrack("Friday", 2, 0));
    QCOMPARE(d.playlistTracks(es[2].dir), (std::vector<fs::path>{c}));
    QVERIFY(!lib.removeDriveTrack("Friday", 2, 5));

    // the pendrives are not playlists of the library, and deleting one keeps the tracks
    QCOMPARE(lib.listPlaylists(), (std::vector<std::string>{"Peak"}));
    QVERIFY(lib.deleteDrive("Friday"));
    QVERIFY(lib.listDrives().empty());
    QVERIFY(fs::exists(a / "track.wav") && fs::exists(c / "track.wav"));
}

void TestTrakcrateLibrary::removalImpactAndForget()
{
    Library lib;
    QVERIFY(Library::create(tmp_ / "lib") && Library::open(tmp_ / "lib", &lib));
    fs::path a = mk(lib, "House", "a"), b = mk(lib, "House", "b"), c = mk(lib, "Dub", "c");
    QVERIFY(lib.createPlaylist("Peak") && lib.addTracksToPlaylist("Peak", {c, a, b}));
    QVERIFY(lib.createPlaylist("Other") && lib.addTracksToPlaylist("Other", {c}));
    QVERIFY(lib.createDrive("Fri") && lib.addFolderToDrive("Fri", "House") && lib.addPlaylistToDrive("Fri", "Peak"));

    QVERIFY(!lib.impactOfRemoving({}).any());
    Library::Impact im = lib.impactOfRemoving({a, b});             // the whole House folder
    QVERIFY(im.any());
    QCOMPARE(im.playlists, (std::vector<std::string>{"Peak"}));
    QCOMPARE(im.driveEntries.size(), size_t(2));                   // Fri / House and Fri / Peak
    QCOMPARE(im.links, 6);                                         // a and b in Peak (2), in House (2), in Fri's Peak (2)

    QVERIFY(lib.forgetTracks({a, b}));
    QCOMPARE(lib.playlistTracks("Peak"), (std::vector<fs::path>{c}));
    QCOMPARE(lib.playlistTracks("Other"), (std::vector<fs::path>{c}));
    QCOMPARE(lib.driveEntries("Fri")[0].tracks, 0);
    QVERIFY(!lib.impactOfRemoving({a, b}).any());
    // links to a folder that was already deleted are still found and removed
    QVERIFY(lib.createDrive("Fri2") && lib.addFolderToDrive("Fri2", "Dub"));
    std::error_code ec;
    fs::remove_all(lib.masterDir() / "Dub", ec);
    QVERIFY(lib.impactOfRemoving({c}).any());
    QVERIFY(lib.forgetTracks({c}));
    QCOMPARE(lib.playlistTracks("Peak").size(), size_t(0));
    QCOMPARE(lib.driveEntries("Fri2")[0].tracks, 0);
}

QTEST_APPLESS_MAIN(TestTrakcrateLibrary)
#include "test_trakcrate_library.moc"
