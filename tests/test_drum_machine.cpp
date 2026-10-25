#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <vector>
#include "audio/sampler_module.h"
#include "audio/sampler_scheduler.h"

using namespace vdj;

class TestDrumMachine : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void kickOnTheBeats();
    void twoVoicesOnTheSameStep();
    void halfStepIsHalfGain();
    void volumesMultiply();
    void silentVoicesPlayNothing();
    void mutedVoicePlaysNothing();
    void perVoiceOffset();
    void swing127MovesTheOddSteps();
    void swing64AndSwingZero();
    void chokeFramesFollowTheBpm();
    void eachVoiceHasItsOwnChoke();
    void disabledModuleIsSilent();
    void oneBarPlaysTheWholePatternOnce();
    void undecodedSampleIsSkippedThenPlays();
    void patternRepeatsWithoutDrift();
    void sequencerStopCancelsTheOneBar();
    void idleModulePreloadsItsSamples();
    void dembowSwing127MovesStepsThreeAndThirteen();
    void dembowSwing64AndOldVariant();
    void dembowOneBarWithSwing();

private:
    QTemporaryDir* dir_ = nullptr;
    SampleCache cache_;
    FakeBeatSource beats_;
    std::vector<Trigger> got_;
    QString bd_, sd_, hh_;

    QString writeWav(const QString& name, int frames)
    {
        QByteArray pcm(frames * 4, 1);
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
    // a drum machine, enabled, with the given voices set up
    std::shared_ptr<SamplerModule> drums(bool enabled = true)
    {
        auto m = SamplerModule::create(SamplerModule::DrumMachine);
        ModuleSettings s = m->settings();
        s.enabled = enabled;
        m->setSettings(s);
        return m;
    }
    static void setSteps(ModuleSettings& s, int voice, std::initializer_list<int> on, quint8 level = StepOn)
    {
        for (int i : on)
            s.drums[voice].steps[i] = level;
    }
    void run(SamplerScheduler& sch, int fromMs, int toMs)
    {
        for (int t = fromMs; t <= toMs; t += 5)
            sch.tick(qint64(t) * 1000000);
    }
    static qint64 ms(double v) { return qint64(v * 1e6); }
};

#define MAKE_SCHED \
    SamplerScheduler sch(&beats_, &cache_, [this](const Trigger& t) { got_.push_back(t); })

void TestDrumMachine::init()
{
    delete dir_;
    dir_ = new QTemporaryDir;
    cache_.clear();
    beats_ = FakeBeatSource();
    got_.clear();
    bd_ = writeWav("bd.wav", 4000);
    sd_ = writeWav("sd.wav", 4000);
    hh_ = writeWav("hh.wav", 4000);
    for (const QString& p : { bd_, sd_, hh_ })
        QVERIFY(cache_.get(p));
}

void TestDrumMachine::kickOnTheBeats()
{
    MAKE_SCHED;
    auto m = drums();
    ModuleSettings s = m->settings();
    s.drums[0].samplePath = bd_;
    setSteps(s, 0, { 0, 4, 8, 12 });
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 2000);
    QVERIFY(got_.size() >= 4);
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(got_[size_t(i)].steadyNs, ms(500.0 * i));
        QCOMPARE(got_[size_t(i)].sample->path, bd_);
        QCOMPARE(got_[size_t(i)].tag, m->id());
        QCOMPARE(got_[size_t(i)].cutAfterFrames, qint64(0));       // no choke
    }
    QCOMPARE(got_[0].gain, 1.0f);
}

void TestDrumMachine::twoVoicesOnTheSameStep()
{
    MAKE_SCHED;
    auto m = drums();
    ModuleSettings s = m->settings();
    s.drums[0].samplePath = bd_;
    s.drums[1].samplePath = sd_;
    s.drums[4].samplePath = hh_;
    setSteps(s, 0, { 0 });
    setSteps(s, 1, { 0, 4 });
    setSteps(s, 4, { 2 });
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 400);
    int bd = 0, sd = 0, hh = 0;
    for (const Trigger& t : got_) {
        if (t.steadyNs == 0 && t.sample->path == bd_) ++bd;
        if (t.steadyNs == 0 && t.sample->path == sd_) ++sd;
        if (t.steadyNs == ms(250) && t.sample->path == hh_) ++hh;
    }
    QCOMPARE(bd, 1);
    QCOMPARE(sd, 1);
    QCOMPARE(hh, 1);
    QCOMPARE(int(got_.size()), 3);
}

void TestDrumMachine::halfStepIsHalfGain()
{
    MAKE_SCHED;
    auto m = drums();
    ModuleSettings s = m->settings();
    s.drums[0].samplePath = bd_;
    setSteps(s, 0, { 0 }, StepOn);
    setSteps(s, 0, { 4 }, StepHalf);
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 700);
    QVERIFY(got_.size() >= 2);
    QCOMPARE(got_[0].gain, 1.0f);
    QCOMPARE(got_[1].gain, 0.5f);
}

void TestDrumMachine::volumesMultiply()
{
    MAKE_SCHED;
    auto m = drums();
    ModuleSettings s = m->settings();
    s.volume = 64;                                   // the module (master)
    s.drums[0].samplePath = bd_;
    s.drums[0].volume = 100;
    setSteps(s, 0, { 0 }, StepHalf);
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 200);
    QVERIFY(!got_.empty());
    QVERIFY(qAbs(got_[0].gain - (64.0f / 127.0f) * (100.0f / 127.0f) * 0.5f) < 1e-6);
}

void TestDrumMachine::silentVoicesPlayNothing()
{
    MAKE_SCHED;
    auto m = drums();
    ModuleSettings s = m->settings();
    s.drums[0].samplePath = bd_;
    s.drums[0].volume = 0;                           // voice volume 0
    setSteps(s, 0, { 0, 4 });
    setSteps(s, 1, { 0, 4 });                        // no sample
    s.drums[2].samplePath = sd_;
    setSteps(s, 2, { 0, 4 });
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 700);
    for (const Trigger& t : got_)
        QCOMPARE(t.sample->path, sd_);               // only the audible voice
    QVERIFY(!got_.empty());
    got_.clear();
    s.volume = 0;                                    // the module volume 0 silences everything
    m->setSettings(s);
    run(sch, 705, 1700);
    QVERIFY(got_.empty());
}

void TestDrumMachine::mutedVoicePlaysNothing()
{
    MAKE_SCHED;
    auto m = drums();
    ModuleSettings s = m->settings();
    s.drums[0].samplePath = bd_;
    s.drums[2].samplePath = sd_;
    setSteps(s, 0, { 0, 4, 8, 12 });
    setSteps(s, 2, { 0, 4, 8, 12 });
    s.drums[0].muted = true;
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 700);
    QVERIFY(!got_.empty());
    for (const Trigger& t : got_)
        QCOMPARE(t.sample->path, sd_);               // the others still play
    got_.clear();
    s.drums[0].muted = false;                        // unmuting plays it again
    m->setSettings(s);
    run(sch, 705, 2700);
    bool bd = false;
    for (const Trigger& t : got_)
        bd = bd || t.sample->path == bd_;
    QVERIFY(bd);
}

void TestDrumMachine::perVoiceOffset()
{
    MAKE_SCHED;
    auto m = drums();
    ModuleSettings s = m->settings();
    s.drums[0].samplePath = bd_;
    s.drums[0].offsetMs = -10;
    s.drums[1].samplePath = sd_;
    s.drums[1].offsetMs = 15;
    s.offsetMs = 40;                                 // the module offset is not used by the DR-6S
    setSteps(s, 0, { 4 });
    setSteps(s, 1, { 4 });
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 700);
    QCOMPARE(int(got_.size()), 2);
    for (const Trigger& t : got_)
        QCOMPARE(t.steadyNs, t.sample->path == bd_ ? ms(490) : ms(515));
}

void TestDrumMachine::swing127MovesTheOddSteps()
{
    MAKE_SCHED;
    auto m = drums();
    ModuleSettings s = m->settings();
    s.swing = 127;
    s.drums[4].samplePath = hh_;
    setSteps(s, 4, { 0, 1, 2, 3, 5 });
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 1600);
    std::vector<qint64> t;
    for (const Trigger& g : got_)
        t.push_back(g.steadyNs);
    std::sort(t.begin(), t.end());
    QCOMPARE(int(t.size()), 5);
    QCOMPARE(t[0], ms(0));                           // step 0
    QCOMPARE(t[1], ms(125 + 125));                   // step 1 (0.25 beat) -> 0.5 beat
    QCOMPARE(t[2], ms(250));                         // step 2 is on the grid
    QCOMPARE(t[3], ms(375 + 125));                   // step 3 (0.75 beat) -> 1.0 beat
    QCOMPARE(t[4], ms(625 + 125));                   // step 5 (1.25 beat): the swing is the same in every beat
}

void TestDrumMachine::swing64AndSwingZero()
{
    for (int swing : { 0, 64 }) {
        got_.clear();
        MAKE_SCHED;
        auto m = drums();
        ModuleSettings s = m->settings();
        s.swing = swing;
        s.drums[4].samplePath = hh_;
        setSteps(s, 4, { 1 });
        m->setSettings(s);
        sch.addModule(m);
        run(sch, 0, 600);
        QCOMPARE(int(got_.size()), 1);
        const double beat = 0.25 + 0.25 * swing / 127.0;
        QVERIFY(qAbs(double(got_[0].steadyNs) - beat * 500e6) < 2.0);
    }
}

void TestDrumMachine::chokeFramesFollowTheBpm()
{
    for (double bpm : { 120.0, 60.0 }) {
        got_.clear();
        beats_.tempo = bpm;
        MAKE_SCHED;
        auto m = drums();
        ModuleSettings s = m->settings();
        s.drums[0].samplePath = bd_;
        s.drums[0].chokeSteps = 4;
        s.drums[1].samplePath = sd_;
        s.drums[1].chokeSteps = 1;
        s.drums[2].samplePath = hh_;                 // no choke
        setSteps(s, 0, { 0 });
        setSteps(s, 1, { 0 });
        setSteps(s, 2, { 0 });
        m->setSettings(s);
        sch.addModule(m);
        run(sch, 0, 100);
        QCOMPARE(int(got_.size()), 3);
        for (const Trigger& t : got_) {
            if (t.sample->path == bd_)
                QCOMPARE(t.cutAfterFrames, qint64(bpm == 120.0 ? 22050 : 44100));    // 4 quarter beats = a beat
            else if (t.sample->path == sd_)
                QVERIFY(qAbs(t.cutAfterFrames - (bpm == 120.0 ? 5512 : 11025)) <= 1);   // 1 quarter beat
            else
                QCOMPARE(t.cutAfterFrames, qint64(0));
            if (t.cutAfterFrames > 0)
                QCOMPARE(t.fadeFrames, qint64(4410));                                // 100 ms
        }
    }
}

void TestDrumMachine::eachVoiceHasItsOwnChoke()
{
    MAKE_SCHED;
    auto m = drums();
    ModuleSettings s = m->settings();
    s.drums[0].samplePath = bd_;
    s.drums[0].chokeSteps = 2;
    setSteps(s, 0, { 0, 4, 8, 12 });
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 2000);
    QVERIFY(got_.size() >= 4);
    for (const Trigger& t : got_)
        QCOMPARE(t.cutAfterFrames, qint64(11025));   // every hit carries it
}

void TestDrumMachine::disabledModuleIsSilent()
{
    MAKE_SCHED;
    auto m = drums(false);
    ModuleSettings s = m->settings();
    s.drums[0].samplePath = bd_;
    setSteps(s, 0, { 0, 4, 8, 12 });
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 3000);
    QVERIFY(got_.empty());
}

void TestDrumMachine::oneBarPlaysTheWholePatternOnce()
{
    MAKE_SCHED;
    auto m = drums(false);
    ModuleSettings s = m->settings();
    s.drums[0].samplePath = bd_;
    s.drums[1].samplePath = sd_;
    setSteps(s, 0, { 0, 8 });
    setSteps(s, 1, { 4, 12 });
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 1200);
    QVERIFY(m->supportsOneBar());
    m->fireOneBar(beats_.beatsAtSteadyNs(ms(1200)));  // beat 2.4: the bar line at beat 4 = 2.0 s
    run(sch, 1205, 7000);
    QCOMPARE(int(got_.size()), 4);                   // every voice of that one bar, then nothing
    std::vector<qint64> t;
    for (const Trigger& g : got_)
        t.push_back(g.steadyNs);
    std::sort(t.begin(), t.end());
    QCOMPARE(t[0], ms(2000));
    QCOMPARE(t[1], ms(2500));
    QCOMPARE(t[2], ms(3000));
    QCOMPARE(t[3], ms(3500));
    QCOMPARE(m->oneBarState(beats_.beatsAtSteadyNs(ms(7000))), OneBarState::Idle);
}

void TestDrumMachine::undecodedSampleIsSkippedThenPlays()
{
    MAKE_SCHED;
    const QString late = writeWav("late.wav", 500);
    auto m = drums();
    ModuleSettings s = m->settings();
    s.drums[0].samplePath = late;
    setSteps(s, 0, { 0, 4, 8, 12 });
    m->setSettings(s);
    sch.addModule(m);
    sch.tick(0);
    QVERIFY(got_.empty());                           // not decoded yet: the hit is skipped, the decode is started
    QVERIFY(sch.waitIdle(2000));
    QVERIFY(cache_.peek(late));
    run(sch, 5, 2000);
    QVERIFY(!got_.empty());
    QCOMPARE(got_[0].sample->path, late);
}

void TestDrumMachine::patternRepeatsWithoutDrift()
{
    MAKE_SCHED;
    auto m = drums();
    ModuleSettings s = m->settings();
    s.drums[0].samplePath = bd_;
    setSteps(s, 0, { 0, 6, 10 });
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 8100);                               // four bars at 120 BPM
    std::vector<qint64> t;
    for (const Trigger& g : got_)
        t.push_back(g.steadyNs);
    std::sort(t.begin(), t.end());
    QVERIFY(t.size() >= 12);
    for (int bar = 0; bar < 4; ++bar) {
        QCOMPARE(t[size_t(bar * 3)], ms(bar * 2000.0));
        QCOMPARE(t[size_t(bar * 3 + 1)], ms(bar * 2000.0 + 750));
        QCOMPARE(t[size_t(bar * 3 + 2)], ms(bar * 2000.0 + 1250));
    }
}

void TestDrumMachine::sequencerStopCancelsTheOneBar()
{
    MAKE_SCHED;
    auto m = drums(false);
    ModuleSettings s = m->settings();
    s.drums[0].samplePath = bd_;
    setSteps(s, 0, { 0, 4, 8, 12 });
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 100);
    m->fireOneBar(0.2);
    beats_.isRunning = false;
    run(sch, 105, 200);
    beats_.isRunning = true;
    beats_.startNs = ms(300);
    run(sch, 300, 4000);
    QVERIFY(got_.empty());
}

void TestDrumMachine::idleModulePreloadsItsSamples()
{
    MAKE_SCHED;
    const QString a = writeWav("pre_a.wav", 300), b = writeWav("pre_b.wav", 300);
    auto m = drums(false);                           // switched off: nothing plays, but the samples are read ahead
    ModuleSettings s = m->settings();
    s.drums[0].samplePath = a;
    s.drums[3].samplePath = b;
    m->setSettings(s);
    sch.addModule(m);
    QVERIFY(!cache_.peek(a));
    sch.tick(0);
    QVERIFY(sch.waitIdle(2000));
    QVERIFY(cache_.peek(a));
    QVERIFY(cache_.peek(b));
    QVERIFY(got_.empty());
}

void TestDrumMachine::dembowSwing127MovesStepsThreeAndThirteen()
{
    MAKE_SCHED;
    auto m = SamplerModule::create(SamplerModule::Dembomatik);
    ModuleSettings s = m->settings();
    s.samplePath = bd_;
    s.enabled = true;
    s.swing = 127;
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 2000);                               // the first bar
    std::vector<qint64> t;
    for (const Trigger& g : got_)
        t.push_back(g.steadyNs);
    std::sort(t.begin(), t.end());
    QVERIFY(t.size() >= 5);
    // X . . X . . X . X . . . . X . .   steps 0, 3, 6, 8, 13 at 120 BPM (125 ms per step)
    QCOMPARE(t[0], ms(0));                           // step 0 unchanged
    QCOMPARE(t[1], ms(500));                         // step 3: 0.75 -> 1.0 beat
    QCOMPARE(t[2], ms(750));                         // step 6: unchanged
    QCOMPARE(t[3], ms(1000));                        // step 8: unchanged
    QCOMPARE(t[4], ms(1625 + 125));                  // step 13: 3.25 -> 3.5 beats = 1750 ms
}

void TestDrumMachine::dembowSwing64AndOldVariant()
{
    MAKE_SCHED;
    auto m = SamplerModule::create(SamplerModule::Dembomatik);
    ModuleSettings s = m->settings();
    s.samplePath = bd_;
    s.enabled = true;
    s.swing = 64;
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 700);
    std::vector<qint64> t;
    for (const Trigger& g : got_)
        t.push_back(g.steadyNs);
    std::sort(t.begin(), t.end());
    QVERIFY(t.size() >= 2);
    QVERIFY(qAbs(double(t[1]) - (0.75 + 0.25 * 64 / 127.0) * 500e6) < 2.0);     // step 3
    // the swing survives the variant, and a variant without it is 0
    QCOMPARE(SamplerModule::fromVariant(m->toVariant())->settings().swing, 64);
    QCOMPARE(SamplerModule::fromVariant(QVariantMap{ { "type", "dembow" } })->settings().swing, 0);
}

void TestDrumMachine::dembowOneBarWithSwing()
{
    MAKE_SCHED;
    auto m = SamplerModule::create(SamplerModule::Dembomatik);
    ModuleSettings s = m->settings();
    s.samplePath = bd_;
    s.swing = 127;
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 1200);
    m->fireOneBar(beats_.beatsAtSteadyNs(ms(1200)));   // the bar at beat 4 = 2.0 s
    run(sch, 1205, 6000);
    QCOMPARE(int(got_.size()), 5);                   // the same five hits, two of them delayed
    std::vector<qint64> t;
    for (const Trigger& g : got_)
        t.push_back(g.steadyNs);
    std::sort(t.begin(), t.end());
    QCOMPARE(t[0], ms(2000));
    QCOMPARE(t[1], ms(2500));                        // step 3 swung
    QCOMPARE(t[2], ms(2750));
    QCOMPARE(t[3], ms(3000));
    QCOMPARE(t[4], ms(3750));                        // step 13 swung
}

QTEST_MAIN(TestDrumMachine)
#include "test_drum_machine.moc"
