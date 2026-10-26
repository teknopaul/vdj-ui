#include <QtTest/QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QGraphicsEffect>
#include <QSignalSpy>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSlider>
#include <QTemporaryDir>
#include <QToolButton>
#include <QWheelEvent>
#include "midi/alsa_clock.h"
#include "ui/drum_editor.h"
#include "ui/sample_chooser.h"
#include "ui/djtoolz_tool_widget.h"
#include "ui/djtoolz_panel.h"
#include "ui/sample_tree_model.h"
#include "ui/drum_voice_selector.h"
#include "ui/djtoolz_icons.h"
#include "ui/step_chooser.h"
#include "ui/swing_slider.h"

using namespace vdj;

class TestDrumUi : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        dir_ = new QTemporaryDir;
        a_ = touch(dir_->path() + "/a.wav");
        b_ = touch(dir_->path() + "/b.wav");
        c_ = touch(dir_->path() + "/c.wav");
        panel_ = new DjtoolzPanel(&clock_, dir_->path());
        panel_->setAudioEnabled(false);
    }
    void cleanup()
    {
        delete panel_;
        panel_ = nullptr;
        delete dir_;
        dir_ = nullptr;
    }
    void selectorStartsOnBd();
    void selectorIsExclusiveAndEmits();
    void selectorKeepsOneLit();
    void selectorIconsFollowTheSelection();
    void stepClickCycle();
    void stepCtrlClickCycle();
    void stepSetStepsDoesNotEmit();
    void stepIconsPerState();
    void stepGapAfterEveryFourth();
    void stepChangedArguments();
    void stepDisabledShowsGrey();
    void stepIgnoresRightClick();
    void swingSliderBasics();
    void swingSliderIsInside();
    void drumMachineWidgetHasAnEditorAndNoGenericRows();
    void selectingAVoiceShowsItsSettings();
    void stepChooserEditsOnlyTheSelectedVoice();
    void perVoiceSampleViaUpDownAndSetSample();
    void wheelOverTheEditorIsTheVoiceVolume();
    void wheelElsewhereIsTheToolVolume();
    void smallWheelDeltasInTheEditor();
    void swingSliderReachesTheSettings();
    void chokeSpinnerValuesAndKeys();
    void chokeSpinnerSnapsTypedValues();
    void chokeIsPerVoice();
    void onOffByButtonAndFunctionKeyHook();
    void oneBarButtonIsThere();
    void drumMachineTakesAWholeRow();
    void dembomatikHasASwingSlider();
    void presetRoundTripKeepsEverything();
    void voiceMuteDimsAndAltClickToggles();

private:
    QTemporaryDir* dir_ = nullptr;
    AlsaClock clock_;
    DjtoolzPanel* panel_ = nullptr;
    QString a_, b_, c_;

    static QString touch(const QString& path)
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("x");
        return path;
    }
    DjtoolzToolWidget* newDrums()
    {
        return panel_->addTool(DjtoolzTool::DrumMachine);
    }
    static void wheel(QWidget* target, int notches, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QWheelEvent e(QPointF(5, 5), target->mapToGlobal(QPointF(5, 5)), QPoint(), QPoint(0, notches * 120), Qt::NoButton, mods,
                      Qt::NoScrollPhase, false);
        QApplication::sendEvent(target, &e);
    }
    static QImage image(const QIcon& i, int size = 24) { return i.pixmap(size, size).toImage(); }
    static void click(QWidget* w, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::mouseClick(w, Qt::LeftButton, m, QPoint(5, 5)); }
};

void TestDrumUi::selectorStartsOnBd()
{
    DrumVoiceSelector s;
    for (int v = 0; v < 6; ++v)
        QCOMPARE(s.button(v)->size(), QSize(24, 24));      // the same size as the other round buttons
    QCOMPARE(s.selected(), 0);
    QVERIFY(s.button(0)->isChecked());
    for (int v = 1; v < 6; ++v)
        QVERIFY(!s.button(v)->isChecked());
}

void TestDrumUi::selectorIsExclusiveAndEmits()
{
    DrumVoiceSelector s;
    s.show();
    QSignalSpy spy(&s, &DrumVoiceSelector::selectedChanged);
    click(s.button(4));                              // HH
    QCOMPARE(s.selected(), 4);
    QVERIFY(s.button(4)->isChecked());
    QVERIFY(!s.button(0)->isChecked());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toInt(), 4);
    click(s.button(5));
    QCOMPARE(s.selected(), 5);
    int lit = 0;
    for (int v = 0; v < 6; ++v)
        lit += s.button(v)->isChecked();
    QCOMPARE(lit, 1);
    s.setSelected(2);                                // no signal
    QCOMPARE(spy.count(), 2);
    QCOMPARE(s.selected(), 2);
    QVERIFY(s.button(2)->isChecked() && !s.button(5)->isChecked());
    s.setSelected(9);                                // out of range: ignored
    QCOMPARE(s.selected(), 2);
}

void TestDrumUi::selectorKeepsOneLit()
{
    DrumVoiceSelector s;
    s.show();
    QSignalSpy spy(&s, &DrumVoiceSelector::selectedChanged);
    click(s.button(0));                              // clicking the lit one: stays lit, no signal
    QVERIFY(s.button(0)->isChecked());
    QCOMPARE(spy.count(), 0);
}

void TestDrumUi::selectorIconsFollowTheSelection()
{
    DrumVoiceSelector s;
    s.show();
    QCOMPARE(image(s.button(0)->icon()), image(drumVoiceIcon(0, true)));
    QCOMPARE(image(s.button(1)->icon()), image(drumVoiceIcon(1, false)));
    click(s.button(1));
    QCOMPARE(image(s.button(1)->icon()), image(drumVoiceIcon(1, true)));
    QCOMPARE(image(s.button(0)->icon()), image(drumVoiceIcon(0, false)));
    QVERIFY(s.button(0)->toolTip().contains("Bass drum"));
    QVERIFY(s.button(5)->toolTip().contains("Open hi-hat"));
}

void TestDrumUi::stepClickCycle()
{
    StepChooser c;
    c.show();
    QCOMPARE(int(c.step(3)), int(StepOff));
    click(c.button(3));
    QCOMPARE(int(c.step(3)), int(StepOn));
    click(c.button(3));
    QCOMPARE(int(c.step(3)), int(StepOff));
    click(c.button(3), Qt::ControlModifier);         // -> half
    QCOMPARE(int(c.step(3)), int(StepHalf));
    click(c.button(3));                              // a plain click on a pink note turns it off
    QCOMPARE(int(c.step(3)), int(StepOff));
    click(c.button(3));
    QCOMPARE(int(c.step(3)), int(StepOn));
    QCOMPARE(int(c.step(4)), int(StepOff));          // the others are untouched
}

void TestDrumUi::stepCtrlClickCycle()
{
    StepChooser c;
    c.show();
    QSignalSpy spy(&c, &StepChooser::changed);
    click(c.button(0), Qt::ControlModifier);         // off -> half
    QCOMPARE(int(c.step(0)), int(StepHalf));
    click(c.button(0), Qt::ControlModifier);         // ctrl on a pink note: it stays pink, nothing changes
    QCOMPARE(int(c.step(0)), int(StepHalf));
    QCOMPARE(spy.count(), 1);
    click(c.button(0));                              // pink -> off with a plain click
    QCOMPARE(int(c.step(0)), int(StepOff));
    click(c.button(0));                              // on
    click(c.button(0), Qt::ControlModifier);         // on -> half
    QCOMPARE(int(c.step(0)), int(StepHalf));
    QCOMPARE(int(StepChooser::nextState(StepOn, true)), int(StepHalf));
    QCOMPARE(int(StepChooser::nextState(StepOff, true)), int(StepHalf));
    QCOMPARE(int(StepChooser::nextState(StepHalf, true)), int(StepHalf));
    QCOMPARE(int(StepChooser::nextState(StepHalf, false)), int(StepOff));
    QCOMPARE(int(StepChooser::nextState(StepOn, false)), int(StepOff));
    QCOMPARE(int(StepChooser::nextState(StepOff, false)), int(StepOn));
}

void TestDrumUi::stepSetStepsDoesNotEmit()
{
    StepChooser c;
    QSignalSpy spy(&c, &StepChooser::changed);
    quint8 steps[kDrumSteps] = { 1, 0, 2, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 2, 0, 9 };
    c.setSteps(steps);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(int(c.step(0)), 1);
    QCOMPARE(int(c.step(2)), 2);
    QCOMPARE(int(c.step(15)), int(StepHalf));        // out of range values are clamped
    QCOMPARE(c.steps().size(), 16);
    QCOMPARE(int(c.steps()[13]), 2);
}

void TestDrumUi::stepIconsPerState()
{
    StepChooser c;
    quint8 steps[kDrumSteps] = {};
    steps[1] = StepOn;
    steps[2] = StepHalf;
    c.setSteps(steps);
    QCOMPARE(image(c.button(0)->icon()), image(djtoolzIcon(DjtoolzIcon::Yellow)));
    QCOMPARE(image(c.button(1)->icon()), image(djtoolzIcon(DjtoolzIcon::Red)));
    QCOMPARE(image(c.button(2)->icon()), image(djtoolzIcon(DjtoolzIcon::Pink)));
    c.show();
    click(c.button(0));
    QCOMPARE(image(c.button(0)->icon()), image(djtoolzIcon(DjtoolzIcon::Red)));
}

void TestDrumUi::stepGapAfterEveryFourth()
{
    StepChooser c;
    c.resize(600, 40);
    c.show();
    QTest::qWait(30);
    auto x = [&](int i) { return c.button(i)->x(); };
    const int normal = x(1) - x(0);
    QCOMPARE(normal, 26);                            // 24 px + 2 px spacing
    QCOMPARE(x(2) - x(1), normal);
    QCOMPARE(x(3) - x(2), normal);
    QVERIFY(x(4) - x(3) > normal);                   // the gap after the 4th
    QVERIFY(x(8) - x(7) > normal);
    QVERIFY(x(12) - x(11) > normal);
    QCOMPARE(x(5) - x(4), normal);
    QCOMPARE(x(13) - x(12), normal);
}

void TestDrumUi::stepChangedArguments()
{
    StepChooser c;
    c.show();
    QSignalSpy spy(&c, &StepChooser::changed);
    click(c.button(7));
    click(c.button(9), Qt::ControlModifier);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy[0][0].toInt(), 7);
    QCOMPARE(spy[0][1].value<quint8>(), quint8(StepOn));
    QCOMPARE(spy[1][0].toInt(), 9);
    QCOMPARE(spy[1][1].value<quint8>(), quint8(StepHalf));
}

void TestDrumUi::stepDisabledShowsGrey()
{
    StepChooser c;
    c.show();
    click(c.button(0));
    c.setEnabled(false);
    QCOMPARE(image(c.button(0)->icon()), image(djtoolzIcon(DjtoolzIcon::Grey)));
    QCOMPARE(image(c.button(5)->icon()), image(djtoolzIcon(DjtoolzIcon::Grey)));
    QSignalSpy spy(&c, &StepChooser::changed);
    click(c.button(1));                              // a disabled chooser does not react
    QCOMPARE(spy.count(), 0);
    c.setEnabled(true);
    QCOMPARE(image(c.button(0)->icon()), image(djtoolzIcon(DjtoolzIcon::Red)));
}

void TestDrumUi::stepIgnoresRightClick()
{
    StepChooser c;
    c.show();
    QTest::mouseClick(c.button(2), Qt::RightButton, Qt::NoModifier, QPoint(5, 5));
    QCOMPARE(int(c.step(2)), int(StepOff));
}

void TestDrumUi::swingSliderBasics()
{
    SwingSlider s;
    QCOMPARE(s.value(), 0);
    QSignalSpy spy(&s, &SwingSlider::valueChanged);
    s.setValue(64);                                  // no signal
    QCOMPARE(spy.count(), 0);
    QCOMPARE(s.value(), 64);
    s.slider()->setValue(100);                       // the user drags
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toInt(), 100);
    QCOMPARE(s.slider()->maximum(), 127);
    QVERIFY(s.toolTip().contains("quarter of a beat"));
}

void TestDrumUi::swingSliderIsInside()
{
    SwingSlider s;
    QToolButton other;
    QVERIFY(SwingSlider::isInside(&s));
    QVERIFY(SwingSlider::isInside(s.slider()));      // its children count too
    QVERIFY(!SwingSlider::isInside(&other));
    QVERIFY(!SwingSlider::isInside(nullptr));
}

void TestDrumUi::drumMachineWidgetHasAnEditorAndNoGenericRows()
{
    auto* w = newDrums();
    QVERIFY(w);
    QVERIFY(w->drumEditor());
    QVERIFY(!w->sampleChooser());                    // the samples are per voice
    QVERIFY(!w->beatChooser());
    QVERIFY(!w->offsetSpin());                       // no tool level offset
    QVERIFY(!w->reverseSyncBox());
    QVERIFY(!w->swingSlider());                      // the swing is in the editor
    QVERIFY(w->drumEditor()->swing());
    QVERIFY(w->oneBarButton());
    QVERIFY(w->wantsFullRow());
    QCOMPARE(w->tool()->typeName(), QString("DR-6S"));
    QVERIFY(w->volumeSlider());                      // the tool (master) volume
    QVERIFY(w->typeLabel()->font().bold());          // the type is bold, the name the user types is not
    QVERIFY(!w->nameEdit()->font().bold());
    QCOMPARE(w->typeLabel()->text(), QString("DR-6S"));
}

void TestDrumUi::selectingAVoiceShowsItsSettings()
{
    auto* w = newDrums();
    auto m = w->tool();
    ToolSettings s = m->settings();
    s.drums[0].samplePath = a_;
    s.drums[0].offsetMs = -5;
    s.drums[0].volume = 90;
    s.drums[0].steps[0] = StepOn;
    s.drums[3].samplePath = c_;
    s.drums[3].offsetMs = 12;
    s.drums[3].volume = 40;
    s.drums[3].chokeSteps = 4;
    s.drums[3].steps[6] = StepHalf;
    m->setSettings(s);
    auto* ed = w->drumEditor();
    ed->reload();
    QCOMPARE(ed->selectedVoice(), 0);
    QCOMPARE(ed->sampleChooser()->sample(), a_);
    QCOMPARE(ed->offsetSpin()->value(), -5);
    QCOMPARE(ed->volumeSlider()->value(), 90);
    QCOMPARE(int(ed->stepChooser()->step(0)), int(StepOn));
    click(ed->selector()->button(3));                // CL
    QCOMPARE(ed->selectedVoice(), 3);
    QCOMPARE(ed->sampleChooser()->sample(), c_);
    QCOMPARE(ed->offsetSpin()->value(), 12);
    QCOMPARE(ed->volumeSlider()->value(), 40);
    QCOMPARE(ed->chokeSpin()->value(), 4);
    QCOMPARE(int(ed->stepChooser()->step(6)), int(StepHalf));
    QCOMPARE(int(ed->stepChooser()->step(0)), int(StepOff));
    // selecting does not change anything in the tool
    QCOMPARE(m->settings().drums[0].samplePath, a_);
    QCOMPARE(m->settings().drums[3].chokeSteps, 4);
}

void TestDrumUi::stepChooserEditsOnlyTheSelectedVoice()
{
    auto* w = newDrums();
    auto* ed = w->drumEditor();
    w->show();
    click(ed->stepChooser()->button(4));             // BD step 5 on
    click(ed->selector()->button(1));                // SD
    click(ed->stepChooser()->button(8), Qt::ControlModifier);   // SD step 9 half
    const ToolSettings s = w->tool()->settings();
    QCOMPARE(int(s.drums[0].steps[4]), int(StepOn));
    QCOMPARE(int(s.drums[1].steps[8]), int(StepHalf));
    QCOMPARE(int(s.drums[1].steps[4]), int(StepOff));
    QCOMPARE(int(s.drums[0].steps[8]), int(StepOff));
    for (int v = 2; v < 6; ++v)
        QCOMPARE(drumPatternToString(s.drums[v].steps), QString("0000000000000000"));
}

void TestDrumUi::perVoiceSampleViaUpDownAndSetSample()
{
    auto* w = newDrums();
    auto* ed = w->drumEditor();
    click(ed->selector()->button(2));                // SY
    ed->sampleChooser()->setSample(a_);
    ed->sampleChooser()->downButton()->click();      // a -> b
    QCOMPARE(w->tool()->settings().drums[2].samplePath, b_);
    ed->sampleChooser()->downButton()->click();
    QCOMPARE(w->tool()->settings().drums[2].samplePath, c_);
    ed->sampleChooser()->upButton()->click();
    QCOMPARE(w->tool()->settings().drums[2].samplePath, b_);
    QVERIFY(w->tool()->settings().drums[0].samplePath.isEmpty());          // the other voices are untouched
    QVERIFY(w->tool()->settings().drums[5].samplePath.isEmpty());
    // the tree's right click menu offers the DR-6S as a submenu with its six voices
    panel_->setSampleFolder(dir_->path());
    QTest::qWait(300);
    QMenu* menu = panel_->sendToMenu(panel_->treeModel()->indexForPath(a_), panel_);
    QVERIFY(menu);
    QMenu* sub = nullptr;
    for (QAction* act : menu->actions())
        if (act->menu() && act->text().contains("DR-6S"))
            sub = act->menu();
    QVERIFY(sub);
    QCOMPARE(sub->actions().size(), 6);
    QCOMPARE(sub->actions()[3]->text(), QString("CL"));
    sub->actions()[3]->trigger();                    // send to CL
    QCOMPARE(w->tool()->settings().drums[3].samplePath, a_);
    QCOMPARE(ed->selectedVoice(), 3);
    QVERIFY(w->tool()->settings().drums[0].samplePath.isEmpty());
    delete menu;
}

void TestDrumUi::wheelOverTheEditorIsTheVoiceVolume()
{
    auto* w = newDrums();
    w->show();
    auto* ed = w->drumEditor();
    click(ed->selector()->button(4));                // HH
    QCOMPARE(w->tool()->settings().drums[4].volume, 127);
    wheel(ed->stepChooser()->button(0), -3);         // over a step button
    QCOMPARE(w->tool()->settings().drums[4].volume, 124);
    wheel(ed->offsetSpin(), -1);                     // over the offset spin box: volume, not the offset
    QCOMPARE(w->tool()->settings().drums[4].volume, 123);
    QCOMPARE(w->tool()->settings().drums[4].offsetMs, 0.0);
    wheel(ed->sampleChooser(), -1, Qt::ShiftModifier);
    QCOMPARE(w->tool()->settings().drums[4].volume, 113);
    wheel(ed, +60);                                  // stops at 127
    QCOMPARE(w->tool()->settings().drums[4].volume, 127);
    wheel(ed, -500);
    QCOMPARE(w->tool()->settings().drums[4].volume, 0);
    QCOMPARE(w->tool()->settings().drums[0].volume, 127);                  // the other voices and ...
    QCOMPARE(w->tool()->settings().volume, 127);                           // ... the tool volume are not touched
    QCOMPARE(ed->volumeSlider()->value(), 0);
}

void TestDrumUi::smallWheelDeltasInTheEditor()
{
    auto* w = newDrums();
    w->show();
    auto small = [&](QWidget* t, int d) {
        QWheelEvent e(QPointF(5, 5), t->mapToGlobal(QPointF(5, 5)), QPoint(), QPoint(0, d), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(t, &e);
    };
    for (int i = 0; i < 8; ++i)
        small(w->drumEditor()->stepChooser()->button(0), -15);
    QCOMPARE(w->tool()->settings().drums[0].volume, 126);
    QCOMPARE(w->tool()->settings().volume, 127);
}

void TestDrumUi::wheelElsewhereIsTheToolVolume()
{
    auto* w = newDrums();
    w->show();
    wheel(w->nameEdit(), -5);                        // the header
    QCOMPARE(w->tool()->settings().volume, 122);
    QCOMPARE(w->tool()->settings().drums[0].volume, 127);
    wheel(w, -2);                                    // the frame itself
    QCOMPARE(w->tool()->settings().volume, 120);
    wheel(w->drumEditor()->swing()->slider(), -1);   // the swing slider is dragged, its wheel is the tool volume
    QCOMPARE(w->tool()->settings().volume, 119);
    QCOMPARE(w->tool()->settings().swing, 0);
}

void TestDrumUi::swingSliderReachesTheSettings()
{
    auto* w = newDrums();
    w->drumEditor()->swing()->slider()->setValue(80);
    QCOMPARE(w->tool()->settings().swing, 80);
    // the swing belongs to the tool: selecting another voice keeps it
    click(w->drumEditor()->selector()->button(2));
    QCOMPARE(w->drumEditor()->swing()->value(), 80);
}

void TestDrumUi::chokeSpinnerValuesAndKeys()
{
    auto* w = newDrums();
    w->show();
    auto* c = w->drumEditor()->chokeSpin();
    QCOMPARE(c->value(), 0);
    QCOMPARE(c->text(), QString("off"));
    QList<int> seen;
    for (int i = 0; i < 7; ++i) {
        c->stepUp();
        seen << c->value();
    }
    QCOMPARE(seen, QList<int>({ 1, 2, 4, 8, 16, 16, 16 }));
    QCOMPARE(c->text(), QString("16 /4 beat"));
    QCOMPARE(w->tool()->settings().drums[0].chokeSteps, 16);
    seen.clear();
    for (int i = 0; i < 7; ++i) {
        c->stepDown();
        seen << c->value();
    }
    QCOMPARE(seen, QList<int>({ 8, 4, 2, 1, 0, 0, 0 }));
    QCOMPARE(w->tool()->settings().drums[0].chokeSteps, 0);
    c->setFocus();
    QTest::keyClick(c, Qt::Key_Up);                  // the keyboard walks the same list
    QTest::keyClick(c, Qt::Key_Up);
    QTest::keyClick(c, Qt::Key_Up);
    QCOMPARE(c->value(), 4);
    QTest::keyClick(c, Qt::Key_Down);
    QCOMPARE(c->value(), 2);
    QVERIFY(c->toolTip().contains("100 ms"));
}

void TestDrumUi::chokeSpinnerSnapsTypedValues()
{
    auto* w = newDrums();
    w->show();
    auto* c = w->drumEditor()->chokeSpin();
    QLineEdit* edit = c->findChild<QLineEdit*>();
    QVERIFY(edit);
    for (const auto& t : { std::pair<QString, int>{ "3", 2 }, { "5", 4 }, { "7", 8 }, { "100", 16 }, { "0", 0 }, { "1", 1 } }) {
        edit->selectAll();
        QTest::keyClicks(edit, t.first);
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(c->value(), t.second);
        QCOMPARE(w->tool()->settings().drums[0].chokeSteps, t.second);
    }
}

void TestDrumUi::chokeIsPerVoice()
{
    auto* w = newDrums();
    auto* ed = w->drumEditor();
    ed->chokeSpin()->setValue(4);                    // BD
    click(ed->selector()->button(5));                // OH
    QCOMPARE(ed->chokeSpin()->value(), 0);           // its own value
    ed->chokeSpin()->setValue(1);
    click(ed->selector()->button(0));
    QCOMPARE(ed->chokeSpin()->value(), 4);
    const ToolSettings s = w->tool()->settings();
    QCOMPARE(s.drums[0].chokeSteps, 4);
    QCOMPARE(s.drums[5].chokeSteps, 1);
    QCOMPARE(s.drums[1].chokeSteps, 0);
}

void TestDrumUi::onOffByButtonAndFunctionKeyHook()
{
    auto* w = newDrums();
    QCOMPARE(w->keyLabel(), QString("F1"));
    QVERIFY(!w->tool()->settings().enabled);
    w->onButton()->click();
    QVERIFY(w->tool()->settings().enabled);
    QVERIFY(panel_->toggleToolAt(0));              // what F1 does
    QVERIFY(!w->tool()->settings().enabled);
}

void TestDrumUi::oneBarButtonIsThere()
{
    auto* w = newDrums();
    QVERIFY(w->oneBarButton());
    QVERIFY(w->tool()->supportsOneBar());
    w->tool()->fireOneBar(1.0);
    w->refresh(1.5);                                 // armed: blinking (green / grey)
    w->refresh(4.5);                                 // the bar plays
    QCOMPARE(image(w->oneBarButton()->icon()), image(djtoolzIcon(DjtoolzIcon::Red)));
    w->refresh(9.0);
    QCOMPARE(image(w->oneBarButton()->icon()), image(djtoolzIcon(DjtoolzIcon::Green)));
}

void TestDrumUi::drumMachineTakesAWholeRow()
{
    for (int cols : { 2, 4 }) {
        panel_->setColumns(cols);
        while (!panel_->toolWidgets().isEmpty())
            panel_->removeTool(panel_->toolWidgets().first()->tool()->id());
        panel_->addTool(DjtoolzTool::Bombomatic);           // 0
        panel_->addTool(DjtoolzTool::DrumMachine);          // 1: wants a row of its own
        panel_->addTool(DjtoolzTool::Bombomatic);           // 2
        panel_->addTool(DjtoolzTool::StabPlayer);           // 3
        panel_->addTool(DjtoolzTool::DrumMachine);          // 4
        panel_->addTool(DjtoolzTool::Bombomatic);           // 5
        QCOMPARE(panel_->gridCell(0), QRect(0, 0, 1, 1));
        QCOMPARE(panel_->gridCell(1), QRect(0, 1, cols, 1));   // the whole row, below the first tool
        QCOMPARE(panel_->gridCell(2), QRect(0, 2, 1, 1));       // the tools after it continue below
        QCOMPARE(panel_->gridCell(3), QRect(1, 2, 1, 1));
        if (cols == 2) {
            QCOMPARE(panel_->gridCell(4), QRect(0, 3, 2, 1));
            QCOMPARE(panel_->gridCell(5), QRect(0, 4, 1, 1));
        } else {
            QCOMPARE(panel_->gridCell(4), QRect(0, 3, 4, 1));   // (cell 2 and 3 left the row unfinished: it moves on)
            QCOMPARE(panel_->gridCell(5), QRect(0, 4, 1, 1));
        }
        // the layout agrees with gridCell
        auto* grid = static_cast<QGridLayout*>(panel_->toolWidgets()[1]->parentWidget()->layout());
        int r, c, rs, cs;
        grid->getItemPosition(grid->indexOf(panel_->toolWidgets()[1]), &r, &c, &rs, &cs);
        QCOMPARE(QRect(c, r, cs, rs), QRect(0, 1, cols, 1));
        grid->getItemPosition(grid->indexOf(panel_->toolWidgets()[2]), &r, &c, &rs, &cs);
        QCOMPARE(QRect(c, r, cs, rs), QRect(0, 2, 1, 1));
    }
}

void TestDrumUi::dembomatikHasASwingSlider()
{
    auto* d = panel_->addTool(DjtoolzTool::Dembomatik);
    QVERIFY(d->swingSlider());
    auto* b = panel_->addTool(DjtoolzTool::Bombomatic);
    QVERIFY(!b->swingSlider());
    auto* s = panel_->addTool(DjtoolzTool::StabPlayer);
    QVERIFY(!s->swingSlider());
    d->swingSlider()->slider()->setValue(55);
    QCOMPARE(d->tool()->settings().swing, 55);
    QVERIFY(panel_->savePresetAs("dem"));
    panel_->removeTool(d->tool()->id());
    QVERIFY(panel_->loadPresetByName("dem"));
    DjtoolzToolWidget* back = nullptr;
    for (auto* w : panel_->toolWidgets())
        if (w->tool()->type() == DjtoolzTool::Dembomatik)
            back = w;
    QVERIFY(back);
    QCOMPARE(back->swingSlider()->value(), 55);
    // the wheel over it is the tool volume, not the swing
    wheel(back->swingSlider()->slider(), -2);
    QCOMPARE(back->tool()->settings().swing, 55);
    QCOMPARE(back->tool()->settings().volume, 125);
}

void TestDrumUi::voiceMuteDimsAndAltClickToggles()
{
    auto* w = newDrums();
    auto* sel = w->drumEditor()->selector();
    auto* ed = w->drumEditor();
    QVERIFY(!sel->muted(2));
    const QString tip = sel->button(2)->toolTip();
    QVERIFY(!tip.contains("muted"));
    QSignalSpy toggled(sel, &DrumVoiceSelector::muteToggled);
    QSignalSpy selected(sel, &DrumVoiceSelector::selectedChanged);
    click(sel->button(2), Qt::AltModifier);
    QVERIFY(sel->muted(2));
    QVERIFY(sel->button(2)->graphicsEffect());                  // dimmed
    QVERIFY(sel->button(2)->toolTip().endsWith("muted"));
    QCOMPARE(toggled.size(), 1);
    QCOMPARE(toggled[0][0].toInt(), 2);
    QCOMPARE(toggled[0][1].toBool(), true);
    QCOMPARE(selected.size(), 0);                                // the selection did not move
    QCOMPARE(sel->selected(), 0);
    QVERIFY(w->tool()->settings().drums[2].muted);
    QVERIFY(!w->tool()->settings().drums[1].muted);
    click(sel->button(2), Qt::AltModifier);                      // again: unmuted
    QVERIFY(!sel->muted(2));
    QVERIFY(!sel->button(2)->graphicsEffect());
    QCOMPARE(sel->button(2)->toolTip(), tip);
    QVERIFY(!w->tool()->settings().drums[2].muted);
    QTest::mouseClick(sel->button(3), Qt::MiddleButton, Qt::NoModifier, QPoint(5, 5));   // middle click
    QVERIFY(sel->muted(3));
    QCOMPARE(sel->selected(), 0);
    click(sel->button(3));                                       // a plain click selects, muted or not
    QCOMPARE(sel->selected(), 3);
    QVERIFY(sel->muted(3));
    ed->toggleVoiceMute(3);
    QVERIFY(!sel->muted(3));
    ed->setVoiceMuted(5, true);                                  // any voice, also a hidden one
    QVERIFY(w->tool()->settings().drums[5].muted);
    QVERIFY(sel->muted(5));
    // a preset keeps the mutes
    QVERIFY(panel_->savePresetAs("muted"));
    panel_->removeTool(w->tool()->id());
    QVERIFY(panel_->loadPresetByName("muted"));
    auto* back = panel_->toolWidgets()[0];
    QVERIFY(back->tool()->settings().drums[5].muted);
    QVERIFY(back->drumEditor()->selector()->muted(5));
    QVERIFY(!back->drumEditor()->selector()->muted(2));
}

void TestDrumUi::presetRoundTripKeepsEverything()
{
    auto* w = newDrums();
    auto m = w->tool();
    ToolSettings s = m->settings();
    s.swing = 99;
    s.volume = 77;
    for (int v = 0; v < kDrumVoices; ++v) {
        s.drums[v].samplePath = QString("/k/%1.wav").arg(v);
        s.drums[v].volume = 10 + v * 10;
        s.drums[v].offsetMs = v - 3;
        s.drums[v].chokeSteps = kDrumChokeValues[v];
        for (int i = 0; i < kDrumSteps; ++i)
            s.drums[v].steps[i] = quint8((i * (v + 1)) % 3);
    }
    m->setSettings(s);
    const ToolSettings before = m->settings();
    QVERIFY(panel_->savePresetAs("kit one"));
    panel_->removeTool(m->id());
    QCOMPARE(panel_->toolWidgets().size(), 0);
    QVERIFY(panel_->loadPresetByName("kit one"));
    QCOMPARE(panel_->toolWidgets().size(), 1);
    auto* back = panel_->toolWidgets()[0];
    QCOMPARE(back->tool()->type(), DjtoolzTool::DrumMachine);
    const ToolSettings after = back->tool()->settings();
    QCOMPARE(after.swing, 99);
    QCOMPARE(after.volume, 77);
    for (int v = 0; v < kDrumVoices; ++v)
        QVERIFY2(after.drums[v] == before.drums[v], kDrumVoiceNames[v]);
    // and the editor shows it
    QCOMPARE(back->drumEditor()->swing()->value(), 99);
    click(back->drumEditor()->selector()->button(3));
    QCOMPARE(back->drumEditor()->chokeSpin()->value(), kDrumChokeValues[3]);
    QCOMPARE(back->drumEditor()->volumeSlider()->value(), 40);
    QCOMPARE(back->drumEditor()->sampleChooser()->sample(), QString("/k/3.wav"));
}

QTEST_MAIN(TestDrumUi)
#include "test_drum_ui.moc"
