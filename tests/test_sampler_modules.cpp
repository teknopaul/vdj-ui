#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <vector>
#include "audio/sampler_module.h"
#include "audio/sampler_scheduler.h"

using namespace vdj;

class TestSamplerModules : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void bombomatic120Bpm();
    void offsetShiftsHits();
    void stabBeatChooser();
    void disabledModuleIsSilent();
    void sampleChangeAppliesFromNextHit();
    void tempoChange();
    void sequencerStopAndRestart();
    void missingSampleIsSkippedThenLoaded();
    void oneBarPlaysNextBarOnly();
    void oneBarCancelWhileArmed();
    void oneBarWorksWhenDisabled();
    void oneBarCancelledBySequencerStop();
    void oneBarStateReporting();
    void reverseSyncStartsBeforeBeat();
    void reverseSyncLateStart();
    void settingsClampAndRoundTrip();
    void volumeScalesTheGainAndZeroIsSilent();
    void dembowPatternOnTheSixteenthGrid();
    void dembowScheduledAt120Bpm();
    void dembowOneBarAndDisabled();

private:
    QTemporaryDir* dir_ = nullptr;
    QString shortWav_, otherWav_, halfSecondWav_, threeSecondWav_;
    SampleCache cache_;
    FakeBeatSource beats_;
    std::vector<Trigger> got_;

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
    std::shared_ptr<SamplerModule> bomb(const QString& sample, bool enabled = true)
    {
        auto m = SamplerModule::create(SamplerModule::Bombomatic);
        ModuleSettings s = m->settings();
        s.samplePath = sample;
        s.enabled = enabled;
        m->setSettings(s);
        return m;
    }
    std::shared_ptr<SamplerModule> stab(const QString& sample, quint8 mask, bool enabled = true)
    {
        auto m = SamplerModule::create(SamplerModule::StabPlayer);
        ModuleSettings s = m->settings();
        s.samplePath = sample;
        s.enabled = enabled;
        s.beatMask = mask;
        m->setSettings(s);
        return m;
    }
    // run the scheduler on fake time from fromMs to toMs in 5 ms steps; the beat source starts at beat 0 at time 0
    void run(SamplerScheduler& sch, int fromMs, int toMs)
    {
        for (int t = fromMs; t <= toMs; t += 5)
            sch.tick(qint64(t) * 1000000);
    }
    static qint64 ms(double v) { return qint64(v * 1e6); }
};

void TestSamplerModules::init()
{
    delete dir_;
    dir_ = new QTemporaryDir;
    cache_.clear();
    beats_ = FakeBeatSource();
    got_.clear();
    shortWav_ = writeWav("a.wav", 1000);
    otherWav_ = writeWav("b.wav", 1000);
    halfSecondWav_ = writeWav("half.wav", 22050);
    threeSecondWav_ = writeWav("three.wav", 3 * 44100);
    for (const QString& p : { shortWav_, otherWav_, halfSecondWav_, threeSecondWav_ })
        QVERIFY(cache_.get(p));
}

#define MAKE_SCHED SamplerScheduler sch(&beats_, &cache_, [this](const Trigger& t) { got_.push_back(t); })

void TestSamplerModules::bombomatic120Bpm()
{
    MAKE_SCHED;
    sch.addModule(bomb(shortWav_));
    run(sch, 0, 8000);
    QCOMPARE(int(got_.size()), 17);                  // beats 0..16 (the one at 8.0 s is inside the 100 ms lookahead of 8.0 s)
    for (size_t i = 0; i < got_.size(); ++i)
        QCOMPARE(got_[i].steadyNs, ms(500.0 * double(i)));
}

void TestSamplerModules::offsetShiftsHits()
{
    MAKE_SCHED;
    auto m = bomb(shortWav_);
    ModuleSettings s = m->settings();
    s.offsetMs = -20;
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 2000);
    QVERIFY(got_.size() >= 4);
    QCOMPARE(got_[1].steadyNs, ms(480));
    QCOMPARE(got_[2].steadyNs, ms(980));
}

void TestSamplerModules::stabBeatChooser()
{
    MAKE_SCHED;
    sch.addModule(stab(shortWav_, 0b0101));          // beats 1 and 3 of the bar
    run(sch, 0, 8000);
    QCOMPARE(int(got_.size()), 9);                   // beats 0,2,4,...,16
    for (size_t i = 0; i < got_.size(); ++i)
        QCOMPARE(got_[i].steadyNs, ms(1000.0 * double(i)));
    got_.clear();
    beats_.startNs = ms(100000);                     // a fresh run with beat 2 and 4 of the bar
    SamplerScheduler sch2(&beats_, &cache_, [this](const Trigger& t) { got_.push_back(t); });
    sch2.addModule(stab(shortWav_, 0b1010));
    for (int t = 100000; t <= 103000; t += 5)
        sch2.tick(qint64(t) * 1000000);
    QCOMPARE(got_.front().steadyNs, ms(100500));     // beat 1 = the 2nd of the bar
    QCOMPARE(got_[1].steadyNs, ms(101500));          // beat 3 = the 4th
}

void TestSamplerModules::disabledModuleIsSilent()
{
    MAKE_SCHED;
    sch.addModule(bomb(shortWav_, false));
    run(sch, 0, 3000);
    QVERIFY(got_.empty());
}

void TestSamplerModules::sampleChangeAppliesFromNextHit()
{
    MAKE_SCHED;
    auto m = bomb(shortWav_);
    sch.addModule(m);
    run(sch, 0, 1200);                               // beats 0, 1 (1.0 s) scheduled; beat 3 (1.5 s) is at 1.4 s
    ModuleSettings s = m->settings();
    s.samplePath = otherWav_;
    m->setSettings(s);
    run(sch, 1205, 2200);
    QVERIFY(got_.size() >= 4);
    QCOMPARE(got_[0].sample->path, shortWav_);
    QCOMPARE(got_[2].sample->path, shortWav_);       // beat at 1.0 s was scheduled at 0.9 s
    QCOMPARE(got_[3].sample->path, otherWav_);       // the hit at 1.5 s onwards
}

void TestSamplerModules::tempoChange()
{
    MAKE_SCHED;
    sch.addModule(bomb(shortWav_));
    run(sch, 0, 2000);                               // 120 BPM
    const qint64 now = ms(2000);
    const double beat = beats_.beatsAtSteadyNs(now);
    beats_.tempo = 60.0;                             // keep the beat count continuous across the change
    beats_.startNs = now - qint64(beat * 60e9 / 60.0);
    got_.clear();
    run(sch, 2005, 6000);
    QVERIFY(got_.size() >= 3);
    for (size_t i = 1; i < got_.size(); ++i)
        QCOMPARE(got_[i].steadyNs - got_[i - 1].steadyNs, ms(1000));
}

void TestSamplerModules::sequencerStopAndRestart()
{
    MAKE_SCHED;
    sch.addModule(bomb(shortWav_));
    run(sch, 0, 1000);
    beats_.isRunning = false;
    got_.clear();
    run(sch, 1005, 3000);
    QVERIFY(got_.empty());
    beats_.isRunning = true;
    beats_.startNs = ms(3000);                       // the beat count starts over
    run(sch, 3000, 4000);
    QVERIFY(got_.size() >= 2);
    QCOMPARE(got_[0].steadyNs, ms(3000));
    QCOMPARE(got_[1].steadyNs, ms(3500));
}

void TestSamplerModules::missingSampleIsSkippedThenLoaded()
{
    MAKE_SCHED;
    const QString late = writeWav("late.wav", 500);  // not in the cache yet
    sch.addModule(bomb(late));
    sch.tick(0);
    QVERIFY(got_.empty());                           // the decode is requested, this beat is skipped
    QVERIFY(sch.waitIdle(2000));
    QVERIFY(cache_.peek(late));
    run(sch, 5, 2000);
    QVERIFY(!got_.empty());
    QVERIFY(got_.front().steadyNs >= ms(0));
}

void TestSamplerModules::oneBarPlaysNextBarOnly()
{
    MAKE_SCHED;
    auto m = stab(shortWav_, 0b1111, false);
    sch.addModule(m);
    run(sch, 0, 1200);                               // beat 2.4: mid bar
    QVERIFY(got_.empty());
    m->fireOneBar(beats_.beatsAtSteadyNs(ms(1200)));
    QCOMPARE(m->oneBarState(2.4), OneBarState::Armed);
    run(sch, 1205, 6000);
    QCOMPARE(int(got_.size()), 4);                   // beats 4,5,6,7 = 2.0, 2.5, 3.0, 3.5 s
    QCOMPARE(got_[0].steadyNs, ms(2000));
    QCOMPARE(got_[3].steadyNs, ms(3500));
    QCOMPARE(m->oneBarState(beats_.beatsAtSteadyNs(ms(6000))), OneBarState::Idle);
    // and it can be fired again
    got_.clear();
    m->fireOneBar(beats_.beatsAtSteadyNs(ms(6000)));  // beat 12 -> bar at beat 16 = 8.0 s
    run(sch, 6005, 9500);
    QCOMPARE(int(got_.size()), 4);
    QCOMPARE(got_[0].steadyNs, ms(8000));
}

void TestSamplerModules::oneBarCancelWhileArmed()
{
    MAKE_SCHED;
    auto m = stab(shortWav_, 0b1111, false);
    sch.addModule(m);
    run(sch, 0, 200);
    m->fireOneBar(0.4);
    QCOMPARE(m->oneBarState(0.4), OneBarState::Armed);
    m->fireOneBar(0.5);                              // second click
    QCOMPARE(m->oneBarState(0.5), OneBarState::Idle);
    run(sch, 205, 5000);
    QVERIFY(got_.empty());
}

void TestSamplerModules::oneBarWorksWhenDisabled()
{
    MAKE_SCHED;
    auto m = stab(shortWav_, 0b0001, false);
    sch.addModule(m);
    run(sch, 0, 100);
    m->fireOneBar(0.2);                              // (SamplerScheduler::fireOneBar reads the real clock, so not used with fake time)
    run(sch, 105, 4000);
    QCOMPARE(int(got_.size()), 1);                   // only beat 1 of the bar is on
    QCOMPARE(got_[0].steadyNs, ms(2000));
    QCOMPARE(got_[0].tag, m->id());
}

void TestSamplerModules::oneBarCancelledBySequencerStop()
{
    MAKE_SCHED;
    auto m = stab(shortWav_, 0b1111, false);
    sch.addModule(m);
    run(sch, 0, 100);
    m->fireOneBar(0.2);
    QVERIFY(m->oneBarState(0.4) != OneBarState::Idle);
    beats_.isRunning = false;
    run(sch, 105, 200);
    beats_.isRunning = true;
    beats_.startNs = ms(300);
    got_.clear();
    run(sch, 300, 4000);
    QVERIFY(got_.empty());
}

void TestSamplerModules::oneBarStateReporting()
{
    auto m = stab(shortWav_, 0b1111, false);
    QCOMPARE(m->oneBarState(0.0), OneBarState::Idle);
    m->fireOneBar(5.2);                              // armed for beat 8
    QCOMPARE(m->oneBarState(7.9), OneBarState::Armed);
    QCOMPARE(m->oneBarState(8.0), OneBarState::Playing);
    QCOMPARE(m->oneBarState(11.9), OneBarState::Playing);
    QCOMPARE(m->oneBarState(12.0), OneBarState::Idle);
    m->finishOneBar(12.0);
    m->fireOneBar(12.5);                             // a fresh press after it played arms again
    QCOMPARE(m->oneBarState(12.5), OneBarState::Armed);
    auto b = bomb(shortWav_, false);
    b->fireOneBar(0.0);                              // the bombomatic has no one bar button
    QCOMPARE(b->oneBarState(0.0), OneBarState::Idle);
}

void TestSamplerModules::reverseSyncStartsBeforeBeat()
{
    MAKE_SCHED;
    auto m = stab(halfSecondWav_, 0b0001);
    ModuleSettings s = m->settings();
    s.reverseSync = true;
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 6000);
    // the sample (500 ms) ends on beat 4 (2.0 s), 8 (4.0 s), ...; the one for beat 0 is skipped (it would have started at -500 ms)
    QVERIFY(got_.size() >= 3);
    QCOMPARE(got_[0].steadyNs, ms(1500));
    QCOMPARE(got_[1].steadyNs, ms(3500));
    QCOMPARE(got_[0].startFrame, qint64(0));
    // with an offset the end moves too
    got_.clear();
    s.offsetMs = 10;
    m->setSettings(s);
    run(sch, 6005, 10000);
    QVERIFY(!got_.empty());
    QCOMPARE((got_[0].steadyNs + ms(500) - ms(10)) % ms(2000), qint64(0));
}

void TestSamplerModules::reverseSyncLateStart()
{
    MAKE_SCHED;
    auto m = stab(threeSecondWav_, 0b0001);
    ModuleSettings s = m->settings();
    s.reverseSync = true;
    m->setSettings(s);
    sch.addModule(m);
    sch.tick(0);                                     // lookahead = 100 ms + 3 s: beats 0 (skipped) and 4 (2.0 s) are inside
    QCOMPARE(int(got_.size()), 1);
    // beat 4 ends at 2.0 s so it should have started at -1.0 s: 1.0 s played, 2.0 s of 3.0 s left -> part-way start
    QCOMPARE(got_[0].steadyNs, qint64(0));
    QCOMPARE(got_[0].startFrame, qint64(44100));
    QCOMPARE(got_[0].sample->path, threeSecondWav_);
}

void TestSamplerModules::settingsClampAndRoundTrip()
{
    auto m = stab(shortWav_, 0b1010);
    ModuleSettings s = m->settings();
    s.offsetMs = 500;
    s.midiChannel = 40;
    s.midiNote = -3;
    s.reverseSync = true;
    s.name = "my stabs";
    m->setSettings(s);
    QCOMPARE(m->settings().offsetMs, SamplerModule::kMaxOffsetMs);
    QCOMPARE(m->settings().midiChannel, 16);
    QCOMPARE(m->settings().midiNote, 0);
    auto copy = SamplerModule::fromVariant(m->toVariant());
    QVERIFY(copy);
    QCOMPARE(copy->type(), SamplerModule::StabPlayer);
    QCOMPARE(copy->settings().name, QString("my stabs"));
    QCOMPARE(copy->settings().beatMask, quint8(0b1010));
    QVERIFY(copy->settings().reverseSync);
    QVERIFY(copy->id() != m->id());
    QVERIFY(!SamplerModule::fromVariant(QVariantMap{ { "type", "nonsense" } }));
    QCOMPARE(SamplerModule::fromVariant(QVariantMap{ { "type", "bombomatic" } })->settings().gain, 1.0f);   // missing keys: defaults
}

void TestSamplerModules::volumeScalesTheGainAndZeroIsSilent()
{
    MAKE_SCHED;
    auto m = bomb(shortWav_);
    QCOMPARE(m->settings().volume, 127);             // full by default
    sch.addModule(m);
    run(sch, 0, 600);
    QVERIFY(!got_.empty());
    QCOMPARE(got_.back().gain, 1.0f);
    ModuleSettings s = m->settings();
    s.volume = 64;
    m->setSettings(s);
    got_.clear();
    run(sch, 605, 1700);
    QVERIFY(!got_.empty());
    QVERIFY(qAbs(got_.back().gain - 64.0f / 127.0f) < 1e-6);
    s.volume = 0;
    m->setSettings(s);
    got_.clear();
    run(sch, 1705, 3000);
    QVERIFY(got_.empty());                           // 0 plays nothing
    s.volume = 5000;                                 // clamped
    m->setSettings(s);
    QCOMPARE(m->settings().volume, 127);
    QCOMPARE(SamplerModule::fromVariant(m->toVariant())->settings().volume, 127);
    s.volume = 33;
    m->setSettings(s);
    QCOMPARE(SamplerModule::fromVariant(m->toVariant())->settings().volume, 33);
}

void TestSamplerModules::dembowPatternOnTheSixteenthGrid()
{
    // X . . X . . X . X . . . . X . .
    const char* pattern = "X..X..X.X....X..";
    for (int i = 0; i < 16; ++i)
        QCOMPARE(Dembomatik::stepIsOn(i), pattern[i] == 'X');
    QVERIFY(Dembomatik::stepIsOn(16));               // the next bar starts again
    auto m = SamplerModule::create(SamplerModule::Dembomatik);
    QCOMPARE(m->typeName(), QString("Dembomatik"));
    std::vector<PlannedHit> hits;
    ModuleSettings s = m->settings();
    s.enabled = true;
    m->setSettings(s);
    m->plan(0.0, 8.0, hits);                         // two bars
    QCOMPARE(int(hits.size()), 10);
    const double expect[] = { 0.0, 0.75, 1.5, 2.0, 3.25, 4.0, 4.75, 5.5, 6.0, 7.25 };
    for (int i = 0; i < 10; ++i)
        QCOMPARE(hits[size_t(i)].beat, expect[i]);
    // a window that starts and ends between steps: each hit once, none twice over consecutive windows
    std::vector<PlannedHit> a, b;
    m->plan(0.1, 3.3, a);
    m->plan(3.3, 8.0, b);
    QCOMPARE(int(a.size() + b.size()), 9);           // everything but the hit at 0.0
    QCOMPARE(a.front().beat, 0.75);
    QCOMPARE(b.front().beat, 4.0);                  // 3.25 belongs to the first window
}

void TestSamplerModules::dembowScheduledAt120Bpm()
{
    MAKE_SCHED;
    const QString p = shortWav_;
    auto m = SamplerModule::create(SamplerModule::Dembomatik);
    ModuleSettings s = m->settings();
    s.samplePath = p;
    s.enabled = true;
    s.offsetMs = -10;
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 4000);                               // two bars at 120 BPM: a beat is 500 ms
    QVERIFY(got_.size() >= 10);
    const double beats[] = { 0.0, 0.75, 1.5, 2.0, 3.25, 4.0, 4.75, 5.5, 6.0, 7.25 };
    for (int i = 0; i < 10; ++i)
        QCOMPARE(got_[size_t(i)].steadyNs, ms(beats[i] * 500.0 - 10.0));
}

void TestSamplerModules::dembowOneBarAndDisabled()
{
    MAKE_SCHED;
    auto m = SamplerModule::create(SamplerModule::Dembomatik);
    ModuleSettings s = m->settings();
    s.samplePath = shortWav_;
    m->setSettings(s);
    sch.addModule(m);
    run(sch, 0, 1200);
    QVERIFY(got_.empty());                           // switched off
    QVERIFY(m->supportsOneBar());
    m->fireOneBar(beats_.beatsAtSteadyNs(ms(1200)));  // beat 2.4: the bar at beat 4 (2.0 s)
    run(sch, 1205, 6000);
    QCOMPARE(int(got_.size()), 5);                   // the five kicks of that one bar
    QCOMPARE(got_[0].steadyNs, ms(2000));
    QCOMPARE(got_[4].steadyNs, ms(2000 + 3.25 * 500));
}

QTEST_MAIN(TestSamplerModules)
#include "test_sampler_modules.moc"
