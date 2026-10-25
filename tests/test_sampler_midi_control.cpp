#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QMetaMethod>
#include "midi/sampler_midi_control.h"

using namespace vdj;

class TestSamplerMidiControl : public QObject {
    Q_OBJECT

    static SamplerMidiControl* make(const QByteArray& text, QTemporaryDir& dir)
    {
        const QString p = dir.filePath("c.adjmm");
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write(text);
        f.close();
        auto* c = new SamplerMidiControl;
        c->loadFromFile(p);
        return c;
    }

private slots:
    void initTestCase() { qRegisterMetaType<vdj::Control>("vdj::Control"); }

    void everyControlHasASignal()
    {
        QTemporaryDir d;
        std::unique_ptr<SamplerMidiControl> c(make(
            "strip2_mute 0 48 127\nstrip3_solo 0 32 127\nstrip4_rec 0 64 127\nstrip5_select 0 70 127\n"
            "strip6_knob 0 21 *\nstrip7_fader 0 6 *\ntrack_back 0 58 127\ntrack_fwd 0 59 127\nmode_toggle 0 60 127\n"
            "strip1_mute 0 N 36\n", d));
        QSignalSpy btn(c.get(), &SamplerMidiControl::buttonPressed);
        QSignalSpy knob(c.get(), &SamplerMidiControl::knobMoved);
        QSignalSpy fad(c.get(), &SamplerMidiControl::faderMoved);
        QSignalSpy back(c.get(), &SamplerMidiControl::trackBack);
        QSignalSpy fwd(c.get(), &SamplerMidiControl::trackForward);
        QSignalSpy mode(c.get(), &SamplerMidiControl::modeToggleRequested);

        c->onControlEvent(1, 48, 127, "x");
        c->onControlEvent(1, 32, 127, "x");
        c->onControlEvent(1, 64, 127, "x");
        c->onControlEvent(1, 70, 127, "x");
        QCOMPARE(btn.size(), 4);
        QCOMPARE(btn[0][0].value<Control>(), Control::Mute);  QCOMPARE(btn[0][1].toInt(), 2);
        QCOMPARE(btn[1][0].value<Control>(), Control::Solo);  QCOMPARE(btn[1][1].toInt(), 3);
        QCOMPARE(btn[2][0].value<Control>(), Control::Rec);   QCOMPARE(btn[2][1].toInt(), 4);
        QCOMPARE(btn[3][0].value<Control>(), Control::Select);QCOMPARE(btn[3][1].toInt(), 5);
        c->onControlEvent(1, 21, 99, "x");
        QCOMPARE(knob.size(), 1); QCOMPARE(knob[0][0].toInt(), 6); QCOMPARE(knob[0][1].toInt(), 99);
        c->onControlEvent(1, 6, 5, "x");
        QCOMPARE(fad.size(), 1); QCOMPARE(fad[0][0].toInt(), 7); QCOMPARE(fad[0][1].toInt(), 5);
        c->onControlEvent(1, 58, 127, "x");
        c->onControlEvent(1, 59, 127, "x");
        c->onControlEvent(1, 60, 127, "x");
        QCOMPARE(back.size(), 1); QCOMPARE(fwd.size(), 1); QCOMPARE(mode.size(), 1);
        c->onNoteEvent(1, 36, 90, true, "x");                     // N op presses a button
        QCOMPARE(btn.size(), 5); QCOMPARE(btn[4][1].toInt(), 1);
        c->onNoteEvent(1, 36, 0, false, "x");
        QCOMPARE(btn.size(), 5);
    }

    void releaseAndUnassignedDoNothing()
    {
        QTemporaryDir d;
        std::unique_ptr<SamplerMidiControl> c(make("strip1_mute 0 48 127\n", d));
        QSignalSpy btn(c.get(), &SamplerMidiControl::buttonPressed);
        c->onControlEvent(1, 48, 0, "x");
        c->onControlEvent(1, 49, 127, "x");
        QCOMPARE(btn.size(), 0);
    }

    void sourceFilter()
    {
        QTemporaryDir d;
        std::unique_ptr<SamplerMidiControl> c(make("#! source nanoKONTROL\nstrip1_mute 0 48 127\n", d));
        QSignalSpy btn(c.get(), &SamplerMidiControl::buttonPressed);
        c->onControlEvent(1, 48, 127, "XDJ-700");
        QCOMPARE(btn.size(), 0);
        c->onControlEvent(1, 48, 127, "KORG nanokontrol Studio");
        QCOMPARE(btn.size(), 1);
        c->onNoteEvent(1, 1, 1, true, "XDJ-700");
        QCOMPARE(btn.size(), 1);
    }

    void emptyFilterAcceptsAll()
    {
        QTemporaryDir d;
        std::unique_ptr<SamplerMidiControl> c(make("strip1_mute 0 48 127\n", d));
        QSignalSpy btn(c.get(), &SamplerMidiControl::buttonPressed);
        c->onControlEvent(1, 48, 127, "anything");
        c->onControlEvent(1, 48, 127, "");
        QCOMPARE(btn.size(), 2);
    }

    void disabledDoesNothing()
    {
        QTemporaryDir d;
        std::unique_ptr<SamplerMidiControl> c(make("strip1_mute 0 48 127\n", d));
        QSignalSpy btn(c.get(), &SamplerMidiControl::buttonPressed);
        c->setEnabled(false);
        c->onControlEvent(1, 48, 127, "x");
        QCOMPARE(btn.size(), 0);
    }

    void learnReportsAndDoesNotAct()
    {
        QTemporaryDir d;
        std::unique_ptr<SamplerMidiControl> c(make("strip1_mute 0 48 127\n", d));
        QSignalSpy btn(c.get(), &SamplerMidiControl::buttonPressed);
        QSignalSpy learned(c.get(), &SamplerMidiControl::learned);
        c->setLearning(true);
        c->onControlEvent(3, 48, 127, "x");
        c->onNoteEvent(2, 40, 100, true, "x");
        c->onNoteEvent(2, 40, 0, false, "x");                      // release is not learned
        QCOMPARE(btn.size(), 0);
        QCOMPARE(learned.size(), 2);
        QCOMPARE(learned[0][0].toInt(), 3); QCOMPARE(learned[0][1].toInt(), 48);
        QVERIFY(!learned[0][2].toBool()); QCOMPARE(learned[0][3].toInt(), 127);
        QVERIFY(learned[1][2].toBool()); QCOMPARE(learned[1][1].toInt(), 40);
        c->setLearning(false);
        c->onControlEvent(3, 48, 127, "x");
        QCOMPARE(btn.size(), 1);
    }

    void changedMapAppliesAtOnce()
    {
        QTemporaryDir d;
        std::unique_ptr<SamplerMidiControl> c(make("strip1_mute 0 48 127\n", d));
        QSignalSpy btn(c.get(), &SamplerMidiControl::buttonPressed);
        AdjmmMap m;
        AdjmmOp op; op.channel = 0; op.controller = 10; op.value = 127;
        m.named["strip2_solo"] = op;
        c->setMap(SamplerCtlMap::fromAdjmm(m));
        c->onControlEvent(1, 48, 127, "x");
        QCOMPARE(btn.size(), 0);
        c->onControlEvent(1, 10, 127, "x");
        QCOMPARE(btn.size(), 1);
    }

    void loadFailureKeepsMap()
    {
        QTemporaryDir d;
        std::unique_ptr<SamplerMidiControl> c(make("strip1_mute 0 48 127\n", d));
        QVERIFY(!c->loadFromFile(d.filePath("missing.adjmm")));
        QCOMPARE(c->map().opCount(), 1);
    }

    void midiMapInputDeclaresSignals()
    {
        const QMetaObject& mo = MidiMapInput::staticMetaObject;
        QVERIFY(mo.indexOfSignal("controlEvent(int,int,int,QString)") >= 0);
        QVERIFY(mo.indexOfSignal("noteEvent(int,int,int,bool,QString)") >= 0);
        MidiMapInput in;
        QSignalSpy a(&in, &MidiMapInput::controlEvent);
        QSignalSpy b(&in, &MidiMapInput::noteEvent);
        QVERIFY(a.isValid() && b.isValid());
    }
};

QTEST_MAIN(TestSamplerMidiControl)
#include "test_sampler_midi_control.moc"
