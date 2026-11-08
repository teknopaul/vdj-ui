#include <QtTest/QtTest>

#include <QStandardPaths>

#include "audio/wav_info.h"
#include "config/config_manager.h"
#include "trakcrate/audio_probe.h"
#include "trakcrate/edit_library.h"
#include "trakcrate/edit_output.h"
#include "trakcrate/importer.h"
#include "ui/edit_prefs.h"

#include <cmath>
#include <fstream>

using namespace trakcrate;

namespace {

DecodedPcm sine(double seconds, double hz = 220)
{
    DecodedPcm p;
    const size_t n = size_t(seconds * p.sampleRate);
    p.stereo.resize(n * 2);
    for (size_t i = 0; i < n; ++i) {
        p.stereo[i * 2] = float(0.4 * std::sin(2 * M_PI * hz * double(i) / p.sampleRate));
        p.stereo[i * 2 + 1] = float(0.3 * std::sin(2 * M_PI * hz * 1.5 * double(i) / p.sampleRate));
    }
    return p;
}

std::string slurp(const fs::path& p)
{
    std::string s;
    readFile(p, &s);
    return s;
}

// Every file of a folder tree with its bytes, to prove nothing changed.
std::map<std::string, std::string> snapshot(const fs::path& dir)
{
    std::map<std::string, std::string> m;
    for (const auto& e : fs::recursive_directory_iterator(dir))
        if (e.is_regular_file()) m[e.path().string()] = slurp(e.path());
    return m;
}

} // namespace

class TestEditOutput : public QObject
{
    Q_OBJECT
    fs::path tmp_;

private slots:
    void init()
    {
        tmp_ = makeTempDir("edit-output-test");
        QVERIFY(!tmp_.empty());
    }
    void cleanup()
    {
        std::error_code ec;
        fs::remove_all(tmp_, ec);
    }

    void titles()
    {
        QCOMPARE(editTitle("Foo (Original Mix)", "Alice"), std::string("Foo (Alice Edit)"));
        QCOMPARE(editTitle("Foo (original mix)  ", "Alice"), std::string("Foo (Alice Edit)"));
        QCOMPARE(editTitle("Foo", "Alice"), std::string("Foo (Alice Edit)"));
        QCOMPARE(editTitle("Foo (Radio Edit)", "Alice"), std::string("Foo (Radio Edit) (Alice Edit)"));
        QCOMPARE(editTitle("Foo (Alice Edit)", "Alice"), std::string("Foo (Alice Edit)"));
        QCOMPARE(editTitle("", "Alice"), std::string());
        QCOMPARE(loopTitle("Foo (Original Mix)", "Bass", "Alice"), std::string("Foo (Bass Alice Edit)"));
        QCOMPARE(fileUser("Alice B"), std::string("alice-b"));
        QCOMPARE(fileUser("../x"), std::string("x"));
        QCOMPARE(fileUser("  "), std::string("user"));
    }

    void uniqueNames()
    {
        const fs::path a = uniqueEditPath(tmp_, "Song", "Alice", "wav");
        QCOMPARE(a.filename().string(), std::string("Song_alice_edit.wav"));
        std::ofstream(a) << "x";
        const fs::path b = uniqueEditPath(tmp_, "Song", "Alice", "wav");
        QCOMPARE(b.filename().string(), std::string("Song_alice_edit_2.wav"));
        std::ofstream(tmp_ / "Song_alice_edit.txt") << "x";                    // the info file counts too, whatever the extension
        std::ofstream(tmp_ / "Song_alice_edit_2.txt") << "x";
        QCOMPARE(uniqueEditPath(tmp_, "Song", "Alice", "mp3").filename().string(), std::string("Song_alice_edit_3.mp3"));
        QCOMPARE(slurp(a), std::string("x"));                                   // never touched
    }

    void wavRoundTrip()
    {
        const DecodedPcm p = sine(2.0);
        AudioTags tags;
        tags.tags["title"] = "Foo (Alice Edit)";
        tags.tags["artist"] = "Bar";
        const fs::path f = tmp_ / "a.wav";
        std::string err;
        QVERIFY2(writeWav(f, p, 123.5, tags, &err, 8), err.c_str());
        QVERIFY(!fs::exists(tmp_ / "a.wav.part"));
        QCOMPARE(vdj::wavBpmFromFile(QString::fromStdString(f.string())), 123.5);
        DecodedPcm q;
        QVERIFY2(decodeAudio(f, &q, &err), err.c_str());
        QCOMPARE(q.frames(), p.frames());
        for (size_t i = 0; i < p.stereo.size(); i += 101) QVERIFY(std::fabs(p.stereo[i] - q.stereo[i]) < 2.0f / 32767);
        AudioTags back;
        QVERIFY(readAudioTags(f, &back));
        QCOMPARE(back.get("title"), std::string("Foo (Alice Edit)"));
        QCOMPARE(back.get("artist"), std::string("Bar"));
        // No tempo, no acid chunk.
        QVERIFY(writeWav(tmp_ / "b.wav", p, 0, tags, &err));
        QCOMPARE(vdj::wavBpmFromFile(QString::fromStdString((tmp_ / "b.wav").string())), 0.0);
    }

    void mp3Cbr()
    {
        const DecodedPcm p = sine(3.0);
        AudioTags tags;
        tags.tags["title"] = "Foo (Alice Edit)";
        tags.tags["artist"] = "Bar";
        tags.tags["album"] = "Baz";
        const fs::path f = tmp_ / "a.mp3";
        std::string err;
        QVERIFY2(writeMp3(f, p, 192, 126.0, tags, &err), err.c_str());
        QVERIFY(!fs::exists(tmp_ / "a.mp3.part"));
        DecodedPcm q;
        QVERIFY2(decodeAudio(f, &q, &err), err.c_str());
        QVERIFY(std::llabs((long long)q.frames() - (long long)p.frames()) < 3000);      // encoder delay and the last frame
        AudioInfo info;
        QVERIFY(probeAudio(f, &info, &err));
        QVERIFY(std::labs(info.bitrate - 192000) < 3000);
        QCOMPARE(info.sampleRate, 44100);
        QCOMPARE(info.channels, 2);
        QCOMPARE(info.tags["title"], std::string("Foo (Alice Edit)"));
        QCOMPARE(info.tags["artist"], std::string("Bar"));
        QCOMPARE(info.tags["album"], std::string("Baz"));
        const std::string bpm = info.tags.count("tbpm") ? info.tags["tbpm"] : info.tags["bpm"];
        QCOMPARE(bpm, std::string("126"));
        // CBR: every frame the same size, so the file size follows the bitrate.
        const double secs = double(q.frames()) / 44100;
        QVERIFY(std::fabs(double(fs::file_size(f)) * 8 / secs / 1000 - 192) < 8);
        // 320 is 320.
        QVERIFY(writeMp3(tmp_ / "c.mp3", p, 320, 0, tags, &err));
        AudioInfo i2;
        QVERIFY(probeAudio(tmp_ / "c.mp3", &i2, &err));
        QVERIFY(std::labs(i2.bitrate - 320000) < 3000);
    }

    void infoFile()
    {
        EditInfo i;
        i.name = "Foo_alice_edit.wav";
        i.sourceFile = "Foo.mp3";
        i.author = "Alice";
        i.timestamp = "2026-11-08T12:00:00";
        i.sourceBpm = 128;
        i.bpm = 128;
        i.firstBeatMs = 25.5;
        i.durationSec = 301.25;
        EditRegion cut;
        cut.startBeat = 32;
        cut.lenBeats = 16;
        cut.algo = EditAlgo::Cross;
        cut.algoParam = 1;
        EditRegion loop;
        loop.kind = EditKind::Loop;
        loop.startBeat = 64;
        loop.lenBeats = 4;
        loop.loopTimes = 4;
        loop.algo = EditAlgo::Fadeout;
        loop.algoParam = 0.5;
        i.regions = {describeRegion(1, cut), describeRegion(2, loop)};
        QCOMPARE(i.regions[0], std::string("#1 cut bars 9-12 algo=cross 1 beat"));
        QCOMPARE(i.regions[1], std::string("#2 loop bars 17-17 x4 algo=fadeout 0.5 beat"));
        EditRegion odd = loop;
        odd.startBeat = 65;
        QCOMPARE(describeRegion(3, odd), std::string("#3 loop beats 66-69 x4 algo=fadeout 0.5 beat"));
        std::string err;
        QVERIFY2(writeInfoFile(tmp_ / "i.txt", i, &err), err.c_str());
        const std::string s = slurp(tmp_ / "i.txt");
        QVERIFY(s.find("author: Alice") != std::string::npos);
        QVERIFY(s.find("bpm: 128") != std::string::npos);
        QVERIFY(s.find("  #1 cut bars 9-12 algo=cross 1 beat") != std::string::npos);
        QVERIFY(s.find("  #2 loop bars 17-17") != std::string::npos);
    }

    void libraryImport()
    {
        Library lib;
        QVERIFY(Library::create(tmp_ / "lib") && Library::open(tmp_ / "lib", &lib));
        const fs::path src = tmp_ / "src" / "Set";
        fs::create_directories(src);
        std::string err;
        for (const char* n : {"a", "b", "c"}) QVERIFY(writeWav(src / (std::string(n) + ".wav"), sine(4.0, 200 + n[0]), 0, AudioTags(), &err));
        ImportOptions io;
        const ImportReport rep = importFolder(lib, src, io);
        QCOMPARE(rep.imported, 3);
        std::vector<fs::path> order = lib.playlistTracks("Set");
        QCOMPARE(int(order.size()), 3);
        const fs::path original = order[1];                                     // b
        const auto before = snapshot(original);

        // the rendered edit and its info file live outside the library
        BeatGrid grid;
        grid.bpm = 120;
        grid.firstBeatUs = 25000;
        AudioTags tags;
        tags.tags["title"] = "B (Alice Edit)";
        const fs::path rendered = tmp_ / "work" / "b_alice_edit.wav";
        fs::create_directories(rendered.parent_path());
        QVERIFY(writeWav(rendered, sine(3.0), grid.bpm, tags, &err));
        EditInfo ei;
        ei.name = "b_alice_edit.wav";
        QVERIFY(writeInfoFile(tmp_ / "work" / "b_alice_edit.txt", ei, &err));

        EditImportResult r = importEditToLibrary(lib, original, rendered, tmp_ / "work" / "b_alice_edit.txt", grid);
        QVERIFY2(r.ok, r.error.c_str());
        QCOMPARE(r.trackDir.parent_path().filename().string(), std::string("Set"));
        QCOMPARE(r.playlist, std::string("Set"));
        QCOMPARE(r.playlistIndex, 1);
        QVERIFY(fs::exists(r.trackDir / "waveform.bin"));
        QVERIFY(fs::exists(r.trackDir / "edit.txt"));
        Track t;
        QVERIFY(loadTrack(r.trackDir, &t, &err));
        QCOMPARE(t.title(), std::string("B (Alice Edit)"));
        QVERIFY(t.hasGrid);
        const BeatGrid g = beatGridFrom(t.grid);
        QCOMPARE(g.bpm, 120.0);
        QCOMPARE(g.firstBeatUs, 25000LL);
        QCOMPARE(origin(t.source, "beatgrid"), std::string("edited"));
        QCOMPARE(t.source.get("edit_of"), original.filename().string());

        // a, edit, b, c
        order = lib.playlistTracks("Set");
        QCOMPARE(int(order.size()), 4);
        QCOMPARE(order[1].filename().string(), r.trackDir.filename().string());
        QCOMPARE(order[2].filename().string(), original.filename().string());
        QCOMPARE(snapshot(original), before);                                  // the original is byte identical

        // a second edit is a distinct track
        const fs::path rendered2 = tmp_ / "work2" / "b_alice_edit.wav";
        fs::create_directories(rendered2.parent_path());
        QVERIFY(writeWav(rendered2, sine(3.0), grid.bpm, tags, &err));
        EditImportResult r2 = importEditToLibrary(lib, original, rendered2, fs::path(), grid);
        QVERIFY2(r2.ok, r2.error.c_str());
        QVERIFY(r2.trackDir != r.trackDir);
        QVERIFY(!fs::exists(r2.trackDir / "edit.txt"));
        QCOMPARE(snapshot(original), before);
    }

    void libraryWithoutMatchingPlaylist()
    {
        Library lib;
        QVERIFY(Library::create(tmp_ / "lib") && Library::open(tmp_ / "lib", &lib));
        const fs::path src = tmp_ / "src" / "Set";
        fs::create_directories(src);
        std::string err;
        QVERIFY(writeWav(src / "a.wav", sine(4.0), 0, AudioTags(), &err));
        ImportOptions io;
        io.noPlaylist = true;
        QCOMPARE(importFolder(lib, src, io).imported, 1);
        const fs::path original = lib.trackDir("Set", lib.listTracks("Set").front());
        BeatGrid grid;
        grid.bpm = 100;
        const fs::path rendered = tmp_ / "work" / "a_x_edit.wav";
        fs::create_directories(rendered.parent_path());
        QVERIFY(writeWav(rendered, sine(3.0), 100, AudioTags(), &err));
        EditImportResult r = importEditToLibrary(lib, original, rendered, fs::path(), grid);
        QVERIFY2(r.ok, r.error.c_str());
        QVERIFY(r.playlist.empty());
        QCOMPARE(r.playlistIndex, -1);
        QVERIFY(!r.note.empty());
        QVERIFY(!lib.hasPlaylist("Set"));                                       // never created

        // a playlist of that name that lacks the original: not added
        QVERIFY(lib.createPlaylist("Set"));
        const fs::path rendered2 = tmp_ / "work2" / "a_x_edit.wav";
        fs::create_directories(rendered2.parent_path());
        QVERIFY(writeWav(rendered2, sine(3.0), 100, AudioTags(), &err));
        EditImportResult r2 = importEditToLibrary(lib, original, rendered2, fs::path(), grid);
        QVERIFY2(r2.ok, r2.error.c_str());
        QVERIFY(r2.playlist.empty());
        QVERIFY(lib.playlistTracks("Set").empty());
    }

    void prefs()
    {
        QStandardPaths::setTestModeEnabled(true);
        vdj::ConfigManager cm;
        cm.clearSection(QStringLiteral("edit"));
        vdj::EditPrefs p(&cm);
        QCOMPARE(p.format(), QStringLiteral("wav"));
        QCOMPARE(p.mp3Kbps(), 256);
        QVERIFY(!p.author().isEmpty());
        const QString from = QString::fromStdString((tmp_ / "orig").string());
        QDir().mkpath(from);
        const QString importedFrom = from + QStringLiteral("/song.mp3");
        QCOMPARE(p.folder(importedFrom), from);                                  // where the original came from
        QCOMPARE(p.loopFolder(importedFrom), from);
        QVERIFY(QFileInfo(p.folder()).isDir());                                  // Music or home
        p.setFormat(QStringLiteral("mp3"));
        p.setMp3Kbps(320);
        p.setAuthor(QStringLiteral("  Alice "));
        const QString chosen = QString::fromStdString((tmp_ / "chosen").string());
        QDir().mkpath(chosen);
        p.setFolder(chosen);
        vdj::EditPrefs again(&cm);                                               // remembered
        QCOMPARE(again.format(), QStringLiteral("mp3"));
        QCOMPARE(again.mp3Kbps(), 320);
        QCOMPARE(again.author(), QStringLiteral("Alice"));
        QCOMPARE(again.folder(importedFrom), chosen);
        QCOMPARE(again.loopFolder(importedFrom), chosen);                        // follows the folder until set on its own
        const QString loops = QString::fromStdString((tmp_ / "loops").string());
        QDir().mkpath(loops);
        again.setLoopFolder(loops);
        QCOMPARE(again.loopFolder(importedFrom), loops);
        QCOMPARE(again.folder(importedFrom), chosen);
        QDir(chosen).removeRecursively();                                        // a vanished folder falls back
        QCOMPARE(again.folder(importedFrom), from);
        cm.clearSection(QStringLiteral("edit"));
    }
};

QTEST_GUILESS_MAIN(TestEditOutput)
#include "test_trakcrate_edit_output.moc"
