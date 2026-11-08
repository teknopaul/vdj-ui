#include <QtTest/QtTest>

#include "trakcrate/edit_render.h"

#include <atomic>
#include <cmath>

using namespace trakcrate;

namespace {

const long long kBeat = 22050;                  // 120 bpm at 44100 Hz
const long long kBeats = 64;
const long long kTotal = kBeat * kBeats;

BeatGrid grid120()
{
    BeatGrid g;
    g.bpm = 120;
    return g;
}

// 111.3 Hz does not fit a whole number of cycles into 4 beats or more, so a raw join of two bars always lands on a phase jump.
DecodedPcm sine()
{
    DecodedPcm p;
    p.stereo.resize(size_t(kTotal) * 2);
    for (long long i = 0; i < kTotal; ++i) {
        const float v = float(0.5 * std::sin(2 * M_PI * 111.3 * double(i) / 44100));
        p.stereo[size_t(i) * 2] = p.stereo[size_t(i) * 2 + 1] = v;
    }
    return p;
}

DecodedPcm noise()
{
    DecodedPcm p;
    p.stereo.resize(size_t(kTotal) * 2);
    unsigned s = 12345;
    for (size_t i = 0; i < p.stereo.size(); i += 2) {
        s = s * 1664525u + 1013904223u;
        p.stereo[i] = p.stereo[i + 1] = float(int(s >> 16) - 32768) / 65536.0f;
    }
    return p;
}

float maxStep(const DecodedPcm& p, size_t from = 0, size_t to = size_t(-1))
{
    float m = 0;
    to = std::min(to, p.frames());
    for (size_t i = std::max<size_t>(from, 1); i < to; ++i) m = std::max(m, std::fabs(p.stereo[i * 2] - p.stereo[(i - 1) * 2]));
    return m;
}

EditRegion cut(int start, int bars, EditAlgo a, double p)
{
    EditRegion r;
    r.kind = EditKind::Cut;
    r.startBeat = start;
    r.lenBeats = bars * 4;
    r.algo = a;
    r.algoParam = p;
    return r;
}

EditRegion loop(int start, int bars, int times, EditAlgo a, double p)
{
    EditRegion r = cut(start, bars, a, p);
    r.kind = EditKind::Loop;
    r.loopTimes = times;
    return r;
}

DecodedPcm render(const DecodedPcm& src, const std::vector<EditRegion>& rs, bool* ok = nullptr)
{
    const BeatGrid g = grid120();
    const EditPlan plan = planEdit(g, rs, kTotal);
    DecodedPcm out;
    const bool r = renderEdit(src, plan, g, &out);
    if (ok) *ok = r;
    return out;
}

} // namespace

class TestEditRender : public QObject
{
    Q_OBJECT
private slots:
    void lengthMatchesPlan()
    {
        const DecodedPcm s = sine();
        const std::vector<EditRegion> rs = {cut(8, 2, EditAlgo::Cross, 1), loop(24, 1, 3, EditAlgo::Fadeout, 1)};
        const EditPlan plan = planEdit(grid120(), rs, kTotal);
        bool ok = false;
        const DecodedPcm out = render(s, rs, &ok);
        QVERIFY(ok);
        QCOMPARE((long long)out.frames(), planOutputFrames(plan));
    }

    void rawJoinClicksButAlgosDoNot()
    {
        const DecodedPcm s = sine();
        const float natural = maxStep(s);
        // The naive edit: bars 2 and 3 cut out by hand.
        DecodedPcm raw;
        raw.stereo.assign(s.stereo.begin(), s.stereo.begin() + 8 * kBeat * 2);
        raw.stereo.insert(raw.stereo.end(), s.stereo.begin() + 16 * kBeat * 2, s.stereo.end());
        QVERIFY(maxStep(raw) > natural * 3);

        const std::vector<std::pair<EditAlgo, double>> algos = {{EditAlgo::Cut, 5},      {EditAlgo::Cut, 10},  {EditAlgo::Cut, 50}, {EditAlgo::Cross, 0.5},
                                                              {EditAlgo::Cross, 2},    {EditAlgo::Fadeout, 1}, {EditAlgo::Fadeout, 0.5}, {EditAlgo::TailOverlap, 0}};
        for (const auto& a : algos) {
            const DecodedPcm out = render(s, {cut(8, 2, a.first, a.second)});
            QVERIFY2(maxStep(out) < natural * 1.5f, qPrintable(QString("algo %1 param %2").arg(int(a.first)).arg(a.second)));
            const DecodedPcm lp = render(s, {loop(8, 2, 4, a.first == EditAlgo::TailOverlap ? EditAlgo::Cut : a.first, a.second)});
            QVERIFY2(maxStep(lp) < natural * 1.5f, qPrintable(QString("loop algo %1 param %2").arg(int(a.first)).arg(a.second)));
        }
    }

    void fadeoutReachesSilence()
    {
        const DecodedPcm out = render(sine(), {cut(8, 1, EditAlgo::Fadeout, 1)});
        const size_t J = size_t(8 * kBeat);
        QCOMPARE(out.stereo[(J - 1) * 2], 0.0f);
        QVERIFY(std::fabs(out.stereo[(J - kBeat - 100) * 2]) <= 0.5f);
        QCOMPARE(out.stereo[J * 2], 0.0f);                    // the next part is faded in from silence
    }

    void crossKeepsPowerAndLength()
    {
        const DecodedPcm s = noise();
        const DecodedPcm out = render(s, {cut(8, 2, EditAlgo::Cross, 1)});
        QCOMPARE((long long)out.frames(), kTotal - 8 * kBeat);
        double eIn = 0, eOut = 0;
        const size_t J = size_t(8 * kBeat);
        for (long long k = 0; k < kBeat; ++k) {
            const float o = out.stereo[(J - kBeat + k) * 2];
            const float i = s.stereo[(J - kBeat + k) * 2];
            eOut += o * o;
            eIn += i * i;
        }
        QVERIFY(std::fabs(eOut / eIn - 1.0) < 0.1);
        // Before and after the crossfade the audio is untouched.
        QCOMPARE(out.stereo[(J - kBeat - 1) * 2], s.stereo[(J - kBeat - 1) * 2]);
        QCOMPARE(out.stereo[(J + 10) * 2], s.stereo[size_t(16 * kBeat + 10) * 2]);
        // Midpoint is the equal power mix.
        const size_t mid = J - kBeat / 2;
        const size_t a = mid, b = size_t(16 * kBeat) - size_t(kBeat / 2);
        const double t = (double(kBeat / 2) + 0.5) / double(kBeat) * M_PI / 2;
        QVERIFY(std::fabs(out.stereo[mid * 2] - (s.stereo[a * 2] * std::cos(t) + s.stereo[b * 2] * std::sin(t))) < 1e-5);
    }

    void tailOverlap()
    {
        const DecodedPcm s = noise();
        const DecodedPcm k0 = render(s, {cut(8, 2, EditAlgo::TailOverlap, 0)});
        const DecodedPcm cutOut = render(s, {cut(8, 2, EditAlgo::Cut, 5)});
        QCOMPARE(k0.stereo, cutOut.stereo);
        const DecodedPcm k2 = render(s, {cut(8, 2, EditAlgo::TailOverlap, 2)});
        QCOMPARE(k2.frames(), k0.frames());
        const size_t J = size_t(8 * kBeat), tail = size_t(2 * kBeat), fade = 221;
        // Same everywhere except the last 5 ms before the join and the two beats after it.
        for (size_t i = 0; i < k2.frames(); i += 7) {
            if (i >= J - fade && i < J + tail) continue;
            QCOMPARE(k2.stereo[i * 2], k0.stereo[i * 2]);
        }
        // The outgoing audio runs on into the tail: no step at the join.
        QCOMPARE(k2.stereo[(J - 1) * 2], s.stereo[(J - 1) * 2]);
        QCOMPARE(k2.stereo[J * 2], s.stereo[J * 2]);              // the tail starts at full level, the new part at zero
        // The tail is capped by the removed range: a 4 beat tail out of a 1 bar cut is fine, one bar of 8 beats cannot go past it.
        bool ok = false;
        render(s, {cut(8, 1, EditAlgo::TailOverlap, 4)}, &ok);
        QVERIFY(ok);
    }

    void cancel()
    {
        const DecodedPcm s = sine();
        const EditPlan plan = planEdit(grid120(), {cut(8, 1, EditAlgo::Cut, 10)}, kTotal);
        std::atomic<bool> stop(true);
        DecodedPcm out;
        QVERIFY(!renderEdit(s, plan, grid120(), &out, &stop));
    }

    void loopSample()
    {
        const DecodedPcm s = sine();
        const float natural = maxStep(s);
        const std::vector<std::pair<EditAlgo, double>> algos = {{EditAlgo::Cut, 10}, {EditAlgo::Cross, 0.5}, {EditAlgo::Fadeout, 1}};
        for (const auto& a : algos) {
            const DecodedPcm l = renderLoopSample(s, grid120(), 8, 4, a.first, a.second);
            QCOMPARE((long long)l.frames(), 4 * kBeat);
            // Play it twice end to end: no click at the seam or anywhere.
            DecodedPcm twice = l;
            twice.stereo.insert(twice.stereo.end(), l.stereo.begin(), l.stereo.end());
            QVERIFY2(maxStep(twice) < natural * 1.5f, qPrintable(QString("algo %1").arg(int(a.first))));
        }
        // A raw repeat of the same 4 beats clicks.
        DecodedPcm raw;
        raw.stereo.assign(s.stereo.begin() + 8 * kBeat * 2, s.stereo.begin() + 12 * kBeat * 2);
        raw.stereo.insert(raw.stereo.end(), raw.stereo.begin(), raw.stereo.end());
        QVERIFY(maxStep(raw) > natural * 3);
        // Refused: tail algo, a range past the end, no grid.
        QCOMPARE(renderLoopSample(s, grid120(), 8, 4, EditAlgo::TailOverlap, 1).frames(), size_t(0));
        QCOMPARE(renderLoopSample(s, grid120(), 62, 4, EditAlgo::Cut, 10).frames(), size_t(0));
        QCOMPARE(renderLoopSample(s, BeatGrid(), 8, 4, EditAlgo::Cut, 10).frames(), size_t(0));
    }

    void edgeCases()
    {
        const DecodedPcm s = sine();
        // A crossfade longer than the part before the join is shortened, not an error.
        bool ok = false;
        DecodedPcm out = render(s, {cut(1, 1, EditAlgo::Cross, 2)}, &ok);
        QVERIFY(ok);
        QCOMPARE((long long)out.frames(), kTotal - 4 * kBeat);
        QVERIFY(maxStep(out) < maxStep(s) * 1.5f);
        // A cut at the very start (no seam) and a loop at the very end.
        out = render(s, {cut(0, 1, EditAlgo::Cut, 10), loop(60, 1, 2, EditAlgo::Cross, 1)}, &ok);
        QVERIFY(ok);
        QCOMPARE((long long)out.frames(), kTotal - 4 * kBeat + 4 * kBeat);
        QVERIFY(maxStep(out) < maxStep(s) * 1.5f);
        // A loop at the start, where there is no audio before it for a pre-roll.
        out = render(s, {loop(0, 1, 2, EditAlgo::Cross, 2)}, &ok);
        QVERIFY(ok);
        QVERIFY(maxStep(out) < maxStep(s) * 1.5f);
        // An empty plan renders an empty result.
        DecodedPcm e;
        QVERIFY(renderEdit(s, EditPlan(), grid120(), &e));
        QCOMPARE(e.frames(), size_t(0));
    }
};

QTEST_APPLESS_MAIN(TestEditRender)
#include "test_trakcrate_edit_render.moc"
