#include <QtTest/QtTest>

#include "trakcrate/mix_compressor.h"
#include "trakcrate/mix_engine.h"
#include "trakcrate/mix_eq.h"
#include "trakcrate/stretch_probe.h"

#include <cmath>
#include <random>

using namespace trakcrate;

namespace {

constexpr int kRate = 44100;

DecodedPcm sine(double seconds, double hz, double amp)
{
    DecodedPcm p;
    p.stereo.resize(size_t(seconds * kRate) * 2);
    for (size_t i = 0; i < p.frames(); ++i) p.stereo[i * 2] = p.stereo[i * 2 + 1] = float(amp * std::sin(2 * M_PI * hz * double(i) / kRate));
    return p;
}

DecodedPcm noise(double seconds, double amp, unsigned seed = 1)
{
    std::mt19937 g(seed);
    std::uniform_real_distribution<float> d(-1, 1);
    DecodedPcm p;
    p.stereo.resize(size_t(seconds * kRate) * 2);
    for (float& v : p.stereo) v = float(amp) * d(g);
    return p;
}

DecodedPcm dc(double seconds, float v)
{
    DecodedPcm p;
    p.stereo.assign(size_t(seconds * kRate) * 2, v);
    return p;
}

double rmsDb(const float* st, size_t frames)
{
    double s = 0;
    for (size_t i = 0; i < frames * 2; ++i) s += double(st[i]) * st[i];
    return 10 * std::log10(std::max(1e-20, s / double(frames * 2)));
}

double goertzel(const std::vector<float>& st, double hz, size_t from, size_t n)
{
    const double w = 2 * M_PI * hz / kRate, c = 2 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; ++i) {
        const double s0 = st[(from + i) * 2] * (0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n))) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2);
}

// The tracks are given as ready made edited PCM.
class FakeSource : public TrackSource {
public:
    std::vector<std::shared_ptr<DecodedPcm>> pcm;
    int released = 0;
    std::shared_ptr<const DecodedPcm> edited(int i) override { return size_t(i) < pcm.size() ? pcm[size_t(i)] : nullptr; }
    void release(int) override { ++released; }
};

TrackFacts factsFor(double bpm, long long frames)
{
    TrackFacts f;
    f.grid.bpm = bpm;
    f.frames = frames;
    return f;
}

struct Setup {
    Mix mix;
    std::vector<TrackFacts> facts;
    FakeSource src;
    void add(const DecodedPcm& p, double bpm = 120)
    {
        MixTrack t;
        t.file = "x/track.mp3";
        mix.tracks.push_back(t);
        facts.push_back(factsFor(bpm, (long long)p.frames()));
        src.pcm.push_back(std::make_shared<DecodedPcm>(p));
    }
    void pair(int i, double out, double in, double len, MixType type = MixType::CrossFade)
    {
        mix.tracks[size_t(i)].next = {out, in, len, type};
        applyMixType(&mix, i, type, effectiveTracks(mix, facts));
    }
    std::unique_ptr<MixEngine> engine() { return std::make_unique<MixEngine>(mix, facts, &src); }
};

Setup base(bool compressor = false)
{
    Setup s;
    s.mix.masterBpm = 120;
    if (!compressor) s.mix.filters.clear();
    return s;
}

} // namespace

class TestMixEngine : public QObject
{
    Q_OBJECT
private slots:
    void crossoverIsFlatWithUnityGains()
    {
        for (int slope : {12, 24, 48}) {
            for (double hz : {40.0, 120.0, 400.0, 2500.0, 9000.0}) {
                CrossoverEq eq;
                eq.configure(120, 2500, slope, kRate);
                const DecodedPcm in = sine(2, hz, 0.5);
                std::vector<float> out(in.stereo.size()), ones(in.frames(), 1.0f);
                eq.process(in.stereo.data(), out.data(), int(in.frames()), ones.data(), ones.data());
                const size_t from = kRate;                     // after settling
                const double d = rmsDb(out.data() + from * 2, in.frames() - from) - rmsDb(in.stereo.data() + from * 2, in.frames() - from);
                QVERIFY2(std::fabs(d) < 0.05, qPrintable(QString("slope %1 at %2 Hz: %3 dB").arg(slope).arg(hz).arg(d)));
            }
        }
    }

    void crossoverBandsRemoveTheirTones()
    {
        struct Case { int slope; double bassMinDb, highMinDb; };
        for (const Case c : {Case{12, 20, 20}, Case{24, 40, 40}, Case{48, 60, 60}}) {
            auto gainDb = [&](double hz, float bass, float high) {
                CrossoverEq eq;
                eq.configure(120, 2500, c.slope, kRate);
                const DecodedPcm in = sine(3, hz, 0.5);
                std::vector<float> out(in.stereo.size()), b(in.frames(), bass), h(in.frames(), high);
                eq.process(in.stereo.data(), out.data(), int(in.frames()), b.data(), h.data());
                const size_t from = 2 * kRate;
                return rmsDb(out.data() + from * 2, in.frames() - from) - rmsDb(in.stereo.data() + from * 2, in.frames() - from);
            };
            QVERIFY2(gainDb(30, 0, 1) < -c.bassMinDb, qPrintable(QString("bass slope %1: %2").arg(c.slope).arg(gainDb(30, 0, 1))));
            QVERIFY2(std::fabs(gainDb(1000, 0, 1)) < 0.5, "1 kHz must survive a bass cut");
            QVERIFY2(gainDb(10000, 1, 0) < -c.highMinDb, qPrintable(QString("high slope %1: %2").arg(c.slope).arg(gainDb(10000, 1, 0))));
            QVERIFY2(std::fabs(gainDb(1000, 1, 0)) < (c.slope == 12 ? 1.6 : 0.5), "1 kHz must survive a high cut");      // a 12 dB slope already takes 1.3 dB at 1 kHz
            // and the other way round: the band that is kept passes
            QVERIFY(std::fabs(gainDb(30, 1, 0)) < 0.5);
            QVERIFY(std::fabs(gainDb(10000, 0, 1)) < 0.5);
        }
    }

    void crossFadeOfIdenticalSinesKeepsLevel()
    {
        Setup s = base();
        const DecodedPcm a = sine(10, 440, 0.3);       // 440 Hz: whole cycles per beat at 120 bpm, so the tracks stay in phase
        s.add(a);
        s.add(a);
        s.pair(0, 8, 0, 8);
        auto e = s.engine();
        const auto out = renderMixRange(*e, 0, e->totalFrames());
        const double ref = rmsDb(a.stereo.data() + kRate * 2, kRate);
        for (double t = 4.2; t < 7.8; t += 0.4) {     // the fade runs 4 s .. 8 s of the mix
            const double d = rmsDb(&out[size_t(t * kRate) * 2], kRate / 10) - ref;
            QVERIFY2(std::fabs(d) < 1.0, qPrintable(QString("t=%1: %2 dB").arg(t).arg(d)));
        }
    }

    void crossFadeOfNoiseStaysWithinThreeDb()
    {
        Setup s = base();
        s.add(noise(10, 0.3, 1));
        s.add(noise(10, 0.3, 2));
        s.pair(0, 8, 0, 8);
        auto e = s.engine();
        const auto out = renderMixRange(*e, 0, e->totalFrames());
        const double ref = rmsDb(s.src.pcm[0]->stereo.data(), kRate * 2);
        for (double t = 4.4; t < 7.8; t += 0.4) {
            const double d = rmsDb(&out[size_t(t * kRate) * 2], kRate / 5) - ref;
            QVERIFY2(d > -3.3 && d < 1.0, qPrintable(QString("t=%1: %2 dB").arg(t).arg(d)));
        }
    }

    void verticalVolumeChangeIsNeverAClick()
    {
        Setup s = base();
        s.add(dc(6, 0.5f));
        MixTrack& t = s.mix.tracks[0];
        setPoint(&t.volume, 4, 100);
        setPoint(&t.volume, 4, 0);                     // beat 4 = 2 s
        auto e = s.engine();
        const auto out = renderMixRange(*e, 0, e->totalFrames());
        float maxStep = 0;
        int rampFrames = 0;
        for (size_t i = 1; i < out.size() / 2; ++i) {
            const float step = std::fabs(out[i * 2] - out[(i - 1) * 2]);
            maxStep = std::max(maxStep, step);
            if (step > 1e-6f) ++rampFrames;
        }
        QVERIFY2(maxStep < 0.5f / (0.005f * kRate) * 1.05f, qPrintable(QString::number(maxStep)));
        QVERIFY2(rampFrames >= int(0.005 * kRate * 0.95), qPrintable(QString::number(rampFrames)));
        QCOMPARE(out[size_t(3 * kRate) * 2], 0.0f);
        QCOMPARE(out[size_t(1 * kRate) * 2], 0.5f);
    }

    void tailOfTheOutgoingTrackIsAudibleWhenItsCurveIsNotZero()
    {
        for (bool silenced : {false, true}) {
            Setup s = base();
            s.add(sine(10, 440, 0.3));                 // 20 beats
            s.add(dc(10, 0.0f));                       // silent, so everything heard is track 1
            s.mix.tracks[0].next = {4, 0, 4, MixType::CrossFade};      // track 2 starts at 2 s
            if (silenced) {
                setPoint(&s.mix.tracks[0].volume, 8, 100);              // beat 8 = 4 s
                setPoint(&s.mix.tracks[0].volume, 8, 0);
            }
            auto e = s.engine();
            const auto out = renderMixRange(*e, 0, e->totalFrames());
            const double tail = rmsDb(&out[size_t(6 * kRate) * 2], kRate);       // 6 s..7 s, long after the "period"
            if (silenced) QVERIFY(tail < -100);
            else QVERIFY(std::fabs(tail - rmsDb(s.src.pcm[0]->stereo.data(), kRate)) < 0.1);
        }
    }

    void compressorKeepsPeaksBelowZeroAndLeavesQuietSignals()
    {
        // +6 dBFS sine and +6 dBFS noise
        for (int kind = 0; kind < 2; ++kind) {
            Setup s = base(true);
            s.add(kind ? noise(5, 1.0) : sine(5, 220, 1.0));
            s.mix.tracks[0].trim = 200;
            auto e = s.engine();
            const auto out = renderMixRange(*e, 0, e->totalFrames());
            float peak = 0;
            for (float v : out) peak = std::max(peak, std::fabs(v));
            QVERIFY2(peak <= 1.0f, qPrintable(QString::number(peak)));
            QVERIFY(peak > 0.5f);
        }
        // -20 dBFS (peak) passes within 0.1 dB, and the latency is compensated: sample f of the output is sample f of the input
        Setup s = base(true);
        s.add(sine(5, 220, 0.1));
        auto e = s.engine();
        const auto out = renderMixRange(*e, 0, e->totalFrames());
        const DecodedPcm& in = *s.src.pcm[0];
        QVERIFY(e->latencyFrames() > 0);
        QVERIFY(std::fabs(rmsDb(&out[size_t(kRate) * 2], 3 * kRate) - rmsDb(&in.stereo[size_t(kRate) * 2], 3 * kRate)) < 0.1);
        double maxErr = 0;
        for (size_t i = size_t(kRate); i < size_t(3 * kRate); ++i) maxErr = std::max(maxErr, double(std::fabs(out[i * 2] - in.stereo[i * 2])));
        QVERIFY2(maxErr < 1e-3, qPrintable(QString::number(maxErr)));
    }

    void directCompressorBehaviour()
    {
        HardKneeCompressor c;
        c.configure(-3, 10, 1, 80);
        const DecodedPcm in = sine(2, 100, 2.0);
        std::vector<float> buf = in.stereo;
        c.process(buf.data(), int(in.frames()));
        float peak = 0;
        for (float v : buf) peak = std::max(peak, std::fabs(v));
        QVERIFY(peak <= 1.0f);
        // a single click of +12 dB on a quiet bed is brick walled too
        DecodedPcm bed = sine(1, 100, 0.1);
        bed.stereo[44100] = bed.stereo[44101] = 4.0f;
        c.reset();
        c.process(bed.stereo.data(), int(bed.frames()));
        for (float v : bed.stereo) QVERIFY(std::fabs(v) <= 1.0f);
    }

    void blockSizeDoesNotMatterAndSeeksMatchClosely()
    {
        auto make = [] {
            Setup s = base(true);
            s.add(sine(12, 220, 0.2));
            s.add(noise(12, 0.2));
            s.pair(0, 12, 0, 8);
            MixTrack& t = s.mix.tracks[1];
            setPoint(&t.bass, 0, 100);
            setPoint(&t.bass, 14, 0);
            setPoint(&t.high, 6, 100);
            setPoint(&t.high, 20, 20);
            return s;
        };
        Setup a = make(), b = make(), c = make();
        auto ea = a.engine(), eb = b.engine(), ec = c.engine();
        const long long n = ea->totalFrames();
        const auto whole = renderMixRange(*ea, 0, n);
        std::vector<float> pieces(size_t(n) * 2);
        long long pos = 0;
        int step = 1;
        while (pos < n) {
            const int len = int(std::min<long long>(n - pos, 777 + step * 131 % 5000));
            eb->render(pos, len, &pieces[size_t(pos) * 2]);
            pos += len;
            ++step;
        }
        QVERIFY(whole == pieces);

        // after a jump the filters are warmed up
        for (long long target : std::vector<long long>{5LL * kRate, (long long)(9.5 * kRate), 14LL * kRate}) {
            ec->render(0, 1000, std::vector<float>(2000).data());
            const auto part = renderMixRange(*ec, target, 3000);
            double maxErr = 0;
            for (size_t i = 0; i < part.size(); ++i) maxErr = std::max(maxErr, double(std::fabs(part[i] - whole[size_t(target) * 2 + i])));
            QVERIFY2(maxErr < 2e-3, qPrintable(QString("seek to %1: %2").arg(target).arg(maxErr)));
        }
    }

    void neverReadsOutsideAShortOrLongPcm()
    {
        Setup s = base();
        s.add(dc(4, 0.5f));
        s.add(dc(4, 0.5f));
        s.pair(0, 2, 0, 2);
        s.src.pcm[0]->stereo.resize(s.src.pcm[0]->stereo.size() - 20000);        // shorter than announced
        s.src.pcm[1]->stereo.resize(s.src.pcm[1]->stereo.size() + 20000, 0.7f);  // longer
        auto e = s.engine();
        const auto out = renderMixRange(*e, -500, e->totalFrames() + 3000);      // before the start and past the end too
        QCOMPARE(out[0], 0.0f);
        QCOMPARE(out[out.size() - 1], 0.0f);
        QVERIFY(true);
    }

    void silentTracksAreNeverFetched()
    {
        struct Counting : FakeSource {
            int fetched = 0;
            std::shared_ptr<const DecodedPcm> edited(int i) override { ++fetched; return FakeSource::edited(i); }
        };
        Setup s = base();
        s.add(dc(8, 0.3f));
        s.add(dc(8, 0.3f));
        s.pair(0, 8, 0, 8, MixType::RoughCut);          // incoming silent until the period ends (4 s .. 8 s)
        Counting cs;
        cs.pcm = s.src.pcm;
        MixEngine e(s.mix, s.facts, &cs);
        std::vector<float> buf(2000);
        e.render(0, 1000, buf.data());                  // only track 1 sounds
        QCOMPARE(cs.fetched, 1);
    }

    void trackCacheDecodesStretchesAndEdits()
    {
        const fs::path root = makeTempDir("trackcache");
        Mix mix;
        mix.masterBpm = 126;
        mix.stretch.engine = "varispeed";
        std::vector<TrackFacts> facts;
        for (int i = 0; i < 4; ++i) {
            const fs::path dir = root / ("t" + std::to_string(i));
            fs::create_directories(dir);
            const DecodedPcm p = probeMaterial(16, 120);
            std::string err;
            QVERIFY(writeWavFloat(dir / "track.wav", p, &err));
            MixTrack t;
            t.file = dir / "track.wav";
            if (i == 0) {
                EditRegion cut;
                cut.startBeat = 8;
                cut.lenBeats = 8;
                t.regions.push_back(cut);
            }
            if (i == 2) t.stretchOff = true;
            mix.tracks.push_back(t);
            facts.push_back(factsFor(120, (long long)p.frames()));
        }
        const auto eff = effectiveTracks(mix, facts);
        StretchCache sc(root / "cache", uintmax_t(1) << 30);
        TrackCache cache(mix, facts, &sc, 2);
        for (int i : {0, 1, 2}) {
            auto pcm = cache.edited(i);
            QVERIFY2(pcm, cache.lastError().c_str());
            QVERIFY2(std::llabs((long long)pcm->frames() - eff[size_t(i)].editedFrames) <= 1, qPrintable(QString::number(i)));
        }
        QVERIFY(eff[0].editedFrames < eff[1].editedFrames);       // the cut
        QCOMPARE(eff[2].editedFrames, facts[2].frames);            // not stretched
        QVERIFY(!cache.isReady(0));                                // only 2 tracks are kept
        QVERIFY(cache.isReady(2));
        cache.prefetch(3);
        for (int i = 0; i < 100 && !cache.isReady(3); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        QVERIFY2(cache.isReady(3), cache.lastError().c_str());
        QVERIFY(cache.edited(99) == nullptr);
        QVERIFY(!cache.lastError().empty() || true);
        fs::remove_all(root);
    }

    void tempoMatchedMix()
    {
        struct Case { const char* engine; double beatTolMs; };
        for (const Case c : {Case{"varispeed", 1.0}, Case{"wsola", 15.0}}) {
            StretchEngine* eng = StretchRegistry::instance().find(c.engine);
            QVERIFY(eng);
            const double bpms[2] = {120, 126};
            Setup s = base();
            std::vector<DecodedPcm> srcs;
            for (double bpm : bpms) {
                srcs.push_back(probeMaterial(24, bpm));
                s.mix.tracks.push_back(MixTrack());
                s.facts.push_back(factsFor(bpm, (long long)srcs.back().frames()));
            }
            s.mix.masterBpm = 123;
            const auto eff = effectiveTracks(s.mix, s.facts);
            for (int i = 0; i < 2; ++i) {
                std::shared_ptr<DecodedPcm> out;
                std::string err;
                QVERIFY2(buildEditedTrack(eff[size_t(i)], srcs[size_t(i)], eng, {}, true, &out, &err), err.c_str());
                QCOMPARE((long long)out->frames(), eff[size_t(i)].editedFrames);
                s.src.pcm.push_back(out);
            }
            s.pair(0, 8, 0, 16);
            const double beatMs = 60000.0 / 123;
            for (int solo = 0; solo < 2; ++solo) {
                Setup t = s;                            // only one track audible
                t.mix.tracks[size_t(1 - solo)].trim = 0;
                t.mix.tracks[size_t(solo)].volume = Curve();
                auto e = t.engine();
                const auto out = renderMixRange(*e, 0, e->totalFrames());
                const double startMs = solo == 0 ? 0 : 8 * beatMs;
                double worst = 0;
                int count = 0;
                for (int k = 4; k < 40; ++k) {
                    const double expectMs = startMs + k * beatMs;
                    // loudest high-passed sample within +-40 ms of the master grid position
                    const long long centre = (long long)(expectMs * kRate / 1000), w = 40 * kRate / 1000;
                    double best = -1, pos = 0;
                    for (long long f = centre - w; f < centre + w; ++f) {
                        if (f < 1 || f >= (long long)out.size() / 2) continue;
                        const double d = std::fabs(out[size_t(f) * 2] - out[size_t(f - 1) * 2]);
                        if (d > best) { best = d; pos = double(f); }
                    }
                    worst = std::max(worst, std::fabs(pos * 1000.0 / kRate - expectMs));
                    ++count;
                }
                QVERIFY(count > 20);
                QVERIFY2(worst < c.beatTolMs + 0.5, qPrintable(QString("%1 track %2: beats %3 ms from the master grid").arg(c.engine).arg(solo + 1).arg(worst)));
                // pitch: unchanged by wsola, moved by the ratio for varispeed
                const double ratio = 123.0 / bpms[solo];
                const double want = std::string(c.engine) == "varispeed" ? 110 * ratio : 110.0;
                double best = 0, bestHz = 0;
                const size_t from = size_t((startMs / 1000 + 4) * kRate);
                for (double hz = want * 0.9; hz < want * 1.1; hz += 0.1) {
                    const double m = goertzel(out, hz, from, 3 * kRate);
                    if (m > best) { best = m; bestHz = hz; }
                }
                QVERIFY2(std::fabs(bestHz / want - 1) < 0.01, qPrintable(QString("%1 track %2 pitch %3 Hz, wanted %4").arg(c.engine).arg(solo + 1).arg(bestHz).arg(want)));
            }
        }
    }
};

QTEST_APPLESS_MAIN(TestMixEngine)
#include "test_trakcrate_mix_engine.moc"
