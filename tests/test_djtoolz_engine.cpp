#include <QtTest/QtTest>
#include <vector>
#include "audio/djtoolz_engine.h"
#include "audio/voice_mixer.h"

using namespace vdj;

class TestDjtoolzEngine : public QObject
{
    Q_OBJECT

private slots:
    void triggerLandsOnExactFrame();
    void triggerInLaterBlockWaits();
    void overlappingVoicesSum();
    void clippingSaturates();
    void lateTriggerDropped();
    void slightlyLateTriggerPlaysAtOnce();
    void loopWraps();
    void stopTagRemovesVoicesAndPending();
    void startFrameSkipsIntoSample();
    void voiceEndsAndIsRemoved();
    void ratioChangesSpeed();
    void masterGain();
    void stopTagAtCutsPlayingVoice();
    void stopTagAtKeepsVoiceStartingAtTheSameTime();
    void stopTagAtInLaterBlock();
    void engineConstructsIdle();
    void timingReportsRenderedMinusScheduled();
    void cutFadesOutAndEndsTheVoice();
    void cutShorterThanTheFadeClampsTheFade();
    void cutAcrossBlockBoundary();
    void cutWithLoopStartFrameAndStopTag();
    void cutsAreIndependentPerVoice();
    void cutZeroOrNegativeIsNoCut();

private:
    // an impulse train: value `v` on every frame for `frames` frames
    static std::shared_ptr<const Sample> flat(int frames, qint16 v)
    {
        auto s = std::make_shared<Sample>();
        s->path = "flat";
        s->audio.samples.fill(v, frames * 2);
        return s;
    }
    static std::shared_ptr<const Sample> ramp(int frames)
    {
        auto s = std::make_shared<Sample>();
        s->audio.samples.resize(frames * 2);
        for (int i = 0; i < frames; ++i)
            s->audio.samples[2 * i] = s->audio.samples[2 * i + 1] = qint16(i);
        return s;
    }
    static Trigger trig(std::shared_ptr<const Sample> s, qint64 ns)
    {
        Trigger t;
        t.sample = s;
        t.steadyNs = ns;
        return t;
    }
    static double nsPerFrame() { return 1e9 / VoiceMixer::kRate; }
    static constexpr int N = 256;
};

void TestDjtoolzEngine::triggerLandsOnExactFrame()
{
    VoiceMixer m;
    m.post(trig(flat(1000, 1000), qint64(100 * nsPerFrame() + 1)));   // start of frame 100 (+1 ns)
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(out[2 * 99], qint16(0));
    QCOMPARE(out[2 * 100], qint16(1000));
    QCOMPARE(out[2 * 100 + 1], qint16(1000));
    QCOMPARE(out[2 * 255], qint16(1000));
    QCOMPARE(m.activeVoices(), 1);
}

void TestDjtoolzEngine::triggerInLaterBlockWaits()
{
    VoiceMixer m;
    const qint64 blockNs = qint64(N * nsPerFrame());
    m.post(trig(flat(1000, 500), blockNs + qint64(10 * nsPerFrame()) + 1));
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(m.pendingTriggers(), 1);
    QCOMPARE(m.activeVoices(), 0);
    for (int i = 0; i < N * 2; ++i)
        QCOMPARE(out[i], qint16(0));
    m.render(out.data(), N, blockNs);
    QCOMPARE(out[2 * 9], qint16(0));
    QCOMPARE(out[2 * 10], qint16(500));
    QCOMPARE(m.pendingTriggers(), 0);
}

void TestDjtoolzEngine::overlappingVoicesSum()
{
    VoiceMixer m;
    m.post(trig(flat(1000, 1000), 1));
    m.post(trig(flat(1000, 2000), qint64(50 * nsPerFrame()) + 1));
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(out[2 * 10], qint16(1000));
    QCOMPARE(out[2 * 60], qint16(3000));
}

void TestDjtoolzEngine::clippingSaturates()
{
    VoiceMixer m;
    m.post(trig(flat(100, 30000), 1));
    m.post(trig(flat(100, 30000), 1));
    m.post(trig(flat(100, -30000), 1));
    m.post(trig(flat(100, -30000), 1));
    m.post(trig(flat(100, -30000), 1));
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(out[0], qint16(-30000));
    m.post(trig(flat(100, 30000), 10));
    m.post(trig(flat(100, 30000), 10));
    m.render(out.data(), N, 0);
    QCOMPARE(out[2], qint16(32767));
}

void TestDjtoolzEngine::lateTriggerDropped()
{
    VoiceMixer m;
    m.post(trig(flat(100, 1000), 0));
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 100000000);                       // 100 ms after the trigger time
    QCOMPARE(m.activeVoices(), 0);
    QCOMPARE(m.pendingTriggers(), 0);
    QCOMPARE(m.droppedLate(), qint64(1));
    QCOMPARE(out[0], qint16(0));
}

void TestDjtoolzEngine::slightlyLateTriggerPlaysAtOnce()
{
    VoiceMixer m;
    m.post(trig(flat(1000, 1000), 0));
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 10000000);                        // 10 ms late: inside the 20 ms tolerance
    QCOMPARE(out[0], qint16(1000));
    QCOMPARE(m.droppedLate(), qint64(0));
}

void TestDjtoolzEngine::loopWraps()
{
    VoiceMixer m;
    Trigger t = trig(ramp(100), 1);
    t.loop = true;
    m.post(t);
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(out[2 * 99], qint16(99));
    QCOMPARE(out[2 * 100], qint16(0));
    QCOMPARE(out[2 * 150], qint16(50));
    QCOMPARE(m.activeVoices(), 1);                            // a loop never ends by itself
    m.stopAll();
    QCOMPARE(m.activeVoices(), 0);
}

void TestDjtoolzEngine::stopTagRemovesVoicesAndPending()
{
    VoiceMixer m;
    Trigger a = trig(flat(10000, 100), 1), b = a, c = trig(flat(10000, 100), 10000000000LL);
    a.tag = 1; b.tag = 2; c.tag = 1;
    m.post(a); m.post(b); m.post(c);
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(m.activeVoices(), 2);
    QCOMPARE(m.pendingTriggers(), 1);
    m.stopTag(1);
    QCOMPARE(m.activeVoices(), 1);
    QCOMPARE(m.pendingTriggers(), 0);
    m.render(out.data(), N, 0);
    QCOMPARE(out[0], qint16(100));
}

void TestDjtoolzEngine::startFrameSkipsIntoSample()
{
    VoiceMixer m;
    Trigger t = trig(ramp(1000), 1);
    t.startFrame = 400;
    m.post(t);
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(out[0], qint16(400));
    QCOMPARE(out[2 * 10], qint16(410));
}

void TestDjtoolzEngine::voiceEndsAndIsRemoved()
{
    VoiceMixer m;
    m.post(trig(flat(300, 100), 1));
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(m.activeVoices(), 1);
    m.render(out.data(), N, qint64(N * nsPerFrame()));
    QCOMPARE(out[2 * 43], qint16(100));                       // frame 299
    QCOMPARE(out[2 * 44], qint16(0));
    QCOMPARE(m.activeVoices(), 0);
}

void TestDjtoolzEngine::ratioChangesSpeed()
{
    VoiceMixer m;
    Trigger t = trig(ramp(1000), 1);
    t.ratio = 2.0;
    m.post(t);
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(out[2 * 10], qint16(20));
    Trigger h = trig(ramp(1000), 1);
    h.ratio = 0.5;
    m.stopAll();
    m.post(h);
    m.render(out.data(), N, 0);
    QCOMPARE(out[2 * 10], qint16(5));
}

void TestDjtoolzEngine::masterGain()
{
    VoiceMixer m;
    m.setMasterGain(0.5f);
    m.post(trig(flat(100, 1000), 1));
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(out[0], qint16(500));
}

void TestDjtoolzEngine::stopTagAtCutsPlayingVoice()
{
    VoiceMixer m;
    Trigger t = trig(flat(10000, 700), 1);
    t.tag = 5;
    m.post(t);
    m.stopTagAt(5, qint64(100 * nsPerFrame()) + 1);
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(out[2 * 99], qint16(700));
    QCOMPARE(out[2 * 100], qint16(0));
    QCOMPARE(out[2 * 200], qint16(0));
    QCOMPARE(m.activeVoices(), 0);
}

void TestDjtoolzEngine::stopTagAtKeepsVoiceStartingAtTheSameTime()
{
    VoiceMixer m;
    Trigger old = trig(flat(10000, 700), 1);
    old.tag = 5;
    m.post(old);
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);                               // the old pass is playing
    const qint64 at = qint64(N * nsPerFrame()) + qint64(50 * nsPerFrame()) + 1;
    Trigger fresh = trig(flat(10000, 100), at);
    fresh.tag = 5;
    m.post(fresh);
    m.stopTagAt(5, at);                                       // "stop the old loop, start the new one"
    m.render(out.data(), N, qint64(N * nsPerFrame()));
    QCOMPARE(out[2 * 49], qint16(700));
    QCOMPARE(out[2 * 50], qint16(100));
    QCOMPARE(out[2 * 200], qint16(100));
    QCOMPARE(m.activeVoices(), 1);
}

void TestDjtoolzEngine::stopTagAtInLaterBlock()
{
    VoiceMixer m;
    Trigger t = trig(flat(100000, 700), 1);
    t.tag = 3;
    m.post(t);
    m.stopTagAt(3, qint64(3 * N * nsPerFrame()) + qint64(10 * nsPerFrame()) + 1);
    std::vector<qint16> out(N * 2);
    for (int b = 0; b < 3; ++b) {
        m.render(out.data(), N, qint64(b * N * nsPerFrame()));
        QCOMPARE(out[2 * 255], qint16(700));
    }
    m.render(out.data(), N, qint64(3 * N * nsPerFrame()));
    QCOMPARE(out[2 * 9], qint16(700));
    QCOMPARE(out[2 * 10], qint16(0));
    QCOMPARE(m.activeVoices(), 0);
    m.stopTagAt(3, 1);
    m.stopTag(3);                                             // cancels a stop that has not happened yet
    QCOMPARE(m.pendingTriggers(), 0);
}

void TestDjtoolzEngine::engineConstructsIdle()
{
    // not started: no output is opened, nothing is rendered, destruction joins the worker
    DjtoolzEngine e;
    QVERIFY(!e.running());
    e.setDevice("nonexistent");
    QCOMPARE(e.device(), QString("nonexistent"));
    e.post(trig(flat(10, 1), 1));
    e.stop();
    QCOMPARE(e.mixer().pendingTriggers(), 0);
}

void TestDjtoolzEngine::timingReportsRenderedMinusScheduled()
{
    VoiceMixer m;
    std::vector<qint16> out(N * 2);
    m.post(trig(flat(100, 1), 1));
    m.render(out.data(), N, 0);
    QVERIFY(m.drainTiming().empty());                         // off by default
    m.setTimingEnabled(true);
    Trigger on = trig(flat(1000, 1), 10000000 + qint64(100.4 * nsPerFrame()));
    on.tag = 4;
    m.post(on);
    Trigger late = trig(flat(1000, 1), 0);                    // 10 ms late: plays at once
    late.tag = 5;
    m.post(late);
    m.render(out.data(), N, 10000000);
    const auto t = m.drainTiming();
    QCOMPARE(int(t.size()), 2);
    for (const auto& s : t) {
        if (s.tag == 4)
            QVERIFY2(qAbs(s.errorNs) < 24000, qPrintable(QString::number(s.errorNs)));   // within one frame (22.7 us)
        else
            QVERIFY(qAbs(s.errorNs - 10000000) < 100);
    }
    QVERIFY(m.drainTiming().empty());                         // drained
    m.setTimingEnabled(false);
    m.post(trig(flat(100, 1), 10000001));
    m.render(out.data(), N, 10000000);
    QVERIFY(m.drainTiming().empty());
}

void TestDjtoolzEngine::cutFadesOutAndEndsTheVoice()
{
    VoiceMixer m;
    Trigger t = trig(flat(5000, 10000), 1);
    t.cutAfterFrames = 1000;
    t.fadeFrames = 441;
    m.post(t);
    std::vector<qint16> out(N * 2 * 4);
    for (int b = 0; b < 4; ++b)
        m.render(out.data() + b * N * 2, N, qint64(b * N * nsPerFrame()));
    // frames 0..558 at full gain, then a linear ramp that reaches 0 at frame 1000
    QCOMPARE(out[2 * 0], qint16(10000));
    QCOMPARE(out[2 * 558], qint16(10000));
    QCOMPARE(out[2 * 559], qint16(10000));                  // the ramp starts at 1.0
    const int mid = 559 + 220;                              // half way down
    QVERIFY2(qAbs(out[2 * mid] - 5000) < 100, qPrintable(QString::number(out[2 * mid])));
    QVERIFY(out[2 * 999] > 0 && out[2 * 999] < 40);        // almost 0
    QCOMPARE(out[2 * 1000], qint16(0));                     // silent at the cut
    QCOMPARE(out[2 * 1001], qint16(0));
    QCOMPARE(out[2 * 1023], qint16(0));
    for (int f = 560; f < 999; ++f)
        QVERIFY(out[2 * f] <= out[2 * (f - 1)]);            // monotonic down
    QCOMPARE(m.activeVoices(), 0);                          // and the voice is gone
}

void TestDjtoolzEngine::cutShorterThanTheFadeClampsTheFade()
{
    VoiceMixer m;
    Trigger t = trig(flat(5000, 10000), 1);
    t.cutAfterFrames = 100;
    t.fadeFrames = 4410;                                    // clamps to 100: a ramp from the first frame
    m.post(t);
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QVERIFY(out[0] > 9800);
    QVERIFY(qAbs(out[2 * 50] - 5000) < 200);
    QVERIFY(out[2 * 99] < 200);
    QCOMPARE(out[2 * 100], qint16(0));
    QCOMPARE(m.activeVoices(), 0);
}

void TestDjtoolzEngine::cutAcrossBlockBoundary()
{
    VoiceMixer m;
    Trigger t = trig(flat(5000, 10000), 1);
    t.cutAfterFrames = 300;                                 // 256 frame blocks: the cut falls in the second block
    t.fadeFrames = 100;
    m.post(t);
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(out[2 * 199], qint16(10000));                  // the fade starts at frame 200
    QVERIFY(out[2 * 250] < 10000 && out[2 * 250] > 4000);
    QCOMPARE(m.activeVoices(), 1);
    m.render(out.data(), N, qint64(N * nsPerFrame()));
    QVERIFY(out[2 * (299 - 256)] < 150);
    QCOMPARE(out[2 * (300 - 256)], qint16(0));
    QCOMPARE(out[2 * 200], qint16(0));
    QCOMPARE(m.activeVoices(), 0);
}

void TestDjtoolzEngine::cutWithLoopStartFrameAndStopTag()
{
    std::vector<qint16> out(N * 2);
    {
        VoiceMixer m;                                       // a loop is cut too
        Trigger t = trig(ramp(50), 1);
        t.loop = true;
        t.cutAfterFrames = 120;
        t.fadeFrames = 20;
        m.post(t);
        m.render(out.data(), N, 0);
        QCOMPARE(out[2 * 60], qint16(10));                  // looping: 60 mod 50
        QCOMPARE(out[2 * 121], qint16(0));
        QCOMPARE(m.activeVoices(), 0);
    }
    {
        VoiceMixer m;                                       // startFrame: the cut counts from where the voice starts playing
        Trigger t = trig(flat(5000, 10000), 1);
        t.startFrame = 3000;
        t.cutAfterFrames = 200;
        t.fadeFrames = 50;
        m.post(t);
        m.render(out.data(), N, 0);
        QCOMPARE(out[2 * 100], qint16(10000));
        QCOMPARE(out[2 * 200], qint16(0));
    }
    {
        VoiceMixer m;                                       // a hard stop before the cut still wins
        Trigger t = trig(flat(5000, 10000), 1);
        t.tag = 7;
        t.cutAfterFrames = 200;
        t.fadeFrames = 50;
        m.post(t);
        m.stopTagAt(7, qint64(80 * nsPerFrame()) + 1);
        m.render(out.data(), N, 0);
        QCOMPARE(out[2 * 79], qint16(10000));
        QCOMPARE(out[2 * 80], qint16(0));
        QCOMPARE(m.activeVoices(), 0);
    }
}

void TestDjtoolzEngine::cutsAreIndependentPerVoice()
{
    VoiceMixer m;
    Trigger a = trig(flat(5000, 1000), 1);
    a.cutAfterFrames = 100;
    a.fadeFrames = 10;
    Trigger b = trig(flat(5000, 2000), 1);                  // no cut
    Trigger c = trig(flat(60, 4000), 1);                    // the sample is shorter than a cut: ends by itself
    c.cutAfterFrames = 500;
    c.fadeFrames = 1;
    m.post(a); m.post(b); m.post(c);
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(out[2 * 10], qint16(7000));
    QCOMPARE(out[2 * 80], qint16(3000));                    // a and b (c ended at 60)
    QCOMPARE(out[2 * 150], qint16(2000));                   // only b
    QCOMPARE(m.activeVoices(), 1);
    // a second hit of the same sample is cut on its own
    m.stopAll();
    Trigger d = a;
    d.steadyNs = qint64(300 * nsPerFrame());
    m.post(a);
    m.post(d);
    m.render(out.data(), N, 0);
    QCOMPARE(out[2 * 50], qint16(1000));
    QCOMPARE(out[2 * 150], qint16(0));
    m.render(out.data(), N, qint64(N * nsPerFrame()));
    QCOMPARE(out[2 * (310 - 256)], qint16(1000));           // the later hit still plays at full level
    QCOMPARE(out[2 * (410 - 256)], qint16(0));
}

void TestDjtoolzEngine::cutZeroOrNegativeIsNoCut()
{
    VoiceMixer m;
    Trigger t = trig(flat(5000, 500), 1);
    t.cutAfterFrames = 0;
    m.post(t);
    Trigger n = trig(flat(5000, 100), 1);
    n.cutAfterFrames = -20;
    m.post(n);
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(out[2 * 255], qint16(600));
    QCOMPARE(m.activeVoices(), 2);
}

QTEST_MAIN(TestDjtoolzEngine)
#include "test_djtoolz_engine.moc"
