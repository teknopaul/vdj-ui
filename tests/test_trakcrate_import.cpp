#include <QtTest/QtTest>

#include "trakcrate/audio_probe.h"
#include "trakcrate/importer.h"
#include "trakcrate/sha256.h"

#include <cmath>
#include <fstream>

using namespace trakcrate;

static std::string slurp(const fs::path& p)
{
    std::string s;
    readFile(p, &s);
    return s;
}

static void put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xFF); }
static void put16(std::string& s, uint16_t v) { s += char(v & 0xFF); s += char(v >> 8); }

// A 16 bit mono 8 kHz sine WAV of `ms` milliseconds; freq makes files differ. title/artist go into a RIFF LIST/INFO chunk.
static void writeWav(const fs::path& p, double freq, int ms = 200, const std::string& title = std::string(), const std::string& artist = std::string())
{
    const int rate = 8000;
    int n = rate * ms / 1000;
    std::string data;
    for (int i = 0; i < n; ++i) put16(data, uint16_t(int16_t(8000 * std::sin(2 * M_PI * freq * i / rate))));
    std::string info;
    auto sub = [&](const char* id, const std::string& v) {
        if (v.empty()) return;
        std::string z = v + '\0';
        if (z.size() % 2) z += '\0';
        info.append(id, 4);
        put32(info, uint32_t(v.size() + 1));
        info += z;
    };
    sub("INAM", title);
    sub("IART", artist);
    std::string list;
    if (!info.empty()) { list = "LIST"; put32(list, uint32_t(info.size() + 4)); list += "INFO"; list += info; }
    std::string fmt;
    put16(fmt, 1); put16(fmt, 1); put32(fmt, rate); put32(fmt, rate * 2); put16(fmt, 2); put16(fmt, 16);
    std::string out = "RIFF";
    put32(out, uint32_t(4 + 8 + fmt.size() + list.size() + 8 + data.size()));
    out += "WAVEfmt ";
    put32(out, uint32_t(fmt.size()));
    out += fmt + list + "data";
    put32(out, uint32_t(data.size()));
    out += data;
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << out;
}

static std::vector<std::string> playlistNames(const Library& lib, const std::string& name)
{
    std::vector<std::string> v;
    for (const fs::path& p : lib.playlistTracks(name)) v.push_back(p.filename().string());
    return v;
}

class TestTrakcrateImport : public QObject
{
    Q_OBJECT

private:
    fs::path tmp_, src_;
    Library lib_;

private slots:
    void init()
    {
        tmp_ = makeTempDir("trakcrate-import");
        QVERIFY(!tmp_.empty());
        std::string err;
        QVERIFY(Library::create(tmp_ / "lib", &err));
        QVERIFY(Library::open(tmp_ / "lib", &lib_, &err));
        src_ = tmp_ / "Acid Loops";
        fs::create_directories(src_);
    }
    void cleanup() { std::error_code ec; fs::remove_all(tmp_, ec); }

    void probeWavAndMp3();
    void probeGarbage();
    void importAlphabetical();
    void importReadsTagsAndArtwork();
    void reimportIsIdempotent();
    void existingPlaylistNeedsAppendOrReplace();
    void duplicateContentIsStoredOnce();
    void dryRunWritesNothing();
    void moveAndLinkModes();
    void recursiveFolders();
    void nameCollisions();
    void playlistOptions();
    void cancelStopsCleanly();
    void failedFileLeavesNoDebris();
};

void TestTrakcrateImport::probeWavAndMp3()
{
    writeWav(src_ / "a.wav", 440, 500, "Hello", "Me");
    AudioInfo w;
    std::string err;
    QVERIFY(probeAudio(src_ / "a.wav", &w, &err));
    QCOMPARE(w.container, std::string("wav"));
    QCOMPARE(w.sampleRate, 8000);
    QCOMPARE(w.channels, 1);
    QCOMPARE(w.bitDepth, 16);
    QVERIFY(std::fabs(w.durationSec - 0.5) < 0.02);
    QCOMPARE(w.tags["title"], std::string("Hello"));
    QCOMPARE(w.tags["artist"], std::string("Me"));
    AudioInfo m;
    QVERIFY(probeAudio(fs::path(TRAKCRATE_FIXTURES) / "tiny.mp3", &m, &err));
    QCOMPARE(m.container, std::string("mp3"));
    QCOMPARE(m.sampleRate, 44100);
    QCOMPARE(m.tags["title"], std::string("Tiny Song"));
    QCOMPARE(m.pictureExt, std::string("png"));
    QVERIFY(m.picture.size() > 50);
}

void TestTrakcrateImport::probeGarbage()
{
    std::ofstream(src_ / "bad.mp3") << "this is not audio at all, just some text bytes";
    { std::ofstream(src_ / "empty.wav"); }
    AudioInfo a;
    std::string err;
    QVERIFY(!probeAudio(src_ / "bad.mp3", &a, &err));
    QVERIFY(!err.empty());
    QVERIFY(!probeAudio(src_ / "empty.wav", &a, &err));
    QVERIFY(!probeAudio(src_ / "missing.wav", &a, &err));
}

void TestTrakcrateImport::importAlphabetical()
{
    writeWav(src_ / "b track.wav", 300);
    writeWav(src_ / "A track.wav", 310);
    writeWav(src_ / "track10.wav", 320);
    writeWav(src_ / "track2.WAV", 330);
    fs::copy_file(fs::path(TRAKCRATE_FIXTURES) / "tiny.mp3", src_ / "tiny.mp3");
    std::ofstream(src_ / "notes.txt") << "not music";
    std::ofstream(src_ / "garbage.mp3") << "not music either";
    std::string before = sha256Hex(slurp(src_ / "track2.WAV"));

    ImportReport r = importFolder(lib_, src_, ImportOptions());
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(r.imported, 5);
    QCOMPARE(r.failed, 1);                                           // garbage.mp3
    QCOMPARE(r.skipped, 1);                                          // notes.txt
    QCOMPARE(r.playlist, std::string("Acid Loops"));
    QCOMPARE(lib_.listFolders().size(), size_t(1));
    QCOMPARE(lib_.listFolders()[0], std::string("Acid Loops"));      // master folder named after the source folder
    QCOMPARE(lib_.listTracks("Acid Loops").size(), size_t(5));
    std::vector<std::string> names = playlistNames(lib_, "Acid Loops");
    std::vector<std::string> want = {"A track", "b track", "tiny", "track2", "track10"};   // natural, case-insensitive
    QCOMPARE(names.size(), want.size());
    for (size_t i = 0; i < want.size(); ++i) QCOMPARE(names[i], want[i]);
    QVERIFY(fs::exists(lib_.pendrivesDir() / "Acid Loops" / "symlinks" / "000_A track"));
    QCOMPARE(sha256Hex(slurp(src_ / "track2.WAV")), before);         // the source is untouched
    QVERIFY(fs::exists(src_ / "A track.wav"));
    for (const std::string& n : names) QVERIFY(!audioFileOf(lib_.trackDir("Acid Loops", n)).empty());
}

void TestTrakcrateImport::importReadsTagsAndArtwork()
{
    fs::copy_file(fs::path(TRAKCRATE_FIXTURES) / "tiny.mp3", src_ / "tiny.mp3");
    writeWav(src_ / "named.wav", 440, 200, "The Real Title", "DJ Test");
    writeWav(src_ / "untagged.wav", 450);
    ImportReport r = importFolder(lib_, src_, ImportOptions());
    QVERIFY2(r.ok, r.error.c_str());

    Track t;
    QVERIFY(loadTrack(lib_.trackDir("Acid Loops", "tiny"), &t));
    QCOMPARE(t.meta.get("title"), std::string("Tiny Song"));
    QCOMPARE(t.meta.get("artist"), std::string("Test Artist"));
    QCOMPARE(t.meta.get("album"), std::string("Test Album"));
    QCOMPARE(t.meta.get("genre"), std::string("Techno"));
    QCOMPARE(t.meta.getInt("year"), 2019LL);
    QCOMPARE(t.meta.getInt("track_number"), 3LL);
    QCOMPARE(t.meta.get("container"), std::string("mp3"));
    QCOMPARE(t.meta.getInt("sample_rate"), 44100LL);
    QCOMPARE(t.meta.get("original_filename"), std::string("tiny.mp3"));
    QCOMPARE(t.meta.get("audio_sha256").size(), size_t(64));
    QCOMPARE(origin(t.source, "meta"), std::string("generated"));
    QVERIFY(!t.source.get("imported_at").empty());
    QVERIFY(fs::exists(t.dir / "artwork.png"));                      // extracted once
    QVERIFY(!fs::exists(t.dir / "artwork.jpg"));
    QCOMPARE(fs::path(audioFileOf(t.dir)).filename().string(), std::string("track.mp3"));

    Track n;
    QVERIFY(loadTrack(lib_.trackDir("Acid Loops", "named"), &n));
    QCOMPARE(n.meta.get("title"), std::string("The Real Title"));
    QCOMPARE(n.meta.get("artist"), std::string("DJ Test"));
    Track u;
    QVERIFY(loadTrack(lib_.trackDir("Acid Loops", "untagged"), &u));
    QCOMPARE(u.meta.get("title"), std::string("untagged"));         // falls back to the file name
    QVERIFY(!fs::exists(u.dir / "artwork.jpg") && !fs::exists(u.dir / "artwork.png"));
}

void TestTrakcrateImport::reimportIsIdempotent()
{
    writeWav(src_ / "one.wav", 400);
    writeWav(src_ / "two.wav", 410);
    QVERIFY(importFolder(lib_, src_, ImportOptions()).ok);
    std::vector<std::string> first = playlistNames(lib_, "Acid Loops");
    ImportOptions o;
    o.append = true;
    ImportReport r = importFolder(lib_, src_, o);
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(r.imported, 0);
    QCOMPARE(r.duplicates, 2);
    QCOMPARE(lib_.listTracks("Acid Loops").size(), size_t(2));       // nothing new in the master
    QCOMPARE(playlistNames(lib_, "Acid Loops"), first);              // and nothing added twice to the playlist
    writeWav(src_ / "three.wav", 420);
    r = importFolder(lib_, src_, o);
    QCOMPARE(r.imported, 1);
    QCOMPARE(playlistNames(lib_, "Acid Loops").size(), size_t(3));
    QCOMPARE(playlistNames(lib_, "Acid Loops").back(), std::string("three"));      // appended at the end
}

void TestTrakcrateImport::existingPlaylistNeedsAppendOrReplace()
{
    writeWav(src_ / "one.wav", 400);
    QVERIFY(importFolder(lib_, src_, ImportOptions()).ok);
    ImportReport r = importFolder(lib_, src_, ImportOptions());
    QVERIFY(!r.ok);
    QVERIFY(r.error.find("exists") != std::string::npos);
    QCOMPARE(r.imported, 0);                                          // refused before doing anything
    writeWav(src_ / "zero.wav", 405);
    ImportOptions rep;
    rep.replace = true;
    r = importFolder(lib_, src_, rep);
    QVERIFY2(r.ok, r.error.c_str());
    std::vector<std::string> names = playlistNames(lib_, "Acid Loops");
    QCOMPARE(names.size(), size_t(2));
    QCOMPARE(names[0], std::string("one"));
    QCOMPARE(names[1], std::string("zero"));
}

void TestTrakcrateImport::duplicateContentIsStoredOnce()
{
    writeWav(src_ / "one.wav", 400);
    fs::copy_file(src_ / "one.wav", src_ / "one copy.wav");          // same bytes, different name
    ImportReport r = importFolder(lib_, src_, ImportOptions());
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(r.imported, 1);
    QCOMPARE(r.duplicates, 1);
    QCOMPARE(lib_.listTracks("Acid Loops").size(), size_t(1));
    QCOMPARE(playlistNames(lib_, "Acid Loops").size(), size_t(1));   // one entry: both files are the same track
    fs::path other = tmp_ / "Other";
    fs::create_directories(other);
    fs::copy_file(src_ / "one.wav", other / "elsewhere.wav");
    ImportReport r2 = importFolder(lib_, other, ImportOptions());
    QCOMPARE(r2.duplicates, 1);
    QCOMPARE(playlistNames(lib_, "Other").size(), size_t(1));        // a new playlist pointing at the existing track
    ImportOptions off;
    off.dedupe = false;
    off.playlist = "NoDedupe";
    QCOMPARE(importFolder(lib_, other, off).imported, 1);
    QCOMPARE(lib_.listFolders().size(), size_t(2));                  // Acid Loops, and Other (made by the no-dedupe import)
}

void TestTrakcrateImport::dryRunWritesNothing()
{
    writeWav(src_ / "one.wav", 400);
    writeWav(src_ / "two.wav", 410);
    ImportOptions o;
    o.dryRun = true;
    ImportReport r = importFolder(lib_, src_, o);
    QVERIFY(r.ok);
    QCOMPARE(r.imported, 2);
    QCOMPARE(r.items[0].status, std::string("would import"));
    QVERIFY(lib_.listFolders().empty());
    QVERIFY(lib_.listPlaylists().empty());
}

void TestTrakcrateImport::moveAndLinkModes()
{
    writeWav(src_ / "moved.wav", 400);
    ImportOptions mv;
    mv.mode = ImportMode::Move;
    mv.playlist = "m";
    QVERIFY(importFolder(lib_, src_, mv).ok);
    QVERIFY(!fs::exists(src_ / "moved.wav"));
    QVERIFY(!audioFileOf(lib_.trackDir("Acid Loops", "moved")).empty());

    writeWav(src_ / "linked.wav", 500);
    ImportOptions ln;
    ln.mode = ImportMode::Link;
    ln.playlist = "l";
    QVERIFY(importFolder(lib_, src_, ln).ok);
    QVERIFY(fs::exists(src_ / "linked.wav"));
    fs::path audio = audioFileOf(lib_.trackDir("Acid Loops", "linked"));
    QVERIFY(!audio.empty());
    QVERIFY(fs::is_symlink(audio));
}

void TestTrakcrateImport::recursiveFolders()
{
    writeWav(src_ / "top.wav", 400);
    writeWav(src_ / "Sub" / "inner.wav", 410);
    writeWav(src_ / "Sub" / "Deep" / "deeper.wav", 420);
    writeWav(src_ / ".hidden" / "secret.wav", 430);
    ImportOptions flat;
    flat.playlist = "flat";
    QCOMPARE(importFolder(lib_, src_, flat).imported, 1);            // not recursive: only the top file

    ImportOptions o;
    o.recursive = true;
    ImportReport r = importFolder(lib_, src_, o);
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(r.imported, 2);                                          // top.wav is a duplicate of the first run
    QCOMPARE(r.duplicates, 1);
    std::vector<std::string> folders = lib_.listFolders();
    QVERIFY(std::find(folders.begin(), folders.end(), "Acid Loops") != folders.end());
    QVERIFY(std::find(folders.begin(), folders.end(), "Acid Loops - Sub") != folders.end());
    QVERIFY(std::find(folders.begin(), folders.end(), "Acid Loops - Sub - Deep") != folders.end());
    QCOMPARE(folders.size(), size_t(3));                              // .hidden is skipped
    std::vector<std::string> names = playlistNames(lib_, "Acid Loops");
    QCOMPARE(names.size(), size_t(3));
    QCOMPARE(names[0], std::string("deeper"));                        // sorted by relative path: Sub/Deep/deeper < Sub/inner < top
    QCOMPARE(names[1], std::string("inner"));
    QCOMPARE(names[2], std::string("top"));
}

void TestTrakcrateImport::nameCollisions()
{
    writeWav(src_ / "x.wav", 400);
    fs::copy_file(fs::path(TRAKCRATE_FIXTURES) / "tiny.mp3", src_ / "x.mp3");
    ImportReport r = importFolder(lib_, src_, ImportOptions());
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(r.imported, 2);
    std::vector<std::string> tracks = lib_.listTracks("Acid Loops");
    QCOMPARE(tracks.size(), size_t(2));
    QCOMPARE(tracks[0], std::string("x"));
    QCOMPARE(tracks[1], std::string("x 2"));
    Track t2;
    QVERIFY(loadTrack(lib_.trackDir("Acid Loops", "x 2"), &t2));
    QVERIFY(t2.meta.get("original_filename") == "x.mp3" || t2.meta.get("original_filename") == "x.wav");
}

void TestTrakcrateImport::playlistOptions()
{
    writeWav(src_ / "one.wav", 400);
    ImportOptions named;
    named.playlist = "friday";
    named.folder = "Gig";
    QVERIFY(importFolder(lib_, src_, named).ok);
    QVERIFY(lib_.hasPlaylist("friday"));
    QVERIFY(!lib_.hasPlaylist("Acid Loops"));
    QCOMPARE(lib_.listFolders()[0], std::string("Gig"));
    ImportOptions none;
    none.noPlaylist = true;
    none.folder = "Other";
    writeWav(src_ / "two.wav", 410);
    QVERIFY(importFolder(lib_, src_, none).ok);
    QCOMPARE(lib_.listPlaylists().size(), size_t(1));
    ImportOptions bad;
    bad.playlist = "../evil";
    ImportReport r = importFolder(lib_, src_, bad);
    QVERIFY(!r.ok);
    QVERIFY(!fs::exists(tmp_ / "lib" / "evil"));
}

void TestTrakcrateImport::cancelStopsCleanly()
{
    writeWav(src_ / "one.wav", 400);
    writeWav(src_ / "two.wav", 410);
    std::atomic<bool> cancel(true);
    ImportReport r = importFolder(lib_, src_, ImportOptions(), nullptr, &cancel);
    QVERIFY(r.cancelled);
    QCOMPARE(r.imported, 0);
    QVERIFY(lib_.listTracks("Acid Loops").empty());

    cancel = false;
    int calls = 0, lastTotal = 0;
    ImportReport r2 = importFolder(lib_, src_, ImportOptions(), [&](int done, int total, const std::string&) {
        ++calls;
        lastTotal = total;
        if (done >= 1) cancel = true;                                // cancel after the first file
    }, &cancel);
    QVERIFY(r2.cancelled);
    QCOMPARE(r2.imported, 1);
    QCOMPARE(lastTotal, 2);
    QVERIFY(calls >= 2);
    QCOMPARE(lib_.listTracks("Acid Loops").size(), size_t(1));       // what was done is complete and consistent
    QCOMPARE(playlistNames(lib_, "Acid Loops").size(), size_t(1));
}

void TestTrakcrateImport::failedFileLeavesNoDebris()
{
    std::ofstream(src_ / "garbage.mp3") << "junk";
    writeWav(src_ / "good.wav", 400);
    ImportReport r = importFolder(lib_, src_, ImportOptions());
    QVERIFY(r.ok);
    QCOMPARE(r.failed, 1);
    QCOMPARE(r.imported, 1);
    QCOMPARE(lib_.listTracks("Acid Loops").size(), size_t(1));       // no empty folder for the bad file
    ImportReport missing = importFolder(lib_, tmp_ / "nope", ImportOptions());
    QVERIFY(!missing.ok);
    QVERIFY(!missing.error.empty());
}

QTEST_APPLESS_MAIN(TestTrakcrateImport)
#include "test_trakcrate_import.moc"
