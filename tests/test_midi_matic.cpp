#include <QtTest/QtTest>
#include <vector>
#include "audio/djtoolz_tool.h"
#include "audio/djtoolz_scheduler.h"
#include "midi/alsa_clock.h"
#include "midi/midi_out.h"

using namespace vdj;

class TestMidiMatic : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        got_.clear();
        beats_ = FakeBeatSource();
    }
    void noteOnAndOffTicks();
    void offsetBecomesTicks();
    void channelAndNoteAreClamped();
    void volumeIsTheVelocity();
    void deviceAddressParsing();
    void beatChooserSelectsBeatsOfTheBar();
    void schedulerEmitsEventsWithoutASample();
    void schedulerHonoursOffsetAndDisabled();
    void sequencerStopReleasesNotes();
    void closedClockRefusesToSend();

private:
    SampleCache cache_;
    FakeBeatSource beats_;
    std::vector<MidiNoteEvent> got_;

    std::shared_ptr<DjtoolzTool> midi(quint8 mask, bool enabled = true)
    {
        auto m = DjtoolzTool::create(DjtoolzTool::MidiMatic);
        ToolSettings s = m->settings();
        s.enabled = enabled;
        s.beatMask = mask;
        s.midiDevice = "20:1";
        s.midiChannel = 10;
        s.midiNote = 36;
        s.midiVelocity = 90;
        m->setSettings(s);
        return m;
    }
    void run(DjtoolzScheduler& sch, int fromMs, int toMs)
    {
        for (int t = fromMs; t <= toMs; t += 5)
            sch.tick(qint64(t) * 1000000);
    }
};

void TestMidiMatic::noteOnAndOffTicks()
{
    ToolSettings s;
    s.midiChannel = 10;
    s.midiNote = 48;
    s.midiVelocity = 127;
    s.midiDevice = "20:0";
    const auto ev = buildNoteEvents(4.0, s, 120.0, 7);
    QCOMPARE(int(ev.size()), 2);
    QCOMPARE(ev[0].kind, MidiNoteEvent::NoteOn);
    QCOMPARE(ev[0].tick, qint64(4 * AlsaClock::kPpq));
    QCOMPARE(ev[0].channel, 9);
    QCOMPARE(ev[0].note, 48);
    QCOMPARE(ev[0].velocity, 127);
    QCOMPARE(ev[0].device, QString("20:0"));
    QCOMPARE(ev[0].tag, 7);
    QCOMPARE(ev[1].kind, MidiNoteEvent::NoteOff);
    QCOMPARE(ev[1].tick, qint64(4 * AlsaClock::kPpq + AlsaClock::kPpq / 4));   // a quarter beat later
    QVERIFY(buildNoteEvents(1.0, s, 0.0).empty());
}

void TestMidiMatic::offsetBecomesTicks()
{
    ToolSettings s;
    s.offsetMs = -25;                                // at 120 BPM a beat is 500 ms = 96 ticks: -25 ms = -4.8 ticks
    QCOMPARE(buildNoteEvents(2.0, s, 120.0)[0].tick, qint64(2 * 96 - 5));
    s.offsetMs = 25;
    QCOMPARE(buildNoteEvents(2.0, s, 120.0)[0].tick, qint64(2 * 96 + 5));
    s.offsetMs = -80;
    QCOMPARE(buildNoteEvents(0.0, s, 120.0)[0].tick, qint64(0));   // never before the start of the queue
    s.offsetMs = 25;
    QCOMPARE(buildNoteEvents(2.0, s, 60.0)[0].tick, qint64(2 * 96 + 2));   // 60 BPM: 1 s per beat, 25 ms = 2.4 ticks
}

void TestMidiMatic::channelAndNoteAreClamped()
{
    ToolSettings s;
    s.midiChannel = 99;
    s.midiNote = 500;
    s.midiVelocity = 0;
    const auto ev = buildNoteEvents(0.0, s, 120.0);
    QCOMPARE(ev[0].channel, 15);
    QCOMPARE(ev[0].note, 127);
    QCOMPARE(ev[0].velocity, 1);
    s.midiChannel = -2;
    s.midiNote = -5;
    QCOMPARE(buildNoteEvents(0.0, s, 120.0)[0].channel, 0);
    QCOMPARE(buildNoteEvents(0.0, s, 120.0)[0].note, 0);
}

void TestMidiMatic::volumeIsTheVelocity()
{
    ToolSettings s;
    s.midiVelocity = 100;
    QCOMPARE(buildNoteEvents(0.0, s, 120.0)[0].velocity, 100);   // volume 127 leaves it as set
    s.volume = 64;
    QCOMPARE(buildNoteEvents(0.0, s, 120.0)[0].velocity, 50);
    s.volume = 1;
    QCOMPARE(buildNoteEvents(0.0, s, 120.0)[0].velocity, 1);     // never 0: that would be a note off
    s.volume = 0;
    QVERIFY(buildNoteEvents(0.0, s, 120.0).empty());             // silent
}

void TestMidiMatic::deviceAddressParsing()
{
    int c = -1, p = -1;
    QVERIFY(parseMidiAddress("20:0", &c, &p));
    QCOMPARE(c, 20);
    QCOMPARE(p, 0);
    QVERIFY(parseMidiAddress(" 128:3 ", &c, &p));
    QCOMPARE(c, 128);
    QVERIFY(!parseMidiAddress("", &c, &p));
    QVERIFY(!parseMidiAddress("20", &c, &p));
    QVERIFY(!parseMidiAddress("a:b", &c, &p));
    QVERIFY(!parseMidiAddress("300:0", &c, &p));
    QVERIFY(!parseMidiAddress("20:0:1", &c, &p));
}

void TestMidiMatic::beatChooserSelectsBeatsOfTheBar()
{
    auto m = midi(0b0110);                           // beats 2 and 3
    std::vector<PlannedHit> hits;
    m->plan(0.0, 8.0, hits);
    QCOMPARE(int(hits.size()), 4);
    QCOMPARE(hits[0].beat, 1.0);
    QCOMPARE(hits[1].beat, 2.0);
    QCOMPARE(hits[2].beat, 5.0);
    QCOMPARE(hits[3].beat, 6.0);
    QVERIFY(!m->usesSample());
    QVERIFY(m->hasBeatChooser());
}

void TestMidiMatic::schedulerEmitsEventsWithoutASample()
{
    DjtoolzScheduler sch(&beats_, &cache_, [](const Trigger&) { QFAIL("no audio expected"); });
    sch.setMidiSink([this](const MidiNoteEvent& e) { got_.push_back(e); });
    auto m = midi(0b1111);
    sch.addTool(m);
    run(sch, 0, 2000);
    // beats 0..4 on and off: ten events, ticks 0, 24, 96, 120, ...
    QVERIFY(got_.size() >= 10);
    QCOMPARE(got_[0].tick, qint64(0));
    QCOMPARE(got_[0].kind, MidiNoteEvent::NoteOn);
    QCOMPARE(got_[1].tick, qint64(24));
    QCOMPARE(got_[2].tick, qint64(96));
    QCOMPARE(got_[0].tag, m->id());
    QCOMPARE(got_[0].device, QString("20:1"));
}

void TestMidiMatic::schedulerHonoursOffsetAndDisabled()
{
    DjtoolzScheduler sch(&beats_, &cache_, [](const Trigger&) {});
    sch.setMidiSink([this](const MidiNoteEvent& e) { got_.push_back(e); });
    auto off = midi(0b1111, false);
    sch.addTool(off);
    run(sch, 0, 1000);
    QVERIFY(got_.empty());
    ToolSettings s = off->settings();
    s.enabled = true;
    s.beatMask = 0b0001;                             // only beat 1 of the bar
    s.offsetMs = -20;                                // -3.84 ticks at 120 BPM -> -4
    off->setSettings(s);
    run(sch, 1005, 6000);
    QVERIFY(got_.size() >= 2);
    QCOMPARE(got_[0].tick, qint64(4 * 96 - 4));      // the bar line at beat 4
    QCOMPARE(got_[0].kind, MidiNoteEvent::NoteOn);
}

void TestMidiMatic::sequencerStopReleasesNotes()
{
    DjtoolzScheduler sch(&beats_, &cache_, [](const Trigger&) {});
    sch.setMidiSink([this](const MidiNoteEvent& e) { got_.push_back(e); });
    sch.addTool(midi(0b1111));
    run(sch, 0, 600);
    got_.clear();
    beats_.isRunning = false;
    run(sch, 605, 700);
    int panics = 0;
    for (const auto& e : got_) {
        if (e.kind == MidiNoteEvent::AllNotesOff) {
            ++panics;
            QCOMPARE(e.channel, 9);
            QCOMPARE(e.device, QString("20:1"));
        }
    }
    QCOMPARE(panics, 1);                             // once, not on every tick
}

void TestMidiMatic::closedClockRefusesToSend()
{
    AlsaClock clock;                                 // never opened
    MidiOut out(&clock);
    MidiNoteEvent e;
    QVERIFY(!out.send(e));
    QVERIFY(!clock.sendDjtoolzMidi(0, 0, 36, 100, 0, -1, 0));
    QCOMPARE(clock.djtoolzPortId(), -1);
    MidiOut none(nullptr);
    QVERIFY(!none.send(e));
}

QTEST_MAIN(TestMidiMatic)
#include "test_midi_matic.moc"
