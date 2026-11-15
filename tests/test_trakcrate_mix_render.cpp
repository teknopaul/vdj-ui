#include <QtTest/QtTest>

#include <QImage>

#include "audio/ladspa_host.h"
#include "trakcrate/audio_probe.h"
#include "trakcrate/edit_output.h"
#include "trakcrate/mix_render.h"
#include "trakcrate/mp3_writer.h"
#include "trakcrate/stretch_probe.h"
#include "ui/mix_filters.h"

#include <cmath>
#include <fstream>
#include <random>

using namespace trakcrate;

namespace {

DecodedPcm noise(double seconds, double amp)
{
    std::mt19937 g(5);
    std::uniform_real_distribution<float> d(-1, 1);
    DecodedPcm p;
    p.stereo.resize(size_t(seconds * 44100) * 2);
    for (float& v : p.stereo) v = float(amp) * d(g);
    return p;
}

std::string slurp(const fs::path& p)
{
    std::string s;
    readFile(p, &s);
    return s;
}

size_t countFiles(const fs::path& d)
{
    size_t n = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(d, ec)) {
        (void)e;
        ++n;
    }
    return n;
}

struct World {
    fs::path root = makeTempDir("mixrender");
    fs::path out;
    std::vector<MixTrackInput> inputs;
    World(int tracks, double seconds = 8, double amp = 0.9)
    {
        out = root / "out";
        for (int i = 0; i < tracks; ++i) {
            const fs::path dir = root / "track-master-copy" / "House" / ("T" + std::to_string(i));
            fs::create_directories(dir);
            DecodedPcm p = probeMaterial(seconds, 120);
            for (float& v : p.stereo) v *= float(amp / 0.9);
            AudioTags tags;
            std::string err;
            writeWav(dir / "track.wav", p, 120, tags, &err);
            KvDoc g;
            BeatGrid grid;
            grid.bpm = 120;
            setBeatGrid(g, grid);
            std::ofstream(dir / "beatgrid.txt") << g.serialise();
            std::ofstream(dir / "waveform.bin") << "x";
            MixTrackInput in;
            in.file = dir / "track.wav";
            in.facts.grid = grid;
            in.facts.frames = (long long)p.frames();
            inputs.push_back(in);
        }
    }
    ~World() { std::error_code ec; fs::remove_all(root, ec); }
    Mix mix(const char* name = "Test mix") const
    {
        Mix m = defaultMix(name, inputs);
        m.author = "Alice";
        m.playlist = "House";
        return m;
    }
};

} // namespace

class TestMixRender : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("XDG_CACHE_HOME", QByteArray((fs::temp_directory_path() / "vdj-mixrender-cache").c_str())); }

    void streamingWriterRoundTrip()
    {
        const fs::path dir = makeTempDir("mp3w");
        const DecodedPcm in = noise(3, 0.3);
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(Qt::red);
        QVERIFY(img.save(QString::fromStdString((dir / "cover.png").string()), "PNG"));
        AudioTags tags;
        tags.tags["title"] = "My mix";
        tags.tags["artist"] = "Alice";
        Mp3Writer w;
        w.setPicture(dir / "cover.png");
        std::string err;
        QVERIFY2(w.open(dir / "a.mp3", 256, true, tags, 44100, &err), err.c_str());
        QVERIFY(fs::exists(dir / "a.mp3.part"));
        for (size_t at = 0; at < in.frames(); at += 5000)                       // odd sized pieces
            QVERIFY(w.write(&in.stereo[at * 2], std::min<size_t>(5000, in.frames() - at), &err));
        QVERIFY2(w.close(&err), err.c_str());
        QVERIFY(!fs::exists(dir / "a.mp3.part"));
        DecodedPcm back;
        QVERIFY2(decodeAudio(dir / "a.mp3", &back, &err), err.c_str());
        QVERIFY(std::llabs((long long)back.frames() - (long long)in.frames()) < 3000);
        AudioInfo info;
        QVERIFY(probeAudio(dir / "a.mp3", &info, &err));
        QCOMPARE(info.tags["title"], std::string("My mix"));
        QCOMPARE(info.tags["artist"], std::string("Alice"));
        QVERIFY(!info.picture.empty());
        const double kbps = double(fs::file_size(dir / "a.mp3")) * 8 / 3.0 / 1000;
        QVERIFY2(kbps > 256 * 0.85 && kbps < 256 * 1.15, qPrintable(QString::number(kbps)));
        // abort leaves nothing
        Mp3Writer b;
        QVERIFY(b.open(dir / "b.mp3", 256, true, tags, 44100, &err));
        b.write(in.stereo.data(), 4000);
        b.abort();
        QVERIFY(!fs::exists(dir / "b.mp3") && !fs::exists(dir / "b.mp3.part"));
        // a bad cover is refused and leaves nothing
        std::ofstream(dir / "bad.png") << "not an image";
        Mp3Writer c;
        c.setPicture(dir / "bad.png");
        QVERIFY(!c.open(dir / "c.mp3", 256, true, tags, 44100, &err));
        QVERIFY(!fs::exists(dir / "c.mp3.part"));
        // constant bitrate still works through writeMp3
        QVERIFY(writeMp3(dir / "d.mp3", in, 128, 120, tags, &err));
        QVERIFY(std::fabs(double(fs::file_size(dir / "d.mp3")) * 8 / 3.0 / 1000 - 128) < 12);
        fs::remove_all(dir);
    }

    void threeTrackMixRendersToTheExpectedLengthWithTagsAndCover()
    {
        World w(3, 8, 0.9);
        Mix m = w.mix();
        for (MixTrack& t : m.tracks) t.trim = 200;                          // loud
        QImage img(32, 32, QImage::Format_RGB32);
        img.fill(Qt::blue);
        QVERIFY(img.save(QString::fromStdString((w.root / "cover.png").string()), "PNG"));
        m.image = "cover.png";
        std::vector<TrackFacts> facts;
        std::string err;
        QVERIFY2(readMixFacts(m, &facts, &err), err.c_str());
        const auto eff = effectiveTracks(m, facts);
        const double lenMs = mixLengthMs(trackStartMs(m, eff), eff);

        std::vector<std::string> stages;
        fs::path file;
        MixRenderOptions opts;
        QVERIFY2(renderMixToMp3(m, w.root, w.out, opts, [&](const std::string& s, double, double) {
                     if (stages.empty() || stages.back() != s) stages.push_back(s);
                 }, nullptr, &err, &file), err.c_str());
        QCOMPARE(file.filename().string(), std::string("Test mix.mp3"));
        QVERIFY(!fs::exists(file.string() + ".part"));
        QCOMPARE(stages.front(), std::string("checking"));
        QCOMPARE(stages.back(), std::string("finishing"));
        DecodedPcm back;
        QVERIFY2(decodeAudio(file, &back, &err), err.c_str());
        QVERIFY2(std::fabs(back.seconds() * 1000 - lenMs) < 70, qPrintable(QString("%1 vs %2").arg(back.seconds() * 1000).arg(lenMs)));
        float peak = 0;
        for (float v : back.stereo) peak = std::max(peak, std::fabs(v));
        QVERIFY2(peak <= 1.15f, qPrintable(QString::number(peak)));          // the mix is brick walled at 0 dBFS; lossy decoding may overshoot a little
        QVERIFY(peak > 0.5f);
        AudioInfo info;
        QVERIFY(probeAudio(file, &info, &err));
        QCOMPARE(info.tags["title"], std::string("Test mix"));
        QCOMPARE(info.tags["artist"], std::string("Alice"));
        QCOMPARE(info.tags["album"], std::string("House"));
        QVERIFY(!info.picture.empty());

        // the same again: a new name (never overwritten), identical bytes (deterministic)
        fs::path file2;
        QVERIFY2(renderMixToMp3(m, w.root, w.out, opts, nullptr, nullptr, &err, &file2), err.c_str());
        QCOMPARE(file2.filename().string(), std::string("Test mix_2.mp3"));
        QVERIFY(slurp(file) == slurp(file2));
        // the track folders were never written to
        QVERIFY(!fs::exists(w.inputs[0].file.parent_path() / "meta.txt"));
    }

    void cancelStopsAndLeavesNoFile()
    {
        World w(3);
        const Mix m = w.mix();
        std::atomic<bool> cancel{false};
        std::string err;
        fs::path file;
        const bool ok = renderMixToMp3(m, w.root, w.out, MixRenderOptions(), [&](const std::string& s, double d, double) {
            if (s == "rendering" && d > 20000) cancel = true;
        }, &cancel, &err, &file);
        QVERIFY(!ok);
        QCOMPARE(err, std::string("cancelled"));
        QCOMPARE(countFiles(w.out), size_t(0));
    }

    void badTracksFailWithAMessageAndWriteNothing()
    {
        World w(3);
        std::string err;
        fs::path file;
        {
            std::error_code ec;
            fs::remove(w.inputs[1].file.parent_path() / "waveform.bin", ec);
            QVERIFY(!renderMixToMp3(w.mix(), w.root, w.out, MixRenderOptions(), nullptr, nullptr, &err, &file));
            QVERIFY2(err.find("Track 2") != std::string::npos && err.find("analysed") != std::string::npos, err.c_str());
            std::ofstream(w.inputs[1].file.parent_path() / "waveform.bin") << "x";
        }
        {
            Mix m = w.mix();
            m.tracks[2].missing = true;
            QVERIFY(!renderMixToMp3(m, w.root, w.out, MixRenderOptions(), nullptr, nullptr, &err, &file));
            QVERIFY2(err.find("Track 3") != std::string::npos, err.c_str());
        }
        {
            Mix m = w.mix();
            m.image = "nope.png";
            QVERIFY(!renderMixToMp3(m, w.root, w.out, MixRenderOptions(), nullptr, nullptr, &err, &file));
            QVERIFY(err.find("cover") != std::string::npos);
        }
        QCOMPARE(countFiles(w.out), size_t(0));
    }

    void masterTempoStretchesThroughTheCache()
    {
        World w(2, 8);
        Mix m = w.mix();
        m.masterBpm = 126;                                                  // 5% faster: both tracks are stretched
        m.stretch.engine = "varispeed";
        std::string err;
        fs::path file;
        std::vector<std::string> stages;
        QVERIFY2(renderMixToMp3(m, w.root, w.out, MixRenderOptions(), [&](const std::string& s, double, double) {
                     if (stages.empty() || stages.back() != s) stages.push_back(s);
                 }, nullptr, &err, &file), err.c_str());
        bool saw = false;
        for (const auto& s : stages) saw = saw || s.rfind("stretching track 2 of 2", 0) == 0;
        QVERIFY(saw);
        std::vector<TrackFacts> facts;
        QVERIFY(readMixFacts(m, &facts, &err));
        const auto eff = effectiveTracks(m, facts);
        DecodedPcm back;
        QVERIFY(decodeAudio(file, &back, &err));
        QVERIFY(std::fabs(back.seconds() * 1000 - mixLengthMs(trackStartMs(m, eff), eff)) < 70);
    }

    void filterProcessorsAreApplied()
    {
        World w(1, 4, 0.5);
        Mix m = w.mix("plain");
        m.filters.clear();
        std::string err;
        fs::path plain, halved;
        QVERIFY2(renderMixToMp3(m, w.root, w.out, MixRenderOptions(), nullptr, nullptr, &err, &plain), err.c_str());
        MixRenderOptions opts;
        int calls = 0;
        opts.ladspaFactory = [&] {
            return std::vector<MixProcessor>{[&calls](float* s, int n) {
                ++calls;
                for (int i = 0; i < n * 2; ++i) s[i] *= 0.5f;
            }};
        };
        m.name = "halved";
        QVERIFY2(renderMixToMp3(m, w.root, w.out, opts, nullptr, nullptr, &err, &halved), err.c_str());
        QVERIFY(calls > 0);
        DecodedPcm a, b;
        QVERIFY(decodeAudio(plain, &a, &err));
        QVERIFY(decodeAudio(halved, &b, &err));
        double ra = 0, rb = 0;
        for (size_t i = 0; i < std::min(a.stereo.size(), b.stereo.size()); ++i) {
            ra += double(a.stereo[i]) * a.stereo[i];
            rb += double(b.stereo[i]) * b.stereo[i];
        }
        QVERIFY(std::fabs(10 * std::log10(rb / ra) + 6.02) < 0.5);
    }

    void ladspaProcessorsFromTheModel()
    {
        std::vector<MixFilter> f;
        MixFilter gone;
        gone.kind = MixFilterKind::Ladspa;
        gone.plugin = "nothing.so|nothing";
        f.push_back(gone);
        MixFilter off = gone;
        off.on = false;
        f.push_back(off);
        f.push_back(defaultFilters().front());
        MixFilterReport rep;
        QCOMPARE(buildLadspaProcessors(f, &rep).size(), size_t(0));
        QCOMPARE(rep.skipped.size(), size_t(1));                            // the missing plugin; the disabled and built-in ones are silently left out
        const auto& plugins = vdj::LadspaHost::plugins();
        if (plugins.empty()) QSKIP("no LADSPA plugins installed");
        MixFilter real;
        real.kind = MixFilterKind::Ladspa;
        real.plugin = plugins.front().key.toStdString();
        MixFilterReport rep2;
        const auto procs = buildLadspaProcessors({real}, &rep2);
        QCOMPARE(procs.size(), size_t(1));
        std::vector<float> buf(2048, 0.1f);
        procs[0](buf.data(), 1024);                                         // runs without crashing
        QVERIFY(true);
    }
};

QTEST_MAIN(TestMixRender)
#include "test_trakcrate_mix_render.moc"
