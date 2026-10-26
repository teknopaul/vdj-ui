#include <QtTest/QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTreeView>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include "midi/alsa_clock.h"
#include "ui/beat_chooser.h"
#include "ui/sample_chooser.h"
#include "ui/sample_tree_model.h"
#include "ui/djtoolz_icons.h"
#include "ui/djtoolz_tool_widget.h"
#include "ui/djtoolz_panel.h"

using namespace vdj;

class TestDjtoolzUi : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void everyTypeCanBeAddedAndRemoved();
    void togglesAndFieldsReachTheTool();
    void typeSpecificControlsOnlyWhereTheyBelong();
    void beatChooserMaskAndIcons();
    void upDownStepsThroughTheFolder();
    void missingSampleIsShown();
    void oneBarButtonLightsAndClears();
    void presetSaveLoadRestoresTheUi();
    void currentStateComesBackAtNextStart();
    void masterVolumeIsSavedAndApplied();
    void statusLabelFollowsTheSequencer();
    void browserDialogPreviewsTheHighlightedSample();
    void treeRightClickSendsToTools();
    void toolsAreInAGridTwoOrFourAcross();
    void mouseWheelOverTheToolIsTheVolume();
    void functionKeysSwitchTools();
    void greenButtonSitsRightOfTheOnOffButtonInEveryTool();
    void smallWheelDeltasAddUpToNotches();
    void drumIconsExistAndDiffer();

private:
    QTemporaryDir* dir_ = nullptr;
    AlsaClock clock_;                                // never opened: the sequencer is stopped
    DjtoolzPanel* panel_ = nullptr;
    QString a_, b_, c_;

    static QString touch(const QString& path)
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("x");
        return path;
    }
    static QImage image(const QIcon& i) { return i.pixmap(24, 24).toImage(); }
    DjtoolzPanel* newPanel()
    {
        auto* p = new DjtoolzPanel(&clock_, dir_->path());
        p->setAudioEnabled(false);
        return p;
    }
};

void TestDjtoolzUi::init()
{
    dir_ = new QTemporaryDir;
    a_ = touch(dir_->path() + "/a.wav");
    b_ = touch(dir_->path() + "/b.wav");
    c_ = touch(dir_->path() + "/c.wav");
    panel_ = newPanel();
}

void TestDjtoolzUi::cleanup()
{
    delete panel_;
    panel_ = nullptr;
    delete dir_;
    dir_ = nullptr;
}

void TestDjtoolzUi::everyTypeCanBeAddedAndRemoved()
{
    QCOMPARE(panel_->toolWidgets().size(), 0);
    for (auto t : { DjtoolzTool::Bombomatic, DjtoolzTool::StabPlayer, DjtoolzTool::AcidLoop, DjtoolzTool::OneShot, DjtoolzTool::MidiMatic, DjtoolzTool::Dembomatik, DjtoolzTool::DrumMachine })
        QVERIFY(panel_->addTool(t));
    QCOMPARE(panel_->toolWidgets().size(), 7);
    QCOMPARE(int(panel_->scheduler()->tools().size()), 7);
    const int id = panel_->toolWidgets()[1]->tool()->id();
    QTest::mouseClick(panel_->toolWidgets()[1]->removeButton(), Qt::LeftButton);
    QCOMPARE(panel_->toolWidgets().size(), 6);
    QVERIFY(!panel_->scheduler()->tool(id));
    // the "Add tool" menu has the seven sound types and the FX tool
    QCOMPARE(panel_->addButton()->menu()->actions().size(), 8);
}

void TestDjtoolzUi::togglesAndFieldsReachTheTool()
{
    auto* w = panel_->addTool(DjtoolzTool::StabPlayer);
    auto m = w->tool();
    QVERIFY(!m->settings().enabled);
    w->onButton()->click();
    QVERIFY(m->settings().enabled);
    w->onButton()->click();
    QVERIFY(!m->settings().enabled);
    w->nameEdit()->clear();
    QTest::keyClicks(w->nameEdit(), "kick stabs");
    QCOMPARE(m->settings().name, QString("kick stabs"));
    w->offsetSpin()->setValue(-12);
    QCOMPARE(m->settings().offsetMs, -12.0);
    w->reverseSyncBox()->setChecked(true);
    QVERIFY(m->settings().reverseSync);
    QCOMPARE(m->settings().beatMask, quint8(0b1111));   // all beats on to start with
    w->beatChooser()->button(1)->click();            // toggles beat 2 off
    QCOMPARE(m->settings().beatMask, quint8(0b1101));

    auto* os = panel_->addTool(DjtoolzTool::OneShot);
    os->quantizeButton()->setChecked(false);
    os->stretchBox()->setChecked(false);
    os->bpmSpin()->setValue(97.5);
    QVERIFY(!os->tool()->settings().quantize);
    QVERIFY(!os->tool()->settings().stretch);
    QCOMPARE(os->tool()->settings().manualBpm, 97.5);

    auto* mm = panel_->addTool(DjtoolzTool::MidiMatic);
    mm->midiChannelSpin()->setValue(10);
    mm->midiNoteSpin()->setValue(42);
    QCOMPARE(mm->tool()->settings().midiChannel, 10);
    QCOMPARE(mm->tool()->settings().midiNote, 42);
    QVERIFY(mm->midiDeviceCombo()->count() >= 1);    // at least "(connected ports)"
}

void TestDjtoolzUi::typeSpecificControlsOnlyWhereTheyBelong()
{
    auto* bomb = panel_->addTool(DjtoolzTool::Bombomatic);
    QVERIFY(bomb->sampleChooser());
    QVERIFY(!bomb->beatChooser() && !bomb->reverseSyncBox() && bomb->oneBarButton() && !bomb->fireButton() && !bomb->midiDeviceCombo());
    auto* stab = panel_->addTool(DjtoolzTool::StabPlayer);
    QVERIFY(stab->beatChooser() && stab->reverseSyncBox() && stab->oneBarButton());
    auto* acid = panel_->addTool(DjtoolzTool::AcidLoop);
    QVERIFY(acid->oneBarButton() && acid->bpmSpin());
    QVERIFY(!acid->reverseSyncBox() && !acid->beatChooser());     // no reverse sync for loops
    auto* shot = panel_->addTool(DjtoolzTool::OneShot);
    QVERIFY(shot->fireButton() && shot->quantizeButton() && shot->stretchBox() && !shot->oneBarButton());
    auto* midi = panel_->addTool(DjtoolzTool::MidiMatic);
    QVERIFY(!midi->sampleChooser() && midi->beatChooser() && midi->midiDeviceCombo() && midi->oneBarButton());
}

void TestDjtoolzUi::beatChooserMaskAndIcons()
{
    BeatChooser bc;
    bc.setMask(0b0101);
    QCOMPARE(bc.mask(), quint8(0b0101));
    QSignalSpy spy(&bc, &BeatChooser::changed);
    bc.button(1)->click();
    QCOMPARE(bc.mask(), quint8(0b0111));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().value<quint8>(), quint8(0b0111));
    bc.setMask(0b1000);                              // setMask does not emit
    QCOMPARE(spy.count(), 1);
    QCOMPARE(image(bc.button(3)->icon()), image(djtoolzIcon(DjtoolzIcon::Red)));
    QCOMPARE(image(bc.button(0)->icon()), image(djtoolzIcon(DjtoolzIcon::Yellow)));
    bc.setEnabled(false);
    QCOMPARE(image(bc.button(3)->icon()), image(djtoolzIcon(DjtoolzIcon::Grey)));
    QCOMPARE(image(bc.button(0)->icon()), image(djtoolzIcon(DjtoolzIcon::Grey)));
    QVERIFY(image(djtoolzIcon(DjtoolzIcon::Red)) != image(djtoolzIcon(DjtoolzIcon::Yellow)));
    QVERIFY(image(djtoolzIcon(DjtoolzIcon::Green)) != image(djtoolzIcon(DjtoolzIcon::Red)));
    bc.setEnabled(true);
    QCOMPARE(image(bc.button(3)->icon()), image(djtoolzIcon(DjtoolzIcon::Red)));
}

void TestDjtoolzUi::upDownStepsThroughTheFolder()
{
    auto* w = panel_->addTool(DjtoolzTool::Bombomatic);
    auto* sc = w->sampleChooser();
    QSignalSpy spy(sc, &SampleChooser::sampleChanged);
    QVERIFY(!sc->downButton()->isEnabled());         // nothing to step from yet
    sc->setSample(a_);
    QVERIFY(sc->downButton()->isEnabled());
    QCOMPARE(sc->nameLabel()->text(), QString("a.wav"));
    sc->downButton()->click();
    QCOMPARE(sc->sample(), b_);
    QCOMPARE(sc->nameLabel()->text(), QString("b.wav"));
    QCOMPARE(w->tool()->settings().samplePath, b_);   // the tool has it at once: the next hit plays b.wav
    sc->downButton()->click();
    sc->downButton()->click();                       // clamps at the last file
    QCOMPARE(sc->sample(), c_);
    QCOMPARE(spy.count(), 2);
    sc->upButton()->click();
    QCOMPARE(w->tool()->settings().samplePath, b_);
    QCOMPARE(spy.count(), 3);
}

void TestDjtoolzUi::missingSampleIsShown()
{
    SampleChooser sc;
    sc.setSample(dir_->path() + "/gone.wav");
    QVERIFY(sc.nameLabel()->text().contains("gone.wav"));
    QVERIFY(sc.nameLabel()->text().contains("missing"));
    sc.setSample(QString());
    QCOMPARE(sc.nameLabel()->text(), QString("(no sample)"));
}

void TestDjtoolzUi::oneBarButtonLightsAndClears()
{
    auto* w = panel_->addTool(DjtoolzTool::StabPlayer);
    QVERIFY(w->oneBarButton()->text().isEmpty());      // the green button is just the icon
    QVERIFY(w->oneBarButton()->toolTip().contains("1 bar"));
    auto* shot = panel_->addTool(DjtoolzTool::OneShot);
    QVERIFY(shot->fireButton()->text().isEmpty());
    QVERIFY(shot->fireButton()->toolTip().contains("Fire"));
    w->refresh(0.5);
    QCOMPARE(image(w->oneBarButton()->icon()), image(djtoolzIcon(DjtoolzIcon::Green)));
    w->tool()->fireOneBar(1.0);                    // armed for the bar at beat 4
    w->refresh(1.5);
    const QImage armed = image(w->oneBarButton()->icon());
    QVERIFY(armed == image(djtoolzIcon(DjtoolzIcon::Green)) || armed == image(djtoolzIcon(DjtoolzIcon::Grey)));   // blinks
    w->refresh(4.5);                                 // the bar is playing
    QCOMPARE(image(w->oneBarButton()->icon()), image(djtoolzIcon(DjtoolzIcon::Red)));
    w->refresh(8.5);                                 // and over
    QCOMPARE(image(w->oneBarButton()->icon()), image(djtoolzIcon(DjtoolzIcon::Green)));
}

void TestDjtoolzUi::presetSaveLoadRestoresTheUi()
{
    panel_->setSampleFolder(dir_->path());
    auto* stab = panel_->addTool(DjtoolzTool::StabPlayer);
    stab->nameEdit()->setText("my stab");
    QTest::keyClick(stab->nameEdit(), Qt::Key_X);
    stab->sampleChooser()->setSample(a_);
    stab->sampleChooser()->downButton()->click();    // a.wav -> b.wav, through the real signal
    stab->offsetSpin()->setValue(7);
    stab->reverseSyncBox()->setChecked(true);
    stab->beatChooser()->setMask(0b0110);
    stab->beatChooser()->button(0)->click();         // mask 0b0111 through the real signal
    QCOMPARE(stab->tool()->settings().beatMask, quint8(0b0111));
    auto* acid = panel_->addTool(DjtoolzTool::AcidLoop);
    acid->sampleChooser()->setSample(b_);
    acid->sampleChooser()->downButton()->click();    // b.wav -> c.wav
    QVERIFY(panel_->savePresetAs("Friday: set"));
    QCOMPARE(panel_->presetCombo()->count(), 2);     // the placeholder + the preset
    QCOMPARE(panel_->presetCombo()->currentText(), QString("Friday_ set"));

    // change everything, then load the preset again from the combo box
    panel_->removeTool(stab->tool()->id());
    panel_->removeTool(acid->tool()->id());
    QCOMPARE(panel_->toolWidgets().size(), 0);
    panel_->addTool(DjtoolzTool::MidiMatic);
    QVERIFY(panel_->loadPresetByName("Friday_ set"));
    QCOMPARE(panel_->toolWidgets().size(), 2);
    auto* s = panel_->toolWidgets()[0];
    QCOMPARE(s->tool()->type(), DjtoolzTool::StabPlayer);
    QCOMPARE(s->nameEdit()->text(), QString("my stabx"));
    QCOMPARE(s->sampleChooser()->sample(), b_);
    QCOMPARE(s->offsetSpin()->value(), 7);
    QVERIFY(s->reverseSyncBox()->isChecked());
    QCOMPARE(s->beatChooser()->mask(), quint8(0b0111));
    QCOMPARE(panel_->toolWidgets()[1]->sampleChooser()->sample(), c_);
    QCOMPARE(panel_->sampleFolder(), dir_->path());
    QCOMPARE(int(panel_->scheduler()->tools().size()), 2);     // the scheduler follows the UI

    // selecting does not load; Load does; Save overwrites the selected preset; Clear wipes the tools
    panel_->clearButton()->click();
    QCOMPARE(panel_->toolWidgets().size(), 0);
    QVERIFY(panel_->presets()->exists("Friday_ set"));
    panel_->presetCombo()->setCurrentIndex(0);
    QVERIFY(!panel_->loadPresetButton()->isEnabled());
    QVERIFY(!panel_->savePresetButton()->isEnabled());
    panel_->presetCombo()->setCurrentIndex(1);
    QCOMPARE(panel_->toolWidgets().size(), 0);
    panel_->loadPresetButton()->click();
    QCOMPARE(panel_->toolWidgets().size(), 2);
    panel_->addTool(DjtoolzTool::MidiMatic);
    panel_->savePresetButton()->click();
    panel_->clearButton()->click();
    panel_->loadPresetButton()->click();
    QCOMPARE(panel_->toolWidgets().size(), 3);

    // delete the preset
    panel_->presetCombo()->setCurrentIndex(1);
    QVERIFY(panel_->presets()->remove("Friday_ set"));
    panel_->refreshPresetList();
    QCOMPARE(panel_->presetCombo()->count(), 1);
}

void TestDjtoolzUi::currentStateComesBackAtNextStart()
{
    auto* w = panel_->addTool(DjtoolzTool::Bombomatic);
    w->sampleChooser()->setSample(a_);
    w->nameEdit()->setText("boom");   // (setText does not edit the tool: the key click below does)
    QTest::keyClick(w->nameEdit(), Qt::Key_Exclam);
    panel_->masterSlider()->setValue(60);
    panel_->shutdown();                              // writes the working state
    delete panel_;
    panel_ = newPanel();                             // "next start", same folder
    QCOMPARE(panel_->toolWidgets().size(), 1);
    QCOMPARE(panel_->toolWidgets()[0]->nameEdit()->text(), QString("boom!"));
    QCOMPARE(panel_->masterSlider()->value(), 60);
}

void TestDjtoolzUi::masterVolumeIsSavedAndApplied()
{
    panel_->masterSlider()->setValue(80);
    QCOMPARE(panel_->masterGain(), 0.8);
    QCOMPARE(panel_->state().masterGain, 0.8);
    DjtoolzState st = panel_->state();
    st.masterGain = 1.25;
    panel_->applyState(st);
    QCOMPARE(panel_->masterSlider()->value(), 125);
}

void TestDjtoolzUi::statusLabelFollowsTheSequencer()
{
    QTest::qWait(150);                               // the poll timer runs every 100 ms
    QVERIFY(panel_->statusLabel()->text().contains("stopped"));
}

void TestDjtoolzUi::browserDialogPreviewsTheHighlightedSample()
{
    QStringList previewed;
    SampleBrowserDialog dlg(dir_->path(), QString(), [&](const QString& p) { previewed << p; });
    QVERIFY(!dlg.previewButton()->isEnabled());      // nothing highlighted
    QVERIFY(dlg.selected().isEmpty());
    QTest::qWait(300);                               // the file system model reads the folder in the background
    const QModelIndex root = dlg.tree()->rootIndex();
    QAbstractItemModel* model = dlg.tree()->model();
    QVERIFY(model->rowCount(root) >= 3);
    dlg.tree()->setCurrentIndex(model->index(1, 0, root));   // b.wav
    QCOMPARE(dlg.selected(), b_);
    QVERIFY(dlg.previewButton()->isEnabled());
    dlg.previewButton()->click();
    QCOMPARE(previewed, QStringList({ b_ }));
}

void TestDjtoolzUi::treeRightClickSendsToTools()
{
    panel_->setSampleFolder(dir_->path());
    QTest::qWait(300);                               // the model reads the folder in the background
    const QModelIndex bIndex = panel_->treeModel()->indexForPath(b_);
    QVERIFY(bIndex.isValid());

    // no tool yet: only Preview and a disabled hint
    QMenu* empty = panel_->sendToMenu(bIndex, panel_);
    QVERIFY(empty);
    QStringList texts;
    for (QAction* a : empty->actions())
        if (!a->isSeparator())
            texts << a->text();
    QVERIFY(texts.contains("Preview"));
    QVERIFY(texts.last().contains("add a tool first"));
    delete empty;

    auto* bomb = panel_->addTool(DjtoolzTool::Bombomatic);
    auto* stab = panel_->addTool(DjtoolzTool::StabPlayer);
    stab->nameEdit()->setText("");
    QTest::keyClicks(stab->nameEdit(), "snares");
    panel_->addTool(DjtoolzTool::MidiMatic);         // no sample: not offered
    QMenu* menu = panel_->sendToMenu(bIndex, panel_);
    QStringList sends;
    QAction* toStab = nullptr;
    for (QAction* a : menu->actions()) {
        if (a->text().startsWith("Send to")) {
            sends << a->text();
            if (a->text() == "Send to snares")
                toStab = a;
        }
    }
    QCOMPARE(sends, QStringList({ "Send to Bombomatic", "Send to snares" }));
    QVERIFY(toStab);
    toStab->trigger();
    QCOMPARE(stab->tool()->settings().samplePath, b_);
    QCOMPARE(stab->sampleChooser()->sample(), b_);
    QCOMPARE(stab->sampleChooser()->nameLabel()->text(), QString("b.wav"));
    QVERIFY(bomb->tool()->settings().samplePath.isEmpty());   // only the chosen one changed
    delete menu;

    // a folder or nothing under the pointer: no menu
    QVERIFY(!panel_->sendToMenu(QModelIndex(), panel_));
    QVERIFY(!panel_->sendToMenu(panel_->treeModel()->indexForPath(dir_->path()), panel_));
}

void TestDjtoolzUi::toolsAreInAGridTwoOrFourAcross()
{
    QCOMPARE(panel_->columns(), 2);                  // the default
    for (int i = 0; i < 5; ++i)
        panel_->addTool(DjtoolzTool::Bombomatic);
    auto cell = [&](int i) {
        auto* g = panel_->toolWidgets()[i]->parentWidget()->layout();
        const int idx = g->indexOf(panel_->toolWidgets()[i]);
        int r, c, rs, cs;
        static_cast<QGridLayout*>(g)->getItemPosition(idx, &r, &c, &rs, &cs);
        return QPoint(c, r);
    };
    QCOMPARE(cell(0), QPoint(0, 0));
    QCOMPARE(cell(1), QPoint(1, 0));
    QCOMPARE(cell(2), QPoint(0, 1));
    QCOMPARE(cell(4), QPoint(0, 2));
    panel_->columnsCombo()->setCurrentIndex(1);
    emit panel_->columnsCombo()->activated(1);       // the user picks "4 across"
    QCOMPARE(panel_->columns(), 4);
    QCOMPARE(cell(3), QPoint(3, 0));
    QCOMPARE(cell(4), QPoint(0, 1));
    // removing one closes the gap
    panel_->removeTool(panel_->toolWidgets()[0]->tool()->id());
    QCOMPARE(cell(3), QPoint(3, 0));
    QCOMPARE(panel_->toolWidgets().size(), 4);
    // the choice is part of the saved state
    QCOMPARE(panel_->state().columns, 4);
    DjtoolzState st = panel_->state();
    st.columns = 2;
    panel_->applyState(st);
    QCOMPARE(panel_->columns(), 2);
    QCOMPARE(cell(1), QPoint(1, 0));
    QCOMPARE(cell(2), QPoint(0, 1));
}

void TestDjtoolzUi::mouseWheelOverTheToolIsTheVolume()
{
    auto* w = panel_->addTool(DjtoolzTool::StabPlayer);
    w->show();
    auto wheel = [](QWidget* target, int notches, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QWheelEvent e(QPointF(5, 5), target->mapToGlobal(QPointF(5, 5)), QPoint(), QPoint(0, notches * 120), Qt::NoButton, mods,
                      Qt::NoScrollPhase, false);
        QApplication::sendEvent(target, &e);
    };
    QCOMPARE(w->tool()->settings().volume, 127);
    wheel(w, -1);                                    // over the tool itself
    QCOMPARE(w->tool()->settings().volume, 126);
    QCOMPARE(w->volumeSlider()->value(), 126);
    wheel(w, -3);
    QCOMPARE(w->tool()->settings().volume, 123);
    wheel(w->offsetSpin(), -2);                      // over a spin box: still the volume, the offset stays
    QCOMPARE(w->tool()->settings().volume, 121);
    QCOMPARE(w->tool()->settings().offsetMs, 0.0);
    wheel(w->nameEdit(), -1, Qt::ShiftModifier);     // shift = coarse steps of 10
    QCOMPARE(w->tool()->settings().volume, 111);
    wheel(w, +5);                                    // up: stops at 127
    QCOMPARE(w->tool()->settings().volume, 116);
    wheel(w, +50);
    QCOMPARE(w->tool()->settings().volume, 127);
    wheel(w, -200);                                  // and at 0
    QCOMPARE(w->tool()->settings().volume, 0);
    w->setVolume(64);
    QCOMPARE(w->tool()->settings().volume, 64);
    QVERIFY(w->volumeSlider()->toolTip().contains("64"));
    // survives a preset round trip
    QVERIFY(panel_->savePresetAs("vol"));
    panel_->removeTool(w->tool()->id());
    QVERIFY(panel_->loadPresetByName("vol"));
    QCOMPARE(panel_->toolWidgets()[0]->volumeSlider()->value(), 64);
}

void TestDjtoolzUi::functionKeysSwitchTools()
{
    for (int i = 0; i < 14; ++i)
        panel_->addTool(DjtoolzTool::Bombomatic);
    auto on = [&](int i) { return panel_->toolWidgets()[i]->tool()->settings().enabled; };
    QCOMPARE(panel_->toolWidgets()[0]->keyLabel(), QString("F1"));
    QCOMPARE(panel_->toolWidgets()[11]->keyLabel(), QString("F12"));
    QVERIFY(panel_->toolWidgets()[12]->keyLabel().isEmpty());      // only twelve keys
    QVERIFY(panel_->toolWidgets()[2]->onButton()->toolTip().contains("F3"));
    QVERIFY(!on(0) && !on(2));
    QVERIFY(panel_->toggleToolAt(2));              // what F3 does
    QVERIFY(on(2));
    QVERIFY(!on(0) && !on(1));
    QVERIFY(panel_->toggleToolAt(2));
    QVERIFY(!on(2));
    QVERIFY(panel_->toggleToolAt(11));
    QVERIFY(on(11));
    QVERIFY(!panel_->toggleToolAt(14));            // no such tool
    QVERIFY(!panel_->toggleToolAt(-1));
    // the keys follow the order: removing the first tool moves everyone up
    const int id = panel_->toolWidgets()[1]->tool()->id();
    panel_->removeTool(panel_->toolWidgets()[0]->tool()->id());
    QCOMPARE(panel_->toolWidgets()[0]->tool()->id(), id);
    QCOMPARE(panel_->toolWidgets()[0]->keyLabel(), QString("F1"));
    QCOMPARE(panel_->toolWidgets()[11]->keyLabel(), QString("F12"));
    QCOMPARE(panel_->toolWidgets()[12]->keyLabel(), QString());
}

void TestDjtoolzUi::drumIconsExistAndDiffer()
{
    const QIcon pink = djtoolzIcon(DjtoolzIcon::Pink);
    QVERIFY(!pink.isNull());
    QVERIFY(!pink.pixmap(24, 24).isNull());
    QVERIFY(image(pink) != image(djtoolzIcon(DjtoolzIcon::Red)));
    QVERIFY(image(pink) != image(djtoolzIcon(DjtoolzIcon::Yellow)));
    QVERIFY(image(djtoolzIcon(DjtoolzIcon::Red)) != image(djtoolzIcon(DjtoolzIcon::Yellow)));
    QSet<QByteArray> seen;
    for (int v = 0; v < 6; ++v) {
        const QIcon on = drumVoiceIcon(v, true), off = drumVoiceIcon(v, false);
        QVERIFY(!on.isNull() && !off.isNull());
        const QImage a = on.pixmap(24, 24).toImage(), b = off.pixmap(24, 24).toImage();
        QCOMPARE(a.size(), QSize(24, 24));
        QVERIFY2(a != b, qPrintable(QString::number(v)));            // lit differs from not lit
        QByteArray ba((const char*)a.constBits(), int(a.sizeInBytes()));
        QVERIFY(!seen.contains(ba));                                  // every voice has its own picture
        seen.insert(ba);
    }
    QVERIFY(drumVoiceIcon(-1, true).isNull());
    QVERIFY(drumVoiceIcon(6, false).isNull());
}

void TestDjtoolzUi::smallWheelDeltasAddUpToNotches()
{
    // many mice and touchpads send 15, 30 ... units per event instead of 120: they must add up, not round to 0
    auto* w = panel_->addTool(DjtoolzTool::Bombomatic);
    w->show();
    auto wheel = [](QWidget* target, QPoint delta, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QWheelEvent e(QPointF(5, 5), target->mapToGlobal(QPointF(5, 5)), QPoint(), delta, Qt::NoButton, mods, Qt::NoScrollPhase, false);
        QApplication::sendEvent(target, &e);
    };
    for (int i = 0; i < 7; ++i)
        wheel(w->nameEdit(), QPoint(0, -15));            // 7 * 15 = 105: not a notch yet
    QCOMPARE(w->tool()->settings().volume, 127);
    wheel(w->nameEdit(), QPoint(0, -15));                // 120: one notch
    QCOMPARE(w->tool()->settings().volume, 126);
    for (int i = 0; i < 4; ++i)
        wheel(w, QPoint(0, -30));                        // 4 * 30 = 120
    QCOMPARE(w->tool()->settings().volume, 125);
    wheel(w, QPoint(0, -60));
    wheel(w, QPoint(0, +120));                           // turning the other way drops the rest
    QCOMPARE(w->tool()->settings().volume, 126);
    wheel(w, QPoint(-120, 0));                           // a sideways wheel counts too (shift + wheel on some systems)
    QCOMPARE(w->tool()->settings().volume, 125);
    wheel(w, QPoint(0, 0));                              // nothing
    QCOMPARE(w->tool()->settings().volume, 125);
    wheel(w, QPoint(0, 240));                            // two notches in one event
    QCOMPARE(w->tool()->settings().volume, 127);
}

void TestDjtoolzUi::greenButtonSitsRightOfTheOnOffButtonInEveryTool()
{
    for (auto t : { DjtoolzTool::Bombomatic, DjtoolzTool::StabPlayer, DjtoolzTool::AcidLoop, DjtoolzTool::OneShot, DjtoolzTool::MidiMatic,
                    DjtoolzTool::Dembomatik, DjtoolzTool::DrumMachine }) {
        auto* w = panel_->addTool(t);
        w->show();
        QTest::qWait(10);
        QToolButton* green = w->oneBarButton() ? w->oneBarButton() : w->fireButton();    // 1 bar, or Fire for the one shot
        QVERIFY2(green, qPrintable(w->tool()->typeName()));
        QVERIFY2(!(w->oneBarButton() && w->fireButton()), qPrintable(w->tool()->typeName()));    // one green button per tool
        const QPoint on = w->onButton()->mapTo(w, QPoint(0, 0)), g = green->mapTo(w, QPoint(0, 0)), name = w->nameEdit()->mapTo(w, QPoint(0, 0));
        QCOMPARE(g.y(), on.y());                                         // the same line
        QVERIFY2(g.x() > on.x() && g.x() < name.x(), qPrintable(w->tool()->typeName()));   // between the on/off button and the name
        QCOMPARE(g.x() - (on.x() + w->onButton()->width()) < 12, true);  // right next to it
    }
}

QTEST_MAIN(TestDjtoolzUi)
#include "test_djtoolz_ui.moc"
