#include <QtTest/QtTest>
#include <QApplication>
#include <QComboBox>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include "midi/alsa_clock.h"
#include "ui/drum_editor.h"
#include "ui/drum_voice_selector.h"
#include "ui/sample_chooser.h"
#include "ui/djtoolz_controller.h"
#include "ui/djtoolz_tool_widget.h"
#include "ui/djtoolz_panel.h"

using namespace vdj;

class TestDjtoolzController : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        dir_ = new QTemporaryDir;
        for (const char* n : { "a.wav", "b.wav", "c.wav" }) {
            QFile f(dir_->path() + "/" + n);
            f.open(QIODevice::WriteOnly);
            f.write("x");
        }
        panel_ = new DjtoolzPanel(&clock_, dir_->path());
        panel_->setAudioEnabled(false);
        bomb_ = panel_->addTool(DjtoolzTool::Bombomatic);   // strip 1
        stab_ = panel_->addTool(DjtoolzTool::StabPlayer);   // strip 2
        shot_ = panel_->addTool(DjtoolzTool::OneShot);      // strip 3
        bomb_->sampleChooser()->setSample(dir_->path() + "/a.wav");
        bomb_->setSample(dir_->path() + "/a.wav");
        ctl_ = new DjtoolzMidiControl;
        write("");
    }
    void cleanup()
    {
        delete ctl_;
        delete panel_;
        delete dir_;
    }

    void muteTogglesTools()
    {
        QVERIFY(!bomb_->tool()->enabled());
        press(Control::Mute, 1);
        QVERIFY(bomb_->tool()->enabled());
        press(Control::Mute, 2);
        QVERIFY(stab_->tool()->enabled());
        press(Control::Mute, 3);
        QVERIFY(shot_->tool()->enabled());
        press(Control::Mute, 1);
        QVERIFY(!bomb_->tool()->enabled());
        QVERIFY(stab_->tool()->enabled());
    }

    void soloPressesTheGreenButton()
    {
        QSignalSpy bar(bomb_->oneBarButton(), &QToolButton::clicked);
        QSignalSpy fire(shot_->fireButton(), &QToolButton::clicked);
        press(Control::Solo, 1);
        press(Control::Solo, 3);
        QCOMPARE(bar.size(), 1);
        QCOMPARE(fire.size(), 1);
        QVERIFY(!stab_->pressGreenButton() || stab_->oneBarButton());     // the stab player has 1 bar
        press(Control::Solo, 4);                                           // no such tool: nothing
    }

    void recAndSelectStepTheSample()
    {
        const QString a = dir_->path() + "/a.wav", b = dir_->path() + "/b.wav", c = dir_->path() + "/c.wav";
        press(Control::Rec, 1);
        QCOMPARE(bomb_->tool()->settings().samplePath, b);
        press(Control::Rec, 1);
        press(Control::Rec, 1);                                            // clamps at the last file
        QCOMPARE(bomb_->tool()->settings().samplePath, c);
        press(Control::Select, 1);
        QCOMPARE(bomb_->tool()->settings().samplePath, b);
        press(Control::Select, 1);
        press(Control::Select, 1);
        QCOMPARE(bomb_->tool()->settings().samplePath, a);
        press(Control::Rec, 2);                                            // a tool without a sample ignores it
        QVERIFY(stab_->tool()->settings().samplePath.isEmpty());
    }

    void recDirectionCanBeSwapped()
    {
        write("#! rec_is_forward 0\n");
        press(Control::Select, 1);
        QCOMPARE(bomb_->tool()->settings().samplePath, dir_->path() + "/b.wav");
        press(Control::Rec, 1);
        QCOMPARE(bomb_->tool()->settings().samplePath, dir_->path() + "/a.wav");
    }

    void knobMapsToOffset()
    {
        write("#! pickup 0\n");
        knob(1, 0);   QCOMPARE(bomb_->tool()->settings().offsetMs, -40.0);
        knob(1, 63);  QCOMPARE(bomb_->tool()->settings().offsetMs, 0.0);
        knob(1, 64);  QCOMPARE(bomb_->tool()->settings().offsetMs, 0.0);
        knob(1, 127); QCOMPARE(bomb_->tool()->settings().offsetMs, 40.0);
        QCOMPARE(bomb_->offsetSpin()->value(), 40);
    }

    void faderMapsToVolume()
    {
        write("#! pickup 0\n");
        fader(2, 0);   QCOMPARE(stab_->tool()->settings().volume, 0);
        fader(2, 99);  QCOMPARE(stab_->tool()->settings().volume, 99);
        fader(2, 127); QCOMPARE(stab_->tool()->settings().volume, 127);
        QCOMPARE(stab_->currentVolume(), 127);
    }

    void pickupWaitsForTheOnScreenValue()
    {
        bomb_->setVolume(100);
        fader(1, 10);
        QCOMPARE(bomb_->currentVolume(), 100);
        fader(1, 60);
        QCOMPARE(bomb_->currentVolume(), 100);
        fader(1, 99);                                                      // within 2: takes over
        QCOMPARE(bomb_->currentVolume(), 99);
        fader(1, 20);
        QCOMPARE(bomb_->currentVolume(), 20);
        bomb_->setVolume(100);                                             // the mouse moved it: re-armed
        fader(1, 20);
        QCOMPARE(bomb_->currentVolume(), 100);
    }

    void knobPickup()
    {
        bomb_->offsetSpin()->setValue(30);
        knob(1, 0);
        QCOMPARE(bomb_->offsetSpin()->value(), 30);
        knob(1, 127);                                                      // crossed 30 ms
        QCOMPARE(bomb_->offsetSpin()->value(), 40);
    }

    void presetLoadRearmsPickup()
    {
        bomb_->setVolume(100);
        fader(1, 100);
        fader(1, 10);
        QCOMPARE(bomb_->currentVolume(), 10);
        panel_->applyState(panel_->state());
        auto* w = panel_->toolWidgets()[0];
        w->setVolume(90);
        fader(1, 5);
        QCOMPARE(w->currentVolume(), 90);
    }

    void stripWithoutToolDoesNothing()
    {
        write("#! pickup 0\n");
        press(Control::Mute, 5);
        fader(5, 3);
        knob(5, 3);
        QCOMPARE(panel_->toolWidgets().size(), 3);
        QVERIFY(!bomb_->tool()->enabled());
    }

    void toolWithoutOffsetIgnoresTheKnob()
    {
        auto* drums = panel_->addTool(DjtoolzTool::DrumMachine);       // strip 4
        write("#! pickup 0\n");
        knob(4, 127);
        QVERIFY(!drums->offsetSpin());
    }

    void drumModeIgnoresToolActions()
    {
        write("#! pickup 0\n");
        panel_->setControlMode(1);
        QCOMPARE(int(panel_->controller()->mode()), int(DjtoolzController::DrumMachine));
        press(Control::Mute, 1);
        fader(1, 5);
        QVERIFY(!bomb_->tool()->enabled());
        QVERIFY(bomb_->currentVolume() != 5);
        panel_->setControlMode(0);
        press(Control::Mute, 1);
        QVERIFY(bomb_->tool()->enabled());
    }

    void modeToggleFlipsTheModeAndTheCombo()
    {
        write("mode_toggle 0 60 127\n");
        ctl_->onControlEvent(1, 60, 127, "x");
        QCOMPARE(panel_->controlModeCombo()->currentData().toInt(), 1);
        ctl_->onControlEvent(1, 60, 127, "x");
        QCOMPARE(panel_->controlModeCombo()->currentData().toInt(), 0);
    }

    void modeIsSavedInTheState()
    {
        panel_->setControlMode(1);
        QCOMPARE(panel_->state().controlMode, 1);
        DjtoolzState st = panel_->state();
        st.controlMode = 0;
        panel_->applyState(st);
        QCOMPARE(int(panel_->controller()->mode()), 0);
    }

    void modeChangeRearmsPickup()
    {
        bomb_->setVolume(100);
        fader(1, 100);
        fader(1, 10);
        QCOMPARE(bomb_->currentVolume(), 10);
        panel_->setControlMode(1);
        panel_->setControlMode(0);
        bomb_->setVolume(90);
        fader(1, 4);
        QCOMPARE(bomb_->currentVolume(), 90);
    }

    void drumStripsDriveVoices()
    {
        write("#! pickup 0\n");
        auto* dr = panel_->addTool(DjtoolzTool::DrumMachine);
        panel_->setControlMode(1);
        press(Control::Mute, 3);
        QVERIFY(dr->tool()->settings().drums[2].muted);          // SY only
        for (int v = 0; v < kDrumVoices; ++v)
            QCOMPARE(dr->tool()->settings().drums[v].muted, v == 2);
        QVERIFY(dr->drumEditor()->selector()->muted(2));
        press(Control::Mute, 3);
        QVERIFY(!dr->tool()->settings().drums[2].muted);
        // the shown voice is BD; strip 6 is OH
        QCOMPARE(dr->drumEditor()->selectedVoice(), 0);
        fader(6, 33);
        knob(6, 127);
        QCOMPARE(dr->tool()->settings().drums[5].volume, 33);
        QCOMPARE(dr->tool()->settings().drums[5].offsetMs, 40.0);
        QCOMPARE(dr->tool()->settings().drums[0].volume, 127);   // the shown voice is untouched
        // the shown voice follows its strip
        fader(1, 50);
        knob(1, 0);
        QCOMPARE(dr->drumEditor()->volumeSlider()->value(), 50);
        QCOMPARE(dr->drumEditor()->offsetSpin()->value(), -40);
        QCOMPARE(dr->tool()->settings().drums[0].volume, 50);
        // strips 7, 8 and the other buttons do nothing
        const ToolSettings before = dr->tool()->settings();
        press(Control::Mute, 7); press(Control::Mute, 8); fader(7, 1); fader(8, 1); knob(7, 1);
        press(Control::Solo, 1); press(Control::Rec, 1); press(Control::Select, 1);
        const ToolSettings after = dr->tool()->settings();
        for (int v = 0; v < kDrumVoices; ++v)
            QVERIFY(before.drums[v] == after.drums[v]);
        QVERIFY(!bomb_->tool()->enabled());                       // the tool strips are not active in this mode
    }

    void drumPickup()
    {
        auto* dr = panel_->addTool(DjtoolzTool::DrumMachine);
        panel_->setControlMode(1);
        dr->drumEditor()->setVoiceVolume(4, 100);
        fader(5, 10);
        QCOMPARE(dr->tool()->settings().drums[4].volume, 100);
        fader(5, 99);
        QCOMPARE(dr->tool()->settings().drums[4].volume, 99);
        fader(5, 30);
        QCOMPARE(dr->tool()->settings().drums[4].volume, 30);
    }

    void trackButtonsStepTheKits()
    {
        auto* dr = panel_->addTool(DjtoolzTool::DrumMachine);
        auto* ed = dr->drumEditor();
        for (const char* n : { "a", "b", "c" }) {
            ed->selectVoice(0);
            ed->setVoiceVolume(0, n[0] - 'a' + 10);                 // tell the kits apart by BD volume
            QVERIFY(ed->saveKitAs(n));
        }
        ed->kitCombo()->setCurrentIndex(0);
        ed->setVoiceMuted(3, true);
        panel_->setControlMode(1);
        trackFwd();                                                 // from "(kits)": the first
        QCOMPARE(ed->kitCombo()->currentText(), QString("a"));
        QCOMPARE(dr->tool()->settings().drums[0].volume, 10);
        trackFwd(); QCOMPARE(ed->kitCombo()->currentText(), QString("b"));
        trackFwd(); QCOMPARE(ed->kitCombo()->currentText(), QString("c"));
        trackFwd(); QCOMPARE(ed->kitCombo()->currentText(), QString("c"));      // stays at the end
        QCOMPARE(dr->tool()->settings().drums[0].volume, 12);
        trackBack(); QCOMPARE(ed->kitCombo()->currentText(), QString("b"));
        trackBack(); trackBack();
        QCOMPARE(ed->kitCombo()->currentText(), QString("a"));              // and stops at the start
        QVERIFY(dr->tool()->settings().drums[3].muted);                    // the mix survives the kit changes
        // from "(kits)" back goes to the last
        ed->kitCombo()->setCurrentIndex(0);
        trackBack();
        QCOMPARE(ed->kitCombo()->currentText(), QString("c"));
    }

    void trackButtonsDoNothingInToolsMode()
    {
        auto* dr = panel_->addTool(DjtoolzTool::DrumMachine);
        QVERIFY(dr->drumEditor()->saveKitAs("a"));
        dr->drumEditor()->kitCombo()->setCurrentIndex(0);
        trackFwd();
        QCOMPARE(dr->drumEditor()->kitCombo()->currentIndex(), 0);
    }

    void noDrumMachineIsHarmless()
    {
        panel_->setControlMode(1);
        press(Control::Mute, 1); fader(1, 5); knob(1, 5); trackFwd(); trackBack();
        QCOMPARE(panel_->toolWidgets().size(), 3);
        QVERIFY(!panel_->drumTarget());
    }

    void lastTouchedDrumMachineIsTheTarget()
    {
        write("#! pickup 0\n");
        auto* d1 = panel_->addTool(DjtoolzTool::DrumMachine);
        auto* d2 = panel_->addTool(DjtoolzTool::DrumMachine);
        QCOMPARE(panel_->drumTarget(), d1);                         // none touched: the first
        panel_->setControlMode(1);
        press(Control::Mute, 1);
        QVERIFY(d1->tool()->settings().drums[0].muted);
        QVERIFY(!d2->tool()->settings().drums[0].muted);
        d2->drumEditor()->setVoiceVolume(1, 77);                    // an edit on the second
        QCOMPARE(panel_->drumTarget(), d2);
        press(Control::Mute, 1);
        QVERIFY(d2->tool()->settings().drums[0].muted);
        QTest::mouseClick(d1, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));   // a click on the first
        QCOMPARE(panel_->drumTarget(), d1);
        press(Control::Mute, 1);
        QVERIFY(!d1->tool()->settings().drums[0].muted);
    }

    void targetChangeRearmsPickup()
    {
        auto* d1 = panel_->addTool(DjtoolzTool::DrumMachine);
        auto* d2 = panel_->addTool(DjtoolzTool::DrumMachine);
        panel_->setControlMode(1);
        d1->drumEditor()->setVoiceVolume(0, 100);
        fader(1, 100);
        fader(1, 10);                                               // following
        QCOMPARE(d1->drumEditor()->voiceVolume(0), 10);
        d2->drumEditor()->setVoiceVolume(0, 90);                    // touch the second: target changes, re-armed
        fader(1, 5);
        QCOMPARE(d2->drumEditor()->voiceVolume(0), 90);
    }

private:
    void trackFwd() { ctl_->onControlEvent(1, 59, 127, "x"); }
    void trackBack() { ctl_->onControlEvent(1, 58, 127, "x"); }
    // All events go through the same path as the real controller: an .adjmm file loaded into DjtoolzMidiControl.
    void write(const QByteArray& extra)
    {
        QByteArray t = extra + "\n";
        for (int s = 1; s <= 8; ++s) {
            t += QStringLiteral("strip%1_mute 0 %2 127\nstrip%1_solo 0 %3 127\nstrip%1_rec 0 %4 127\nstrip%1_select 0 %5 127\n"
                                "strip%1_knob 0 %6 *\nstrip%1_fader 0 %7 *\n")
                     .arg(s).arg(48 + s).arg(32 + s).arg(64 + s).arg(80 + s).arg(16 + s).arg(s).toUtf8();
        }
        t += "track_back 0 58 127\ntrack_fwd 0 59 127\n";
        const QString p = dir_->filePath("ctl.adjmm");
        QFile f(p);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(t);
        f.close();
        QVERIFY(ctl_->loadFromFile(p));
        panel_->controller()->attach(ctl_);
    }
    void press(Control c, int strip)
    {
        const int base = c == Control::Mute ? 48 : c == Control::Solo ? 32 : c == Control::Rec ? 64 : 80;
        ctl_->onControlEvent(1, base + strip, 127, "x");
        ctl_->onControlEvent(1, base + strip, 0, "x");
    }
    void knob(int strip, int v) { ctl_->onControlEvent(1, 16 + strip, v, "x"); }
    void fader(int strip, int v) { ctl_->onControlEvent(1, strip, v, "x"); }

    QTemporaryDir* dir_ = nullptr;
    AlsaClock clock_;
    DjtoolzPanel* panel_ = nullptr;
    DjtoolzMidiControl* ctl_ = nullptr;
    DjtoolzToolWidget *bomb_ = nullptr, *stab_ = nullptr, *shot_ = nullptr;
};

QTEST_MAIN(TestDjtoolzController)
#include "test_djtoolz_controller.moc"
