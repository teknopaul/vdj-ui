#include <QtTest/QtTest>
#include <vector>
#include "audio/sampler_engine.h"
#include "audio/voice_mixer.h"

using namespace vdj;

class TestSamplerEngine : public QObject
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

void TestSamplerEngine::triggerLandsOnExactFrame()
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

void TestSamplerEngine::triggerInLaterBlockWaits()
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

void TestSamplerEngine::overlappingVoicesSum()
{
    VoiceMixer m;
    m.post(trig(flat(1000, 1000), 1));
    m.post(trig(flat(1000, 2000), qint64(50 * nsPerFrame()) + 1));
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(out[2 * 10], qint16(1000));
    QCOMPARE(out[2 * 60], qint16(3000));
}

void TestSamplerEngine::clippingSaturates()
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

void TestSamplerEngine::lateTriggerDropped()
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

void TestSamplerEngine::slightlyLateTriggerPlaysAtOnce()
{
    VoiceMixer m;
    m.post(trig(flat(1000, 1000), 0));
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 10000000);                        // 10 ms late: inside the 20 ms tolerance
    QCOMPARE(out[0], qint16(1000));
    QCOMPARE(m.droppedLate(), qint64(0));
}

void TestSamplerEngine::loopWraps()
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

void TestSamplerEngine::stopTagRemovesVoicesAndPending()
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

void TestSamplerEngine::startFrameSkipsIntoSample()
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

void TestSamplerEngine::voiceEndsAndIsRemoved()
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

void TestSamplerEngine::ratioChangesSpeed()
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

void TestSamplerEngine::masterGain()
{
    VoiceMixer m;
    m.setMasterGain(0.5f);
    m.post(trig(flat(100, 1000), 1));
    std::vector<qint16> out(N * 2);
    m.render(out.data(), N, 0);
    QCOMPARE(out[0], qint16(500));
}

void TestSamplerEngine::stopTagAtCutsPlayingVoice()
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

void TestSamplerEngine::stopTagAtKeepsVoiceStartingAtTheSameTime()
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

void TestSamplerEngine::stopTagAtInLaterBlock()
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

void TestSamplerEngine::engineConstructsIdle()
{
    // not started: no output is opened, nothing is rendered, destruction joins the worker
    SamplerEngine e;
    QVERIFY(!e.running());
    e.setDevice("nonexistent");
    QCOMPARE(e.device(), QString("nonexistent"));
    e.post(trig(flat(10, 1), 1));
    e.stop();
    QCOMPARE(e.mixer().pendingTriggers(), 0);
}

void TestSamplerEngine::timingReportsRenderedMinusScheduled()
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

QTEST_MAIN(TestSamplerEngine)
#include "test_sampler_engine.moc"
