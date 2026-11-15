#include <QtTest/QtTest>

#include "trakcrate/mix_model.h"
#include "trakcrate/track.h"

#include <cmath>
#include <fstream>

using namespace trakcrate;

namespace {

TrackFacts facts(double bpm, double seconds, long long firstUs = 0)
{
    TrackFacts f;
    f.grid.bpm = bpm;
    f.grid.firstBeatUs = firstUs;
    f.frames = std::llround(seconds * kEditRate);
    return f;
}

std::vector<MixTrackInput> inputs(std::vector<TrackFacts> fs)
{
    std::vector<MixTrackInput> v;
    int n = 0;
    for (const TrackFacts& f : fs) {
        MixTrackInput in;
        in.file = "track-master-copy/House/T" + std::to_string(++n) + "/track.mp3";
        in.facts = f;
        v.push_back(in);
    }
    return v;
}

std::vector<TrackFacts> factsOf(const std::vector<MixTrackInput>& in)
{
    std::vector<TrackFacts> v;
    for (const auto& i : in) v.push_back(i.facts);
    return v;
}

EditRegion cut(int start, int bars)
{
    EditRegion r;
    r.kind = EditKind::Cut;
    r.startBeat = start;
    r.lenBeats = bars * 4;
    return r;
}

using Pts = std::vector<std::pair<double, double>>;

Pts pts(const Curve& c)
{
    Pts p;
    for (const CurvePoint& q : c.points) p.push_back({q.beat, q.value});
    return p;
}

void writeFile(const fs::path& p, const std::string& s)
{
    std::ofstream(p, std::ios::binary) << s;
}

} // namespace

class TestMixModel : public QObject
{
    Q_OBJECT
private slots:
    void curveInterpolationAndHolds()
    {
        Curve c;
        QCOMPARE(curveGain(c, 5, 120), 1.0);                 // empty = 100 %
        setPoint(&c, 10, 0);
        setPoint(&c, 20, 100);
        QCOMPARE(curveGain(c, 0, 120), 0.0);                 // first value holds before
        QCOMPARE(curveGain(c, 15, 120), 0.5);
        QCOMPARE(curveGain(c, 99, 120), 1.0);                // last value holds after
        QVERIFY(!curveIsDefault(c));
        removePoint(&c, 0);
        QVERIFY(curveIsDefault(c));
    }

    void pointsStaySorted()
    {
        Curve c;
        setPoint(&c, 5, 10);
        setPoint(&c, 1, 20);
        const int i = setPoint(&c, 3, 30);
        QCOMPARE(i, 1);
        QCOMPARE(pts(c), (Pts{{1, 20}, {3, 30}, {5, 10}}));
        const int j = movePoint(&c, 1, 9, 130);              // value clamps
        QCOMPARE(j, 2);
        QCOMPARE(pts(c), (Pts{{1, 20}, {5, 10}, {9, 100}}));
        setPoint(&c, 5, 70);                                 // same beat: after the existing one
        QCOMPARE(pts(c)[1], (std::pair<double, double>{5, 10}));
        QCOMPARE(pts(c)[2], (std::pair<double, double>{5, 70}));
    }

    void verticalChangeHasFiveMsRamp()
    {
        Curve c;
        setPoint(&c, 10, 100);
        setPoint(&c, 10, 0);
        const double bpm = 120;
        const double ramp = 0.005 * bpm / 60;                 // beats
        QCOMPARE(curveGain(c, 9.999, bpm), 1.0);
        QVERIFY(std::fabs(curveGain(c, 10 + ramp / 2, bpm) - 0.5) < 1e-9);
        QCOMPARE(curveGain(c, 10 + ramp * 1.01, bpm), 0.0);
        // never a step larger than the ramp allows
        double prev = curveGain(c, 9.9, bpm);
        for (double b = 9.9; b < 10.1; b += ramp / 10) {
            const double g = curveGain(c, b, bpm);
            QVERIFY(std::fabs(g - prev) <= 0.101);
            prev = g;
        }
    }

    void defaultMixPlacesTracks()
    {
        const auto in = inputs({facts(120, 200), facts(120, 200), facts(120, 200)});
        const Mix mix = defaultMix("x", in);
        QCOMPARE(mix.masterBpm, 120.0);
        QCOMPARE(mix.tracks.size(), size_t(3));
        QCOMPARE(mix.tracks[0].next.lenBeats, 32.0);
        QCOMPARE(mix.tracks[0].next.outStartBeat, 368.0);     // 400 beats, 32 before the end
        QCOMPARE(mix.tracks[0].next.inStartBeat, 0.0);
        QCOMPARE(int(mix.tracks[0].next.type), int(MixType::CrossFade));
        const auto eff = effectiveTracks(mix, factsOf(in));
        const auto st = trackStartMs(mix, eff);
        QCOMPARE(st[0], 0.0);
        QVERIFY(std::fabs(st[1] - 184000) < 0.01);            // track 2 starts at track 1's outStart
        QVERIFY(std::fabs(st[2] - 368000) < 0.01);
        QVERIFY(std::fabs(mixLengthMs(st, eff) - 568000) < 0.01);
        QVERIFY(validateMix(mix, factsOf(in)).empty());
        // cross-fade curves written
        QCOMPARE(pts(mix.tracks[0].volume), (Pts{{368, 100}, {400, 0}}));
        // track 2 is the incoming of pair 1 and the outgoing of pair 2 (here: the last pair of the mix is 2 -> 3)
        QCOMPARE(pts(mix.tracks[1].volume), (Pts{{0, 0}, {32, 100}, {368, 100}, {400, 0}}));
        QCOMPARE(pts(mix.tracks[1].high).size(), size_t(0));
    }

    void cutShortensEditedTrackAndMovesDefaultOutStart()
    {
        auto in = inputs({facts(120, 200), facts(120, 200)});
        in[0].regions = {cut(100, 4)};                        // 16 beats = 8 s
        const Mix mix = defaultMix("x", in);
        const auto eff = effectiveTracks(mix, factsOf(in));
        QVERIFY(eff[0].ok);
        QCOMPARE(eff[0].editedFrames, std::llround(192.0 * kEditRate));
        QCOMPARE(eff[0].editedBeats, 384.0);
        QCOMPARE(mix.tracks[0].next.outStartBeat, 352.0);
    }

    void everyMixTypeMatchesTheTable()
    {
        struct Row {
            MixType type;
            Pts outVol, inVol, outBass, inBass;
        };
        // o0 = 64, i0 = 0, L = 32 -> oE = 96, iE = 32; bass-switch halfway = 16
        const std::vector<Row> table = {
            {MixType::CrossFade, {{64, 100}, {96, 0}}, {{0, 0}, {32, 100}}, {}, {}},
            {MixType::CrossFadeDrop, {{64, 100}, {96, 0}}, {{0, 0}, {32, 100}}, {}, {{0, 0}, {32, 0}, {32, 100}}},
            {MixType::BassDrop, {{64, 100}, {96, 100}, {96, 0}}, {{0, 0}, {32, 100}}, {{64, 100}, {96, 0}}, {{0, 0}, {32, 0}, {32, 100}}},
            {MixType::RoughCut, {{64, 100}, {96, 100}, {96, 0}}, {{0, 0}, {32, 0}, {32, 100}}, {}, {}},
            {MixType::DropCut, {{64, 100}, {92, 100}, {96, 0}}, {{0, 0}, {32, 0}, {32, 100}}, {}, {}},
            {MixType::StopCut, {{64, 100}, {94, 100}, {95, 0}}, {{0, 0}, {32, 0}, {32, 100}}, {}, {}},
            {MixType::TailCut, {{64, 100}, {96, 100}, {104, 0}}, {{0, 0}, {32, 0}, {32, 100}}, {}, {}},
            // the incoming window starts 8 beats before the track: clamped to beat 0
            {MixType::BassSwitch, {{64, 100}, {96, 100}, {96, 0}}, {{0, 0}, {0, 100}}, {{64, 100}, {80, 100}, {80, 0}}, {{0, 0}, {16, 0}, {16, 100}}},
        };
        auto in = inputs({facts(120, 300), facts(120, 300)});
        for (const Row& r : table) {
            Mix mix = defaultMix("x", in);
            mix.tracks[0].next = {64, 0, 32, MixType::CrossFade};
            for (MixTrack& t : mix.tracks) t.volume = t.bass = t.high = Curve();
            const auto eff = effectiveTracks(mix, factsOf(in));
            applyMixType(&mix, 0, r.type, eff);
            QCOMPARE(int(mix.tracks[0].next.type), int(r.type));
            const QString name = mixTypeName(r.type);
            QVERIFY2(pts(mix.tracks[0].volume) == r.outVol, qPrintable(name + " out volume"));
            QVERIFY2(pts(mix.tracks[1].volume) == r.inVol, qPrintable(name + " in volume"));
            QVERIFY2(pts(mix.tracks[0].bass) == r.outBass, qPrintable(name + " out bass"));
            QVERIFY2(pts(mix.tracks[1].bass) == r.inBass, qPrintable(name + " in bass"));
            QVERIFY2(mix.tracks[0].high.points.empty() && mix.tracks[1].high.points.empty(), qPrintable(name + " high"));
            // idempotent
            const Mix once = mix;
            applyMixType(&mix, 0, r.type, eff);
            QVERIFY2(pts(mix.tracks[0].volume) == pts(once.tracks[0].volume) && pts(mix.tracks[1].bass) == pts(once.tracks[1].bass),
                     qPrintable(name + " idempotent"));
        }
    }

    void mixTypeNamesRoundTrip()
    {
        for (int i = 0; i < kMixTypeCount; ++i) {
            MixType t;
            QVERIFY(mixTypeFromName(mixTypeName(MixType(i)), &t));
            QCOMPARE(int(t), i);
        }
        MixType t;
        QVERIFY(!mixTypeFromName("nope", &t));
    }

    void pointsOutsideTheWindowSurviveAndOldInsideAreReplaced()
    {
        auto in = inputs({facts(120, 300), facts(120, 300)});
        Mix mix = defaultMix("x", in);
        mix.tracks[0].next = {64, 0, 32, MixType::CrossFade};
        const auto eff = effectiveTracks(mix, factsOf(in));
        mix.tracks[0].volume = Curve();
        setPoint(&mix.tracks[0].volume, 10, 50);              // before the window [64, 104]
        setPoint(&mix.tracks[0].volume, 80, 20);              // inside: replaced
        setPoint(&mix.tracks[0].volume, 200, 70);             // after
        setPoint(&mix.tracks[0].bass, 90, 0);                 // inside, bass not written by cross-fade: cleared
        applyMixType(&mix, 0, MixType::CrossFade, eff);
        QCOMPARE(pts(mix.tracks[0].volume), (Pts{{10, 50}, {64, 100}, {96, 0}, {200, 70}}));
        QVERIFY(mix.tracks[0].bass.points.empty());
        // a bass curve with points outside is held at 100 % across the window
        setPoint(&mix.tracks[0].bass, 300, 30);
        setPoint(&mix.tracks[0].bass, 90, 0);
        applyMixType(&mix, 0, MixType::CrossFade, eff);
        QCOMPARE(pts(mix.tracks[0].bass), (Pts{{64, 100}, {104, 100}, {300, 30}}));
    }

    void analysedOnTempFolder()
    {
        const fs::path dir = makeTempDir("mixmodel");
        QVERIFY(!dir.empty());
        QVERIFY(!isAnalysed(dir));
        QVERIFY(analysedProblem(dir).find("beat grid") != std::string::npos);
        KvDoc d;
        BeatGrid g;
        g.bpm = 124;
        setBeatGrid(d, g);
        writeFile(dir / "beatgrid.txt", d.serialise());
        QVERIFY(analysedProblem(dir).find("waveform") != std::string::npos);
        writeFile(dir / "waveform.bin", "x");
        QVERIFY(isAnalysed(dir));
        g.bpm = 0;
        KvDoc none;
        setBeatGrid(none, g);
        writeFile(dir / "beatgrid.txt", none.serialise());
        QVERIFY(!isAnalysed(dir));
        QVERIFY(!isAnalysed(dir / "nope"));
        fs::remove_all(dir);
    }

    void validateMixFailures()
    {
        auto in = inputs({facts(120, 100), facts(120, 100)});
        const auto f = factsOf(in);
        const Mix good = defaultMix("x", in);
        QVERIFY(validateMix(good, f).empty());

        Mix m = good;
        m.masterBpm = 20;
        QVERIFY(!validateMix(m, f).empty());
        m = good;
        m.masterBpm = 301;
        QVERIFY(!validateMix(m, f).empty());
        m = good;
        m.tracks[1].missing = true;
        QVERIFY(validateMix(m, f).find("Track 2") != std::string::npos);
        m = good;
        m.tracks[0].next.lenBeats = 0;
        QVERIFY(!validateMix(m, f).empty());
        m = good;
        m.tracks[0].next.outStartBeat = 190;                  // 190 + 32 > 200 beats
        QVERIFY(!validateMix(m, f).empty());
        m = good;
        m.tracks[1].next.inStartBeat = 0;
        m.tracks[0].next.inStartBeat = 180;                   // lies beyond track 2's 200 beats with the length
        QVERIFY(!validateMix(m, f).empty());
        m = good;
        m.tracks[0].volume.points = {{0, 120}};
        QVERIFY(!validateMix(m, f).empty());
        m = good;
        m.tracks[0].trim = 250;
        QVERIFY(!validateMix(m, f).empty());
        m = good;
        m.tracks[0].regions = {cut(3, 1)};                    // regions are checked: a cut must be bars-long, this one runs fine but...
        m.tracks[0].regions[0].lenBeats = 5;
        QVERIFY(!validateMix(m, f).empty());
        m = good;
        m.masterBpm = 250;                                    // ratio 2.08
        QVERIFY(validateMix(m, f).find("not possible") != std::string::npos);
        m = good;
        m.tracks.clear();
        QVERIFY(!validateMix(m, {}).empty());
        m = good;
        m.tracks[0].next.outStartBeat = 0;
        m.tracks[0].next.inStartBeat = 60;                    // track 2 would start before the mix
        QVERIFY(validateMix(m, f).find("before the mix") != std::string::npos);
    }

    void gridNudgeShiftsOnlyTheEffectiveGrid()
    {
        const TrackFacts tf = facts(120, 60, 100000);
        Mix mix;
        mix.masterBpm = 120;
        MixTrack t;
        const EffectiveTrack base = effectiveTrack(mix, t, tf);
        t.gridNudgeMs = 12.5;
        const EffectiveTrack n = effectiveTrack(mix, t, tf);
        QCOMPARE(n.stretchedGrid.firstBeatUs - base.stretchedGrid.firstBeatUs, 12500LL);
        QCOMPARE(n.stretchedGrid.bpm, base.stretchedGrid.bpm);
        QCOMPARE(n.editedFrames, base.editedFrames);
        QCOMPARE(tf.grid.firstBeatUs, 100000LL);              // the input facts are untouched
    }

    void masterBpmAndRatios()
    {
        QCOMPARE(defaultMasterBpm({facts(126, 10), facts(120, 10)}), 126.0);
        QCOMPARE(defaultMasterBpm({}), 120.0);
        TrackFacts none;
        QCOMPARE(defaultMasterBpm({none, facts(99, 10)}), 99.0);
        QVERIFY(std::fabs(stretchRatio(124, facts(120, 1).grid, false) - 124.0 / 120) < 1e-12);
        QVERIFY(std::fabs(stretchRatio(124, facts(126, 1).grid, false) - 124.0 / 126) < 1e-12);
        QVERIFY(std::fabs(stretchRatio(124, facts(90, 1).grid, false) - 124.0 / 90) < 1e-12);
        QCOMPARE(stretchRatio(124, facts(90, 1).grid, true), 1.0);
    }

    void stretchedGridAndCutOnTheStretchedTimeline()
    {
        const TrackFacts tf = facts(126, 100, 100000);
        Mix mix;
        mix.masterBpm = 124;
        MixTrack t;
        const double ratio = 124.0 / 126;
        EffectiveTrack e = effectiveTrack(mix, t, tf);
        QVERIFY(e.ok);
        QVERIFY(std::fabs(e.ratio - ratio) < 1e-12);
        QCOMPARE(e.stretchedGrid.bpm, 124.0);
        QCOMPARE(e.stretchedGrid.firstBeatUs, std::llround(100000 / ratio));
        QCOMPARE(e.stretchedFrames, std::llround(tf.frames / ratio));
        QCOMPARE(e.editedFrames, e.stretchedFrames);
        QCOMPARE(e.bpm, 124.0);

        t.regions = {cut(8, 4)};                              // 16 beats of the MASTER grid
        e = effectiveTrack(mix, t, tf);
        QVERIFY(e.ok);
        const double cutFrames = 16 * 60.0 / 124 * kEditRate;
        QVERIFY(std::fabs(double(e.stretchedFrames - e.editedFrames) - cutFrames) < 2);
        // beat positions are on the edited grid: beats after the cut come 16 earlier
        EffectiveTrack plain = effectiveTrack(mix, MixTrack(), tf);
        QVERIFY(std::fabs(e.editedBeats - (plain.editedBeats - 16)) < 0.01);
    }

    void stretchOffKeepsOwnSpacing()
    {
        Mix mix;
        mix.masterBpm = 124;
        MixTrack t;
        t.stretchOff = true;
        const TrackFacts tf = facts(126, 100, 50000);
        const EffectiveTrack e = effectiveTrack(mix, t, tf);
        QCOMPARE(e.ratio, 1.0);
        QCOMPARE(e.bpm, 126.0);
        QCOMPARE(e.stretchedGrid.firstBeatUs, 50000LL);
        QCOMPARE(e.stretchedFrames, tf.frames);
    }

    void stretchWarningsAndErrors()
    {
        QVERIFY(stretchWarning(1.0).empty());
        QVERIFY(stretchWarning(1.079).empty());
        QVERIFY(stretchWarning(0.925).empty());
        QVERIFY(!stretchWarning(1.1).empty());
        QVERIFY(!stretchImpossible(1.1));
        QVERIFY(!stretchImpossible(2.0));
        QVERIFY(!stretchImpossible(0.5));
        QVERIFY(stretchImpossible(2.01));
        QVERIFY(stretchImpossible(0.49));
        QVERIFY(stretchWarning(2.5).find("not possible") != std::string::npos);
    }

    void timelineWithDifferentTempiAnchorsInMilliseconds()
    {
        auto in = inputs({facts(120, 300), facts(126, 300)});
        Mix mix = defaultMix("x", in);
        mix.masterBpm = 123;
        mix.tracks[0].next = {100, 8, 32, MixType::CrossFade};
        const auto eff = effectiveTracks(mix, factsOf(in));
        const auto st = trackStartMs(mix, eff);
        // both tracks run at 123 bpm now: track 2 starts 92 beats after track 1
        QVERIFY(std::fabs(st[1] - 92 * 60000.0 / 123) < 0.01);
        QVERIFY(std::fabs(beatToMixMs(st, eff, 1, 8) - beatToMixMs(st, eff, 0, 100)) < 0.01);
        QVERIFY(std::fabs(mixMsToBeat(st, eff, 1, beatToMixMs(st, eff, 1, 17.25)) - 17.25) < 1e-9);
    }
};

QTEST_APPLESS_MAIN(TestMixModel)
#include "test_trakcrate_mix_model.moc"
