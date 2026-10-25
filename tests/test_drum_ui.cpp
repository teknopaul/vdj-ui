#include <QtTest/QtTest>
#include <QApplication>
#include <QCheckBox>
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
#include "ui/sampler_module_widget.h"
#include "ui/sampler_panel.h"
#include "ui/sample_tree_model.h"
#include "ui/drum_voice_selector.h"
#include "ui/sampler_icons.h"
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
        panel_ = new SamplerPanel(&clock_, dir_->path());
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
    void wheelElsewhereIsTheModuleVolume();
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

private:
    QTemporaryDir* dir_ = nullptr;
    AlsaClock clock_;
    SamplerPanel* panel_ = nullptr;
    QString a_, b_, c_;

    static QString touch(const QString& path)
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("x");
        return path;
    }
    SamplerModuleWidget* newDrums()
    {
        return panel_->addModule(SamplerModule::DrumMachine);
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
    QCOMPARE(image(c.button(0)->icon()), image(samplerIcon(SamplerIcon::Yellow)));
    QCOMPARE(image(c.button(1)->icon()), image(samplerIcon(SamplerIcon::Red)));
    QCOMPARE(image(c.button(2)->icon()), image(samplerIcon(SamplerIcon::Pink)));
    c.show();
    click(c.button(0));
    QCOMPARE(image(c.button(0)->icon()), image(samplerIcon(SamplerIcon::Red)));
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
    QCOMPARE(image(c.button(0)->icon()), image(samplerIcon(SamplerIcon::Grey)));
    QCOMPARE(image(c.button(5)->icon()), image(samplerIcon(SamplerIcon::Grey)));
    QSignalSpy spy(&c, &StepChooser::changed);
    click(c.button(1));                              // a disabled chooser does not react
    QCOMPARE(spy.count(), 0);
    c.setEnabled(true);
    QCOMPARE(image(c.button(0)->icon()), image(samplerIcon(SamplerIcon::Red)));
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
    QVERIFY(!w->offsetSpin());                       // no module level offset
    QVERIFY(!w->reverseSyncBox());
    QVERIFY(!w->swingSlider());                      // the swing is in the editor
    QVERIFY(w->drumEditor()->swing());
    QVERIFY(w->oneBarButton());
    QVERIFY(w->wantsFullRow());
    QCOMPARE(w->module()->typeName(), QString("DR-6S"));
    QVERIFY(w->volumeSlider());                      // the module (master) volume
    QVERIFY(w->typeLabel()->font().bold());          // the type is bold, the name the user types is not
    QVERIFY(!w->nameEdit()->font().bold());
    QCOMPARE(w->typeLabel()->text(), QString("DR-6S"));
}

void TestDrumUi::selectingAVoiceShowsItsSettings()
{
    auto* w = newDrums();
    auto m = w->module();
    ModuleSettings s = m->settings();
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
    // selecting does not change anything in the module
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
    const ModuleSettings s = w->module()->settings();
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
    QCOMPARE(w->module()->settings().drums[2].samplePath, b_);
    ed->sampleChooser()->downButton()->click();
    QCOMPARE(w->module()->settings().drums[2].samplePath, c_);
    ed->sampleChooser()->upButton()->click();
    QCOMPARE(w->module()->settings().drums[2].samplePath, b_);
    QVERIFY(w->module()->settings().drums[0].samplePath.isEmpty());          // the other voices are untouched
    QVERIFY(w->module()->settings().drums[5].samplePath.isEmpty());
    // the tree's right click menu does not offer a DR-6S (it has no single sample)
    panel_->setSampleFolder(dir_->path());
    QTest::qWait(300);
    QMenu* menu = panel_->sendToMenu(panel_->treeModel()->indexForPath(a_), panel_);
    QVERIFY(menu);
    for (QAction* act : menu->actions())
        QVERIFY(!act->text().contains("DR-6S"));
    delete menu;
}

void TestDrumUi::wheelOverTheEditorIsTheVoiceVolume()
{
    auto* w = newDrums();
    w->show();
    auto* ed = w->drumEditor();
    click(ed->selector()->button(4));                // HH
    QCOMPARE(w->module()->settings().drums[4].volume, 127);
    wheel(ed->stepChooser()->button(0), -3);         // over a step button
    QCOMPARE(w->module()->settings().drums[4].volume, 124);
    wheel(ed->offsetSpin(), -1);                     // over the offset spin box: volume, not the offset
    QCOMPARE(w->module()->settings().drums[4].volume, 123);
    QCOMPARE(w->module()->settings().drums[4].offsetMs, 0.0);
    wheel(ed->sampleChooser(), -1, Qt::ShiftModifier);
    QCOMPARE(w->module()->settings().drums[4].volume, 113);
    wheel(ed, +60);                                  // stops at 127
    QCOMPARE(w->module()->settings().drums[4].volume, 127);
    wheel(ed, -500);
    QCOMPARE(w->module()->settings().drums[4].volume, 0);
    QCOMPARE(w->module()->settings().drums[0].volume, 127);                  // the other voices and ...
    QCOMPARE(w->module()->settings().volume, 127);                           // ... the module volume are not touched
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
    QCOMPARE(w->module()->settings().drums[0].volume, 126);
    QCOMPARE(w->module()->settings().volume, 127);
}

void TestDrumUi::wheelElsewhereIsTheModuleVolume()
{
    auto* w = newDrums();
    w->show();
    wheel(w->nameEdit(), -5);                        // the header
    QCOMPARE(w->module()->settings().volume, 122);
    QCOMPARE(w->module()->settings().drums[0].volume, 127);
    wheel(w, -2);                                    // the frame itself
    QCOMPARE(w->module()->settings().volume, 120);
    wheel(w->drumEditor()->swing()->slider(), -1);   // the swing slider is dragged, its wheel is the module volume
    QCOMPARE(w->module()->settings().volume, 119);
    QCOMPARE(w->module()->settings().swing, 0);
}

void TestDrumUi::swingSliderReachesTheSettings()
{
    auto* w = newDrums();
    w->drumEditor()->swing()->slider()->setValue(80);
    QCOMPARE(w->module()->settings().swing, 80);
    // the swing belongs to the module: selecting another voice keeps it
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
    QCOMPARE(w->module()->settings().drums[0].chokeSteps, 16);
    seen.clear();
    for (int i = 0; i < 7; ++i) {
        c->stepDown();
        seen << c->value();
    }
    QCOMPARE(seen, QList<int>({ 8, 4, 2, 1, 0, 0, 0 }));
    QCOMPARE(w->module()->settings().drums[0].chokeSteps, 0);
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
        QCOMPARE(w->module()->settings().drums[0].chokeSteps, t.second);
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
    const ModuleSettings s = w->module()->settings();
    QCOMPARE(s.drums[0].chokeSteps, 4);
    QCOMPARE(s.drums[5].chokeSteps, 1);
    QCOMPARE(s.drums[1].chokeSteps, 0);
}

void TestDrumUi::onOffByButtonAndFunctionKeyHook()
{
    auto* w = newDrums();
    QCOMPARE(w->keyLabel(), QString("F1"));
    QVERIFY(!w->module()->settings().enabled);
    w->onButton()->click();
    QVERIFY(w->module()->settings().enabled);
    QVERIFY(panel_->toggleModuleAt(0));              // what F1 does
    QVERIFY(!w->module()->settings().enabled);
}

void TestDrumUi::oneBarButtonIsThere()
{
    auto* w = newDrums();
    QVERIFY(w->oneBarButton());
    QVERIFY(w->module()->supportsOneBar());
    w->module()->fireOneBar(1.0);
    w->refresh(1.5);                                 // armed: blinking (green / grey)
    w->refresh(4.5);                                 // the bar plays
    QCOMPARE(image(w->oneBarButton()->icon()), image(samplerIcon(SamplerIcon::Red)));
    w->refresh(9.0);
    QCOMPARE(image(w->oneBarButton()->icon()), image(samplerIcon(SamplerIcon::Green)));
}

void TestDrumUi::drumMachineTakesAWholeRow()
{
    for (int cols : { 2, 4 }) {
        panel_->setColumns(cols);
        while (!panel_->moduleWidgets().isEmpty())
            panel_->removeModule(panel_->moduleWidgets().first()->module()->id());
        panel_->addModule(SamplerModule::Bombomatic);           // 0
        panel_->addModule(SamplerModule::DrumMachine);          // 1: wants a row of its own
        panel_->addModule(SamplerModule::Bombomatic);           // 2
        panel_->addModule(SamplerModule::StabPlayer);           // 3
        panel_->addModule(SamplerModule::DrumMachine);          // 4
        panel_->addModule(SamplerModule::Bombomatic);           // 5
        QCOMPARE(panel_->gridCell(0), QRect(0, 0, 1, 1));
        QCOMPARE(panel_->gridCell(1), QRect(0, 1, cols, 1));   // the whole row, below the first module
        QCOMPARE(panel_->gridCell(2), QRect(0, 2, 1, 1));       // the modules after it continue below
        QCOMPARE(panel_->gridCell(3), QRect(1, 2, 1, 1));
        if (cols == 2) {
            QCOMPARE(panel_->gridCell(4), QRect(0, 3, 2, 1));
            QCOMPARE(panel_->gridCell(5), QRect(0, 4, 1, 1));
        } else {
            QCOMPARE(panel_->gridCell(4), QRect(0, 3, 4, 1));   // (cell 2 and 3 left the row unfinished: it moves on)
            QCOMPARE(panel_->gridCell(5), QRect(0, 4, 1, 1));
        }
        // the layout agrees with gridCell
        auto* grid = static_cast<QGridLayout*>(panel_->moduleWidgets()[1]->parentWidget()->layout());
        int r, c, rs, cs;
        grid->getItemPosition(grid->indexOf(panel_->moduleWidgets()[1]), &r, &c, &rs, &cs);
        QCOMPARE(QRect(c, r, cs, rs), QRect(0, 1, cols, 1));
        grid->getItemPosition(grid->indexOf(panel_->moduleWidgets()[2]), &r, &c, &rs, &cs);
        QCOMPARE(QRect(c, r, cs, rs), QRect(0, 2, 1, 1));
    }
}

void TestDrumUi::dembomatikHasASwingSlider()
{
    auto* d = panel_->addModule(SamplerModule::Dembomatik);
    QVERIFY(d->swingSlider());
    auto* b = panel_->addModule(SamplerModule::Bombomatic);
    QVERIFY(!b->swingSlider());
    auto* s = panel_->addModule(SamplerModule::StabPlayer);
    QVERIFY(!s->swingSlider());
    d->swingSlider()->slider()->setValue(55);
    QCOMPARE(d->module()->settings().swing, 55);
    QVERIFY(panel_->savePresetAs("dem"));
    panel_->removeModule(d->module()->id());
    QVERIFY(panel_->loadPresetByName("dem"));
    SamplerModuleWidget* back = nullptr;
    for (auto* w : panel_->moduleWidgets())
        if (w->module()->type() == SamplerModule::Dembomatik)
            back = w;
    QVERIFY(back);
    QCOMPARE(back->swingSlider()->value(), 55);
    // the wheel over it is the module volume, not the swing
    wheel(back->swingSlider()->slider(), -2);
    QCOMPARE(back->module()->settings().swing, 55);
    QCOMPARE(back->module()->settings().volume, 125);
}

void TestDrumUi::presetRoundTripKeepsEverything()
{
    auto* w = newDrums();
    auto m = w->module();
    ModuleSettings s = m->settings();
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
    const ModuleSettings before = m->settings();
    QVERIFY(panel_->savePresetAs("kit one"));
    panel_->removeModule(m->id());
    QCOMPARE(panel_->moduleWidgets().size(), 0);
    QVERIFY(panel_->loadPresetByName("kit one"));
    QCOMPARE(panel_->moduleWidgets().size(), 1);
    auto* back = panel_->moduleWidgets()[0];
    QCOMPARE(back->module()->type(), SamplerModule::DrumMachine);
    const ModuleSettings after = back->module()->settings();
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
