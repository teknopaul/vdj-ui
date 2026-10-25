#include <QtTest/QtTest>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include "audio/drum_kits.h"
#include "midi/alsa_clock.h"
#include "ui/drum_editor.h"
#include "ui/drum_voice_selector.h"
#include "ui/sample_chooser.h"
#include "ui/sampler_module_widget.h"
#include "ui/sampler_panel.h"
#include "ui/step_chooser.h"
#include "ui/swing_slider.h"

using namespace vdj;

class TestDrumKits : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void saveLoadListRemoveRename();
    void roundTripOfAllVoices();
    void loadingAKitKeepsNameOnOffAndVolume();
    void forwardCompatibility();
    void badFiles();
    void sanitising();
    void noStrayTempFiles();
    void uiSaveChangeLoadRestores();
    void kitListIsSharedBetweenModules();
    void kitsAndPresetsAreIndependent();
    void missingSamplesAreShown();
    void deleteKitFromTheUi();

private:
    QTemporaryDir* dir_ = nullptr;
    AlsaClock clock_;
    SamplerPanel* panel_ = nullptr;

    static DrumKit sampleKit()
    {
        DrumKit k;
        k.swing = 66;
        for (int v = 0; v < kDrumVoices; ++v) {
            k.voices[v].samplePath = QString("/kit/%1 é.wav").arg(kDrumVoiceNames[v]);
            k.voices[v].volume = 30 + v * 15;
            k.voices[v].offsetMs = v * 4 - 9;
            k.voices[v].chokeSteps = kDrumChokeValues[v];
            for (int i = 0; i < kDrumSteps; ++i)
                k.voices[v].steps[i] = quint8((i + 2 * v) % 3);
        }
        return k;
    }
    static bool same(const DrumKit& a, const DrumKit& b)
    {
        if (a.swing != b.swing)
            return false;
        for (int v = 0; v < kDrumVoices; ++v)
            if (a.voices[v] != b.voices[v])
                return false;
        return true;
    }
};

void TestDrumKits::init()
{
    dir_ = new QTemporaryDir;
    panel_ = new SamplerPanel(&clock_, dir_->path());
    panel_->setAudioEnabled(false);
}

void TestDrumKits::cleanup()
{
    delete panel_;
    panel_ = nullptr;
    delete dir_;
    dir_ = nullptr;
}

void TestDrumKits::saveLoadListRemoveRename()
{
    DrumKits k(dir_->path());
    QVERIFY(k.list().isEmpty());
    QVERIFY(k.save("b kit", sampleKit()));
    QVERIFY(k.save("A kit", sampleKit()));
    QCOMPARE(k.list(), QStringList({ "A kit", "b kit" }));
    QVERIFY(k.exists("A kit"));
    QVERIFY(!k.exists("nope"));
    DrumKit second = sampleKit();
    second.swing = 5;
    QVERIFY(k.save("A kit", second));                // overwrite
    DrumKit back;
    QVERIFY(k.load("A kit", &back));
    QCOMPARE(back.swing, 5);
    QVERIFY(!k.rename("A kit", "b kit"));            // the target exists
    QVERIFY(k.rename("A kit", "C kit"));
    QCOMPARE(k.list(), QStringList({ "b kit", "C kit" }));
    QVERIFY(k.remove("b kit"));
    QVERIFY(!k.remove("b kit"));
    QCOMPARE(k.list(), QStringList({ "C kit" }));
    QVERIFY(!k.save("", sampleKit()));
    QVERIFY(!k.save("...", sampleKit()));
    QVERIFY(QFile::exists(dir_->path() + "/drum-kits/C kit.json"));
}

void TestDrumKits::roundTripOfAllVoices()
{
    DrumKits k(dir_->path());
    const DrumKit kit = sampleKit();
    QVERIFY(k.save("full", kit));
    DrumKit back;
    QVERIFY(k.load("full", &back));
    QVERIFY(same(kit, back));
    QCOMPARE(drumPatternToString(back.voices[3].steps), drumPatternToString(kit.voices[3].steps));
}

void TestDrumKits::loadingAKitKeepsNameOnOffAndVolume()
{
    auto m = SamplerModule::create(SamplerModule::DrumMachine);
    ModuleSettings s = m->settings();
    s.name = "my drums";
    s.enabled = true;
    s.volume = 55;
    s.gain = 1.5f;
    s.offsetMs = 3;
    m->setSettings(s);
    ModuleSettings t = m->settings();
    applyDrumKit(&t, sampleKit());
    m->setSettings(t);
    const ModuleSettings after = m->settings();
    QCOMPARE(after.name, QString("my drums"));
    QVERIFY(after.enabled);
    QCOMPARE(after.volume, 55);
    QCOMPARE(after.gain, 1.5f);
    QCOMPARE(after.offsetMs, 3.0);
    QCOMPARE(after.swing, 66);
    QCOMPARE(after.drums[2].samplePath, sampleKit().voices[2].samplePath);
    QVERIFY(same(drumKitFromSettings(after), sampleKit()));
}

void TestDrumKits::forwardCompatibility()
{
    DrumKits k(dir_->path());
    QDir().mkpath(k.kitDir());
    QFile f(k.kitPath("future"));
    f.open(QIODevice::WriteOnly);
    f.write(R"({"version":9,"swing":200,"extra":[1,2],"voices":[{"sample":"/a.wav","volume":50,"pattern":"1000100010001000","newKey":1},{"choke":4}]})");
    f.close();
    DrumKit kit;
    QVERIFY(k.load("future", &kit));
    QCOMPARE(kit.swing, 127);                        // clamped
    QCOMPARE(kit.voices[0].samplePath, QString("/a.wav"));
    QCOMPARE(kit.voices[0].volume, 50);
    QCOMPARE(int(kit.voices[0].steps[4]), 1);
    QCOMPARE(kit.voices[1].chokeSteps, 4);
    QCOMPARE(kit.voices[1].volume, 127);             // default for a missing key
    QCOMPARE(kit.voices[5].samplePath, QString());   // voices that are not in the file
}

void TestDrumKits::badFiles()
{
    DrumKits k(dir_->path());
    QDir().mkpath(k.kitDir());
    for (const char* text : { "", "{nope", "[1]", "7" }) {
        QFile f(k.kitPath("bad"));
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(text);
        f.close();
        DrumKit kit;
        QString err;
        QVERIFY2(!k.load("bad", &kit, &err), text);
        QVERIFY(!err.isEmpty());
    }
    DrumKit kit;
    QVERIFY(!k.load("missing", &kit));
    QVERIFY(!k.load("", &kit));
}

void TestDrumKits::sanitising()
{
    QCOMPARE(DrumKits::sanitise("  my/kit:1 "), QString("my_kit_1"));
    QCOMPARE(DrumKits::sanitise("../x"), QString("_x"));
    DrumKits k(dir_->path());
    QVERIFY(k.save("../escape", sampleKit()));
    QVERIFY(QFile::exists(k.kitDir() + "/_escape.json"));
    QVERIFY(!QFile::exists(dir_->path() + "/escape.json"));
}

void TestDrumKits::noStrayTempFiles()
{
    DrumKits k(dir_->path());
    QVERIFY(k.save("one", sampleKit()));
    QVERIFY(k.save("one", sampleKit()));
    QCOMPARE(QDir(k.kitDir()).entryList(QDir::Files).size(), 1);
}

void TestDrumKits::uiSaveChangeLoadRestores()
{
    auto* w = panel_->addModule(SamplerModule::DrumMachine);
    auto* ed = w->drumEditor();
    w->show();
    QCOMPARE(ed->kitCombo()->count(), 1);            // "(kits)"
    QVERIFY(!ed->deleteKitButton()->isEnabled());
    QTest::mouseClick(ed->stepChooser()->button(4), Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    ed->swing()->slider()->setValue(40);
    ed->chokeSpin()->setValue(2);
    QVERIFY(ed->saveKitAs("my kit"));
    QCOMPARE(ed->kitCombo()->count(), 2);
    QCOMPARE(ed->kitCombo()->currentText(), QString("my kit"));
    QVERIFY(ed->deleteKitButton()->isEnabled());
    // change it, then load the kit again
    QTest::mouseClick(ed->stepChooser()->button(4), Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));    // BD step 5 off
    QTest::mouseClick(ed->stepChooser()->button(9), Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    ed->swing()->slider()->setValue(99);
    ed->chokeSpin()->setValue(8);
    ed->selector()->button(2)->click();
    ed->sampleChooser()->setSample("/other.wav");
    QVERIFY(ed->loadKit("my kit"));
    ed->selectVoice(0);
    QCOMPARE(int(ed->stepChooser()->step(4)), int(StepOn));
    QCOMPARE(int(ed->stepChooser()->step(9)), int(StepOff));
    QCOMPARE(ed->swing()->value(), 40);
    QCOMPARE(ed->chokeSpin()->value(), 2);
    QCOMPARE(w->module()->settings().swing, 40);
    // selecting from the combo box applies it too
    ed->swing()->slider()->setValue(1);
    ed->kitCombo()->setCurrentIndex(1);
    emit ed->kitCombo()->activated(1);
    QCOMPARE(w->module()->settings().swing, 40);
}

void TestDrumKits::kitListIsSharedBetweenModules()
{
    auto* a = panel_->addModule(SamplerModule::DrumMachine);
    auto* b = panel_->addModule(SamplerModule::DrumMachine);
    QCOMPARE(b->drumEditor()->kitCombo()->count(), 1);
    a->drumEditor()->swing()->slider()->setValue(77);
    QVERIFY(a->drumEditor()->saveKitAs("shared"));
    QCOMPARE(b->drumEditor()->kitCombo()->count(), 2);               // refreshed through the panel
    QCOMPARE(b->drumEditor()->kitCombo()->itemText(1), QString("shared"));
    QVERIFY(b->drumEditor()->loadKit("shared"));
    QCOMPARE(b->module()->settings().swing, 77);
    QCOMPARE(b->module()->settings().name, b->module()->typeName());  // the name was not taken from the kit
    QVERIFY(!b->module()->settings().enabled);
}

void TestDrumKits::kitsAndPresetsAreIndependent()
{
    auto* w = panel_->addModule(SamplerModule::DrumMachine);
    QVERIFY(w->drumEditor()->saveKitAs("only a kit"));
    QVERIFY(panel_->savePresetAs("only a preset"));
    QCOMPARE(panel_->drumKits()->list(), QStringList({ "only a kit" }));
    QCOMPARE(panel_->presets()->list(), QStringList({ "only a preset" }));
    QVERIFY(QDir(dir_->path() + "/drum-kits").exists());
    QVERIFY(QDir(dir_->path() + "/sampler-presets").exists());
    QVERIFY(!QFile::exists(dir_->path() + "/sampler-presets/only a kit.json"));
    QVERIFY(!QFile::exists(dir_->path() + "/drum-kits/only a preset.json"));
}

void TestDrumKits::missingSamplesAreShown()
{
    auto* w = panel_->addModule(SamplerModule::DrumMachine);
    DrumKit kit = sampleKit();
    QVERIFY(panel_->drumKits()->save("gone", kit));
    QVERIFY(w->drumEditor()->loadKit("gone"));
    w->drumEditor()->selectVoice(0);
    QVERIFY(w->drumEditor()->sampleChooser()->nameLabel()->text().contains("missing"));
    QCOMPARE(w->module()->settings().drums[0].samplePath, kit.voices[0].samplePath);   // kept as it is
}

void TestDrumKits::deleteKitFromTheUi()
{
    auto* w = panel_->addModule(SamplerModule::DrumMachine);
    QVERIFY(w->drumEditor()->saveKitAs("temp"));
    QVERIFY(panel_->drumKits()->remove("temp"));
    panel_->drumKitsChanged();
    QCOMPARE(w->drumEditor()->kitCombo()->count(), 1);
    QVERIFY(!w->drumEditor()->deleteKitButton()->isEnabled());
}

QTEST_MAIN(TestDrumKits)
#include "test_drum_kits.moc"
