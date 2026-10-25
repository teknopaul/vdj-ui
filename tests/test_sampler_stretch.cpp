#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <cmath>
#include <vector>
#include "audio/sample_stretch.h"
#include "audio/sampler_module.h"
#include "audio/sampler_scheduler.h"

using namespace vdj;

class TestSamplerStretch : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void stretchLengthFollowsTempo();
    void stretchExactFrames();
    void stretchKeepsPitch();
    void stretchCacheHit();
    void acidLoopStartsOnBarAndDoesNotDrift();
    void acidLoopWithoutBpmAssumesFourBeats();
    void acidLoopWaitsForStretchedCopy();
    void acidLoopRestartChokesPreviousPass();
    void acidLoopSwitchedOffEndsOnNextBeat();
    void acidLoopOneBarButton();
    void oneShotQuantisedVsImmediate();
    void oneShotUnknownBpmNotStretched();
    void oneShotStretchesWhenReady();
    void oneShotOffset();
    void oneShotImmediateLead();

private:
    QTemporaryDir* dir_ = nullptr;
    SampleCache cache_;
    FakeBeatSource beats_;
    std::vector<Trigger> got_;
    std::vector<std::pair<int, qint64>> stops_;

    // a sine wave of `frames` frames at 44.1 kHz, stereo, to a .wav
    QString writeWav(const QString& name, qint64 frames, double freq = 440.0)
    {
        QByteArray pcm(int(frames * 4), 0);
        auto* s = reinterpret_cast<qint16*>(pcm.data());
        for (qint64 i = 0; i < frames; ++i)
            s[2 * i] = s[2 * i + 1] = qint16(8000 * std::sin(2 * M_PI * freq * double(i) / 44100.0));
        QByteArray h("RIFF");
        auto le32 = [&](quint32 v) { for (int i = 0; i < 4; ++i) h.append(char((v >> (8 * i)) & 0xff)); };
        auto le16 = [&](quint16 v) { h.append(char(v & 0xff)); h.append(char(v >> 8)); };
        le32(36 + pcm.size()); h.append("WAVEfmt "); le32(16); le16(1); le16(2); le32(44100); le32(44100 * 4); le16(4); le16(16);
        h.append("data"); le32(pcm.size());
        const QString path = dir_->path() + "/" + name;
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(h);
        f.write(pcm);
        return path;
    }
    std::shared_ptr<SamplerModule> make(SamplerModule::Type type, const QString& sample, bool enabled)
    {
        auto m = SamplerModule::create(type);
        ModuleSettings s = m->settings();
        s.samplePath = sample;
        s.enabled = enabled;
        m->setSettings(s);
        return m;
    }
    void run(SamplerScheduler& sch, int fromMs, int toMs)
    {
        for (int t = fromMs; t <= toMs; t += 5)
            sch.tick(qint64(t) * 1000000);
    }
    static qint64 ms(double v) { return qint64(v * 1e6); }
    // a loop 4 beats long at 100 BPM = 2.4 s
    static constexpr qint64 kLoop100 = 105840;
};

#define MAKE_SCHED \
    SamplerScheduler sch(&beats_, &cache_, [this](const Trigger& t) { got_.push_back(t); }); \
    sch.setStopSink([this](int tag, qint64 ns) { stops_.emplace_back(tag, ns); })

void TestSamplerStretch::init()
{
    delete dir_;
    dir_ = new QTemporaryDir;
    cache_.clear();
    beats_ = FakeBeatSource();
    got_.clear();
    stops_.clear();
}

void TestSamplerStretch::stretchLengthFollowsTempo()
{
    Sample s;
    s.audio.samples.resize(int(kLoop100 * 2));
    s.bpm = 100;
    for (double tempo : { 0.8, 1.0, 1.2, 1.5 }) {
        const auto out = stretchSample(s, tempo);
        const double expect = double(kLoop100) / tempo;
        QVERIFY2(qAbs(double(out->audio.frames()) - expect) / expect < 0.005, qPrintable(QString::number(tempo)));
        QVERIFY(qAbs(out->bpm - 100 * tempo) < 1e-6);
    }
    // clamped to what the stretcher can do
    QCOMPARE(stretchSample(s, 5.0)->audio.frames(), qint64(std::llround(double(kLoop100) / 2.0)));
}

void TestSamplerStretch::stretchExactFrames()
{
    Sample s;
    s.audio.samples.resize(int(kLoop100 * 2));
    QCOMPARE(stretchSample(s, 1.2, 88200)->audio.frames(), qint64(88200));
    QCOMPARE(stretchSample(s, 1.2, 100000)->audio.frames(), qint64(100000));    // padded with silence
}

void TestSamplerStretch::stretchKeepsPitch()
{
    const QString p = writeWav("sine.wav", 44100 * 2, 440.0);
    auto base = cache_.get(p);
    QVERIFY(base);
    const auto out = stretchSample(*base, 1.25);
    int crossings = 0;
    const qint64 n = out->audio.frames() - 4096;
    for (qint64 i = 4097; i < n; ++i)
        if (out->audio.samples[int(2 * (i - 1))] < 0 && out->audio.samples[int(2 * i)] >= 0)
            ++crossings;
    const double hz = crossings * 44100.0 / double(n - 4097);
    QVERIFY2(qAbs(hz - 440.0) < 12.0, qPrintable(QString::number(hz)));
}

void TestSamplerStretch::stretchCacheHit()
{
    const QString p = writeWav("loop_100bpm.wav", kLoop100);
    auto base = cache_.get(p);
    StretchCache sc;
    QVERIFY(!sc.peek(p, 1200));
    auto a = sc.get(*base, 1200);
    auto b = sc.get(*base, 1200);
    QVERIFY(a.get() == b.get());
    QCOMPARE(sc.count(), 1);
    sc.get(*base, 1100);
    QCOMPARE(sc.count(), 2);
    QCOMPARE(tempoPermille(1.2004), 1200);
    QCOMPARE(tempoPermille(9.0), 2000);
}

void TestSamplerStretch::acidLoopStartsOnBarAndDoesNotDrift()
{
    MAKE_SCHED;
    const QString p = writeWav("loop_100bpm.wav", kLoop100);   // 100 BPM loop at a 120 BPM sequencer: tempo 1.2
    QVERIFY(cache_.get(p));
    auto m = make(SamplerModule::AcidLoop, p, false);
    sch.addModule(m);
    sch.tick(0);                                               // starts the stretch
    QVERIFY(sch.waitIdle(5000));
    run(sch, 5, 1200);
    QVERIFY(got_.empty());                                     // disabled
    ModuleSettings s = m->settings();
    s.enabled = true;
    m->setSettings(s);
    run(sch, 1205, 2000 * 17 + 50);
    QVERIFY(got_.size() >= 16);
    for (size_t i = 0; i < 16; ++i) {
        QCOMPARE(got_[i].steadyNs, ms(2000.0 * double(i + 1)));    // next bar line (2.0 s) and every 4 beats after, no drift
        QCOMPARE(got_[i].sample->audio.frames(), qint64(88200));  // exactly 4 beats at 120 BPM
    }
    QVERIFY(!got_[0].loop);
}

void TestSamplerStretch::acidLoopWithoutBpmAssumesFourBeats()
{
    MAKE_SCHED;
    const QString p = writeWav("mystery.wav", 3 * 44100);       // 3 s, no BPM anywhere: assumed 4 beats -> 2 s at 120 BPM
    QVERIFY(cache_.get(p));
    sch.addModule(make(SamplerModule::AcidLoop, p, true));
    sch.tick(0);
    QVERIFY(sch.waitIdle(5000));
    run(sch, 5, 2200);
    QVERIFY(!got_.empty());
    QCOMPARE(got_[0].sample->audio.frames(), qint64(88200));
}

void TestSamplerStretch::acidLoopWaitsForStretchedCopy()
{
    MAKE_SCHED;
    const QString p = writeWav("loop_100bpm.wav", kLoop100);
    QVERIFY(cache_.get(p));
    sch.addModule(make(SamplerModule::AcidLoop, p, true));
    sch.tick(0);                                               // the bar at beat 0 is inside the lookahead but the copy is not ready
    const bool playedAtOnce = !got_.empty();
    QVERIFY(sch.waitIdle(5000));
    QVERIFY(!playedAtOnce || got_[0].sample->audio.frames() == 88200);   // never the unstretched loop
    run(sch, 5, 2200);
    QVERIFY(!got_.empty());
    for (const Trigger& t : got_)
        QCOMPARE(t.sample->audio.frames(), qint64(88200));
}

void TestSamplerStretch::acidLoopRestartChokesPreviousPass()
{
    MAKE_SCHED;
    const QString p = writeWav("loop_100bpm.wav", kLoop100);
    QVERIFY(cache_.get(p));
    auto m = make(SamplerModule::AcidLoop, p, true);
    sch.addModule(m);
    sch.tick(0);
    QVERIFY(sch.waitIdle(5000));
    run(sch, 5, 4200);
    QVERIFY(got_.size() >= 2);
    QVERIFY(stops_.size() >= got_.size() - 1);
    // every start has a stop of the same tag at the same instant
    for (const Trigger& t : got_) {
        bool found = false;
        for (const auto& st : stops_)
            found |= st.first == m->id() && st.second == t.steadyNs;
        QVERIFY(found);
    }
}

void TestSamplerStretch::acidLoopSwitchedOffEndsOnNextBeat()
{
    MAKE_SCHED;
    const QString p = writeWav("loop_100bpm.wav", kLoop100);
    QVERIFY(cache_.get(p));
    auto m = make(SamplerModule::AcidLoop, p, true);
    sch.addModule(m);
    sch.tick(0);
    QVERIFY(sch.waitIdle(5000));
    run(sch, 5, 2100);
    stops_.clear();
    ModuleSettings s = m->settings();
    s.enabled = false;
    m->setSettings(s);
    run(sch, 2105, 2110);                                      // beat 4.2: the next beat is 5 = 2.5 s
    QCOMPARE(int(stops_.size()), 1);
    QCOMPARE(stops_[0].first, m->id());
    QCOMPARE(stops_[0].second, ms(2500));
    got_.clear();
    run(sch, 2115, 8000);
    QVERIFY(got_.empty());
}

void TestSamplerStretch::acidLoopOneBarButton()
{
    MAKE_SCHED;
    const QString p = writeWav("loop_100bpm.wav", kLoop100);
    QVERIFY(cache_.get(p));
    auto m = make(SamplerModule::AcidLoop, p, false);
    sch.addModule(m);
    sch.tick(0);
    QVERIFY(sch.waitIdle(5000));
    run(sch, 5, 1000);
    m->fireOneBar(beats_.beatsAtSteadyNs(ms(1000)));           // beat 2 -> the bar line at beat 4 = 2.0 s
    run(sch, 1005, 9000);
    QCOMPARE(int(got_.size()), 1);                             // one pass of the loop, not repeated
    QCOMPARE(got_[0].steadyNs, ms(2000));
}

void TestSamplerStretch::oneShotQuantisedVsImmediate()
{
    MAKE_SCHED;
    const QString p = writeWav("hit.wav", 4410);
    QVERIFY(cache_.get(p));
    auto m = make(SamplerModule::OneShot, p, false);
    sch.addModule(m);
    QVERIFY(sch.fireOneShot(m->id(), ms(1200)));                // beat 2.4: quantised to beat 3 = 1.5 s
    QCOMPARE(got_.back().steadyNs, ms(1500));
    ModuleSettings s = m->settings();
    s.quantize = false;
    m->setSettings(s);
    QVERIFY(sch.fireOneShot(m->id(), ms(1200)));
    QCOMPARE(got_.back().steadyNs, ms(1200));
    s.quantize = true;
    m->setSettings(s);
    beats_.isRunning = false;                                  // no sequencer: nothing to quantise to
    QVERIFY(sch.fireOneShot(m->id(), ms(1200)));
    QCOMPARE(got_.back().steadyNs, ms(1200));
    QVERIFY(!sch.fireOneShot(12345, ms(1)));                   // not a module
    sch.tick(ms(1300));                                        // a one shot never plays by itself
    QCOMPARE(int(got_.size()), 3);
}

void TestSamplerStretch::oneShotUnknownBpmNotStretched()
{
    MAKE_SCHED;
    const QString p = writeWav("hit.wav", 22050);
    auto base = cache_.get(p);
    auto m = make(SamplerModule::OneShot, p, false);
    sch.addModule(m);
    QVERIFY(sch.fireOneShot(m->id(), ms(100)));
    QVERIFY(got_.back().sample.get() == base.get());
    QVERIFY(sch.waitIdle(200));
    QCOMPARE(sch.stretchCache().count(), 0);
}

void TestSamplerStretch::oneShotStretchesWhenReady()
{
    MAKE_SCHED;
    const QString p = writeWav("tom_100bpm.wav", 22050);       // 100 BPM sample, 120 BPM sequencer
    auto base = cache_.get(p);
    QVERIFY(base && base->bpm == 100.0);
    auto m = make(SamplerModule::OneShot, p, false);
    sch.addModule(m);
    QVERIFY(sch.fireOneShot(m->id(), ms(100)));
    QVERIFY(got_.back().sample.get() == base.get());           // as recorded this time ...
    QVERIFY(sch.waitIdle(5000));
    QVERIFY(sch.fireOneShot(m->id(), ms(100)));
    const qint64 frames = got_.back().sample->audio.frames();  // ... the stretched copy the next time
    QVERIFY(qAbs(double(frames) - 22050.0 / 1.2) / (22050.0 / 1.2) < 0.01);
    ModuleSettings s = m->settings();
    s.stretch = false;
    m->setSettings(s);
    QVERIFY(sch.fireOneShot(m->id(), ms(100)));
    QVERIFY(got_.back().sample.get() == base.get());
    s.stretch = true;
    s.manualBpm = 0;
    m->setSettings(s);
}

void TestSamplerStretch::oneShotOffset()
{
    MAKE_SCHED;
    const QString p = writeWav("hit.wav", 4410);
    QVERIFY(cache_.get(p));
    auto m = make(SamplerModule::OneShot, p, false);
    ModuleSettings s = m->settings();
    s.offsetMs = -30;
    m->setSettings(s);
    sch.addModule(m);
    QVERIFY(sch.fireOneShot(m->id(), ms(1200)));
    QCOMPARE(got_.back().steadyNs, ms(1470));
}

void TestSamplerStretch::oneShotImmediateLead()
{
    MAKE_SCHED;
    sch.setImmediateLeadMs(60);
    const QString p = writeWav("hit.wav", 4410);
    QVERIFY(cache_.get(p));
    auto m = make(SamplerModule::OneShot, p, false);
    sch.addModule(m);
    ModuleSettings s = m->settings();
    s.quantize = false;
    m->setSettings(s);
    QVERIFY(sch.fireOneShot(m->id(), ms(1200)));
    QCOMPARE(got_.back().steadyNs, ms(1260));                  // heard 60 ms from now, not "now"
    s.quantize = true;
    m->setSettings(s);
    QVERIFY(sch.fireOneShot(m->id(), ms(1200)));
    QCOMPARE(got_.back().steadyNs, ms(1500));                  // the next beat is far enough away
    QVERIFY(sch.fireOneShot(m->id(), ms(1480)));
    QCOMPARE(got_.back().steadyNs, ms(2000));                  // 20 ms to the beat is too close: the one after
}

QTEST_MAIN(TestSamplerStretch)
#include "test_sampler_stretch.moc"
