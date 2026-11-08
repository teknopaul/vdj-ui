#include <QtTest/QtTest>

#include "trakcrate/edit_plan.h"

using namespace trakcrate;

namespace {

BeatGrid grid(double bpm = 120, long long firstUs = 0)
{
    BeatGrid g;
    g.bpm = bpm;
    g.firstBeatUs = firstUs;
    return g;
}

EditRegion cut(int start, int bars, EditAlgo a = EditAlgo::Cut, double p = 10)
{
    EditRegion r;
    r.kind = EditKind::Cut;
    r.startBeat = start;
    r.lenBeats = bars * 4;
    r.algo = a;
    r.algoParam = p;
    return r;
}

EditRegion loop(int start, int bars, int times, EditAlgo a = EditAlgo::Cut, double p = 10)
{
    EditRegion r = cut(start, bars, a, p);
    r.kind = EditKind::Loop;
    r.loopTimes = times;
    return r;
}

const long long kTotal = 44100LL * 64;      // 64 s = 128 beats at 120 bpm

} // namespace

class TestEditPlan : public QObject
{
    Q_OBJECT
private slots:
    void beatFrameHasNoDrift()
    {
        const BeatGrid g = grid(128, 123456);
        const double exact = 0.123456 + 1000 * 60.0 / 128;
        QCOMPARE(beatFrame(g, 1000), std::llround(exact * 44100));
        const BeatGrid h = grid(123.4);
        QCOMPARE(beatFrame(h, 1000), std::llround(1000 * 60.0 / 123.4 * 44100));
        QCOMPARE(beatFrame(grid(120), 4), 88200LL);
    }

    void validation()
    {
        const BeatGrid g = grid();
        QVERIFY(validateRegions(g, {}, kTotal).empty());
        QVERIFY(!validateRegions(BeatGrid(), {cut(0, 1)}, kTotal).empty());
        QVERIFY(validateRegions(g, {cut(4, 1)}, kTotal).empty());
        EditRegion odd = cut(4, 1);
        odd.lenBeats = 6;
        QVERIFY(!validateRegions(g, {odd}, kTotal).empty());
        QVERIFY(!validateRegions(g, {cut(4, 1, EditAlgo::Cut, 7)}, kTotal).empty());     // 7 ms is not offered
        QVERIFY(!validateRegions(g, {cut(-4, 1)}, kTotal).empty());
        QVERIFY(!validateRegions(g, {cut(124, 2)}, kTotal).empty());                       // past the end
        QVERIFY(!validateRegions(g, {loop(4, 3, 2)}, kTotal).empty());                     // 3 bars
        QVERIFY(!validateRegions(g, {loop(4, 1, 1)}, kTotal).empty());                     // once
        QVERIFY(!validateRegions(g, {loop(4, 1, 17)}, kTotal).empty());
        QVERIFY(!validateRegions(g, {loop(4, 1, 2, EditAlgo::TailOverlap, 1)}, kTotal).empty());
        QVERIFY(!validateRegions(g, {loop(4, 1, 2, EditAlgo::EarlyCut)}, kTotal).empty());
        QVERIFY(validateRegions(g, {loop(4, 4, 4, EditAlgo::Fadeout, 0.5)}, kTotal).empty());
        EditRegion tail = cut(8, 2, EditAlgo::TailOverlap, 2);
        QVERIFY(validateRegions(g, {tail}, kTotal).empty());
        tail.algoParam = 3;
        QVERIFY(!validateRegions(g, {tail}, kTotal).empty());
    }

    void overlapRejected()
    {
        const BeatGrid g = grid();
        QVERIFY(!validateRegions(g, {cut(8, 2), loop(12, 1, 2)}, kTotal).empty());
        QVERIFY(!validateRegions(g, {cut(8, 1), cut(12, 1)}, kTotal).empty());             // touching cuts
        QVERIFY(validateRegions(g, {cut(8, 1), loop(12, 1, 2)}, kTotal).empty());          // cut then loop may touch
        QVERIFY(validateRegions(g, {loop(8, 1, 2), cut(12, 1)}, kTotal).empty());
        QVERIFY(!validateRegions(g, {cut(8, 1), cut(8, 1)}, kTotal).empty());
    }

    void cutPlan()
    {
        const BeatGrid g = grid();
        const EditPlan p = planEdit(g, {cut(8, 2, EditAlgo::Cross, 1)}, kTotal);
        QCOMPARE(int(p.segments.size()), 2);
        QCOMPARE(int(p.joins.size()), 1);
        QCOMPARE(p.segments[0].start, 0LL);
        QCOMPARE(p.segments[0].end, beatFrame(g, 8));
        QCOMPARE(p.segments[1].start, beatFrame(g, 16));
        QCOMPARE(p.segments[1].end, kTotal);
        QCOMPARE(p.joins[0].outFrame, beatFrame(g, 8));
        QCOMPARE(p.joins[0].inFrame, beatFrame(g, 16));
        QVERIFY(p.joins[0].algo == EditAlgo::Cross);
        QCOMPARE(planOutputFrames(p), kTotal - 8 * 22050);
    }

    void loopPlan()
    {
        const BeatGrid g = grid();
        const EditPlan p = planEdit(g, {loop(16, 1, 3, EditAlgo::Fadeout, 1)}, kTotal);
        QCOMPARE(int(p.segments.size()), 3);
        QCOMPARE(int(p.joins.size()), 2);
        QCOMPARE(p.segments[0].end, beatFrame(g, 20));
        QCOMPARE(p.segments[1].start, beatFrame(g, 16));
        QCOMPARE(p.segments[1].end, beatFrame(g, 20));
        QCOMPARE(p.segments[2].start, beatFrame(g, 16));
        QCOMPARE(p.segments[2].end, kTotal);
        QCOMPARE(p.joins[1].outFrame, beatFrame(g, 20));
        QCOMPARE(p.joins[1].inFrame, beatFrame(g, 16));
        QCOMPARE(planOutputFrames(p), kTotal + 2 * 4 * 22050);
    }

    void earlyCutResolves()
    {
        EditRegion r = cut(16, 2);
        r.algo = EditAlgo::EarlyCut;
        r.earlyShift = 2;
        r.earlyAfter = false;
        r.earlyJoinAlgo = EditAlgo::Cross;
        r.earlyJoinParam = 0.5;
        const BeatGrid g = grid();
        QVERIFY(validateRegions(g, {r}, kTotal).empty());
        EditRegion before = resolveRegions({r}).front();
        QCOMPARE(before.startBeat, 14);
        QCOMPARE(before.lenBeats, 8);
        QVERIFY(before.algo == EditAlgo::Cross);
        r.earlyAfter = true;
        EditRegion after = resolveRegions({r}).front();
        QCOMPARE(after.startBeat, 18);
        QCOMPARE(after.lenBeats, 8);
        const EditPlan p = planEdit(g, {r}, kTotal);
        QCOMPARE(planOutputFrames(p), kTotal - 8 * 22050);
        // The shifted range must not run into a neighbour.
        QVERIFY(!validateRegions(g, {r, cut(26, 1)}, kTotal).empty());
        r.startBeat = 1;
        r.earlyAfter = false;
        r.earlyShift = 2;
        QVERIFY(!validateRegions(g, {r}, kTotal).empty());      // would start at -1
    }

    void cutAtStartMovesTheGrid()
    {
        const BeatGrid g = grid(120, 0);
        const EditPlan p = planEdit(g, {cut(0, 2)}, kTotal);
        QCOMPARE(int(p.segments.size()), 1);
        QCOMPARE(int(p.joins.size()), 0);
        QCOMPARE(p.segments[0].start, beatFrame(g, 8));
        QCOMPARE(outputGrid(g, p).firstBeatUs, 0LL);           // beat 9 is now the first sample
        QCOMPARE(outputGrid(g, p).bpm, 120.0);
        // With a 100 ms lead-in before beat 1 the lead-in stays and joins the rest, so the grid keeps its offset.
        const BeatGrid h = grid(120, 100000);
        const EditPlan q = planEdit(h, {cut(0, 2)}, kTotal);
        QCOMPARE(int(q.segments.size()), 2);
        QCOMPARE(int(q.joins.size()), 1);
        QCOMPARE(outputGrid(h, q).firstBeatUs, 100000LL);
        QCOMPARE(outputGrid(h, planEdit(h, {cut(8, 1)}, kTotal)).firstBeatUs, 100000LL);
    }

    void cutAtEnd()
    {
        const BeatGrid g = grid();
        const EditPlan p = planEdit(g, {cut(120, 2)}, kTotal);
        QCOMPARE(int(p.segments.size()), 1);
        QCOMPARE(int(p.joins.size()), 0);
        QCOMPARE(p.segments[0].end, beatFrame(g, 120));
    }

    void mixedLength()
    {
        const BeatGrid g = grid();
        const std::vector<EditRegion> rs = {loop(4, 1, 4), cut(16, 2), cut(40, 1, EditAlgo::Fadeout, 1), loop(60, 2, 2, EditAlgo::Cross, 2)};
        QVERIFY(validateRegions(g, rs, kTotal).empty());
        const EditPlan p = planEdit(g, rs, kTotal);
        QCOMPARE(int(p.joins.size()), int(p.segments.size()) - 1);
        const long long beats = 128 - 8 - 4 + 3 * 4 + 1 * 8;
        QCOMPARE(planOutputFrames(p), beats * 22050LL);
        // Order given does not matter.
        const EditPlan q = planEdit(g, {rs[3], rs[1], rs[0], rs[2]}, kTotal);
        QCOMPARE(planOutputFrames(q), planOutputFrames(p));
        QCOMPARE(int(q.segments.size()), int(p.segments.size()));
    }
};

QTEST_APPLESS_MAIN(TestEditPlan)
#include "test_trakcrate_edit_plan.moc"
