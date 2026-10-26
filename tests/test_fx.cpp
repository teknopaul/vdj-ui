#include <QtTest/QtTest>
#include <QComboBox>
#include <QSlider>
#include <cmath>
#include "audio/fx_rack.h"
#include "audio/ladspa_host.h"
#include "ui/djtoolz_panel.h"
#include "ui/djtoolz_tool_widget.h"

using namespace vdj;

namespace {

const QString kKey = QStringLiteral("xone-ladspa.so|xone_hp");

// RMS of a sine of `hz` after the filter, once it has settled (stereo float frames)
double rmsThrough(LadspaInstance& fx, double hz, int frames = 44100)
{
    std::vector<float> buf(size_t(frames) * 2);
    for (int i = 0; i < frames; ++i)
        buf[size_t(2 * i)] = buf[size_t(2 * i + 1)] = 0.5f * float(std::sin(2.0 * M_PI * hz * i / 44100.0));
    fx.process(buf.data(), frames);
    double sum = 0;
    const int from = frames / 2;
    for (int i = from; i < frames; ++i)
        sum += double(buf[size_t(2 * i)]) * double(buf[size_t(2 * i)]);
    return std::sqrt(sum / (frames - from));
}

int controlIndex(const LadspaPluginInfo& info, const QString& name)
{
    for (size_t i = 0; i < info.controls.size(); ++i)
        if (info.controls[i].name == name)
            return int(i);
    return -1;
}

} // namespace

class TestFx : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        LadspaHost::rescan(QStringList() << QStringLiteral(VDJ_LADSPA_DIR));
    }

    void hostFindsTheXoneFilter()
    {
        LadspaPluginInfo info;
        QVERIFY(LadspaHost::find(kKey, &info));
        QCOMPARE(info.name, QString("Xone filter"));
        QCOMPARE(info.audioIn.size(), size_t(2));
        QCOMPARE(info.audioOut.size(), size_t(2));
        QCOMPARE(info.controls.size(), size_t(2));
        const LadspaControl& freq = info.controls[size_t(controlIndex(info, "Freq"))];
        const LadspaControl& wild = info.controls[size_t(controlIndex(info, "Wild"))];
        QCOMPARE(freq.hi, 14000.0f);
        QVERIFY(freq.logarithmic);
        QCOMPARE(wild.lo, 0.01f);
        QCOMPARE(wild.hi, 6.5f);
        QCOMPARE(wild.def, 0.01f);
    }

    void knobMapping()
    {
        LadspaControl c;
        c.lo = 20; c.hi = 14000; c.logarithmic = true;
        QCOMPARE(ladspaControlFromUnit(c, 0.0), 20.0f);
        QCOMPARE(ladspaControlFromUnit(c, 1.0), 14000.0f);
        QVERIFY(std::abs(ladspaControlToUnit(c, ladspaControlFromUnit(c, 0.3)) - 0.3) < 1e-4);
    }

    void highPassCutsTheBassAndKeepsTheTreble()
    {
        LadspaPluginInfo info;
        QVERIFY(LadspaHost::find(kKey, &info));
        auto fx = LadspaInstance::create(info);
        QVERIFY(fx);
        fx->setControl(controlIndex(info, "Freq"), 2000.0f);
        const double low = rmsThrough(*fx, 100.0);
        const double high = rmsThrough(*fx, 10000.0);
        QVERIFY2(low < 0.01, qPrintable(QString::number(low)));
        QVERIFY2(high > 0.25, qPrintable(QString::number(high)));
    }

    void wildBoostsTheCutoff()
    {
        LadspaPluginInfo info;
        QVERIFY(LadspaHost::find(kKey, &info));
        auto fx = LadspaInstance::create(info);
        fx->setControl(controlIndex(info, "Freq"), 2000.0f);
        fx->setControl(controlIndex(info, "Wild"), 0.01f);
        const double plain = rmsThrough(*fx, 2000.0);
        fx->setControl(controlIndex(info, "Wild"), 3.0f);
        const double wild = rmsThrough(*fx, 2000.0);
        QVERIFY2(wild > plain * 1.5, qPrintable(QString("%1 vs %2").arg(wild).arg(plain)));
    }

    void staysFiniteAtMaximumWild()
    {
        LadspaPluginInfo info;
        QVERIFY(LadspaHost::find(kKey, &info));
        auto fx = LadspaInstance::create(info);
        fx->setControl(controlIndex(info, "Freq"), 500.0f);
        fx->setControl(controlIndex(info, "Wild"), 6.5f);
        std::vector<float> buf(44100 * 2, 0.9f);
        fx->process(buf.data(), 44100);
        for (float v : buf)
            QVERIFY(std::isfinite(v) && std::abs(v) <= 1.0f);
    }

    // Freq fully down: Wild makes (almost) no difference, so it cannot distort the mix. (The filter itself still shifts the phase of
    // the bass a little, so the sound is compared with the same filter without resonance.)
    void minimumFreqLeavesTheSoundAlone()
    {
        LadspaPluginInfo info;
        QVERIFY(LadspaHost::find(kKey, &info));
        const LadspaControl& freq = info.controls[size_t(controlIndex(info, "Freq"))];
        QCOMPARE(freq.lo, 10.0f);
        const int frames = 44100;
        std::vector<float> in(size_t(frames) * 2);
        for (int i = 0; i < frames; ++i)                       // loud, with a kick-like low part
            in[size_t(2 * i)] = in[size_t(2 * i + 1)] = 0.4f * float(std::sin(2.0 * M_PI * 1000.0 * i / 44100.0))
                                                        + 0.4f * float(std::sin(2.0 * M_PI * 60.0 * i / 44100.0));
        auto run = [&](float wild) {
            auto fx = LadspaInstance::create(info);
            fx->setControl(controlIndex(info, "Freq"), freq.lo);
            fx->setControl(controlIndex(info, "Wild"), wild);
            std::vector<float> buf = in;
            fx->process(buf.data(), frames);
            return buf;
        };
        const auto plain = run(0.01f), wild = run(6.5f);
        double err = 0, ref = 0;
        for (int i = frames / 2; i < frames; ++i) {
            const double d = double(wild[size_t(2 * i)]) - double(plain[size_t(2 * i)]);
            err += d * d;
            ref += double(plain[size_t(2 * i)]) * double(plain[size_t(2 * i)]);
        }
        QVERIFY2(std::sqrt(err / ref) < 0.03, qPrintable(QString::number(std::sqrt(err / ref))));
    }

    void fxToolRoundTrips()
    {
        FxTool t;
        ToolSettings s = t.settings();
        s.enabled = true;
        t.setSettings(s);
        t.setPlugin(kKey);
        t.setValue("Freq", 1234.0f);
        auto back = DjtoolzTool::fromVariant(t.toVariant());
        QVERIFY(back);
        QCOMPARE(back->type(), DjtoolzTool::Fx);
        auto* fx = static_cast<FxTool*>(back.get());
        QCOMPARE(fx->spec().plugin, kKey);
        QCOMPARE(fx->spec().values.value("Freq"), 1234.0f);
        QVERIFY(!fx->bypassed());
    }

    void runnerFiltersThePlayedAudioOnlyWhileOn()
    {
        FxRack rack;
        auto tool = std::make_shared<FxTool>();
        tool->setPlugin(kKey);
        tool->setValue("Freq", 4000.0f);
        rack.add(tool);
        FxRunner runner(&rack);

        auto sine = [](std::vector<qint16>& b, double hz) {
            for (size_t i = 0; i < b.size() / 2; ++i)
                b[2 * i] = b[2 * i + 1] = qint16(12000.0 * std::sin(2.0 * M_PI * hz * double(i) / 44100.0));
        };
        std::vector<qint16> bass(44100 * 2);
        sine(bass, 100.0);
        const std::vector<qint16> original = bass;
        runner.process(bass.data(), 44100);
        QCOMPARE(bass, original);                              // off: untouched
        QCOMPARE(runner.activeEffects(), 0);

        ToolSettings s = tool->settings();
        s.enabled = true;
        tool->setSettings(s);
        runner.process(bass.data(), 44100);
        QCOMPARE(runner.activeEffects(), 1);
        int peak = 0;
        for (size_t i = bass.size() / 2; i < bass.size(); ++i)
            peak = std::max(peak, std::abs(int(bass[i])));
        QVERIFY2(peak < 500, qPrintable(QString::number(peak)));

        rack.remove(tool->id());
        bass = original;
        runner.process(bass.data(), 44100);
        QCOMPARE(bass, original);                              // removed from the rack
    }

    // VDJ_FX_SMOKE=1: runs every LADSPA plugin installed on this PC with its defaults for a second (finds hosting bugs)
    void installedPluginsRunWithTheirDefaults()
    {
        if (qEnvironmentVariableIsEmpty("VDJ_FX_SMOKE"))
            QSKIP("set VDJ_FX_SMOKE=1");
        LadspaHost::rescan();
        QVERIFY(!LadspaHost::plugins().empty());
        int ran = 0;
        for (const LadspaPluginInfo& info : LadspaHost::plugins()) {
            auto fx = LadspaInstance::create(info);
            if (!fx) {
                qWarning() << "no instance:" << info.key;
                continue;
            }
            std::vector<float> buf(4410 * 2);
            for (size_t i = 0; i < buf.size() / 2; ++i)
                buf[2 * i] = buf[2 * i + 1] = 0.3f * float(std::sin(0.05 * double(i)));
            for (int round = 0; round < 10; ++round)
                fx->process(buf.data(), 4410);
            ++ran;
        }
        qInfo() << ran << "of" << LadspaHost::plugins().size() << "plugins ran";
        QVERIFY(ran > 0);
        LadspaHost::rescan(QStringList() << QStringLiteral(VDJ_LADSPA_DIR));
    }

    void midiKnobAndFaderDriveTheFirstTwoControls()
    {
        QTemporaryDir dir;
        DjtoolzPanel panel(nullptr, dir.path());
        auto* w = panel.addTool(DjtoolzTool::Fx);
        w->fxPluginCombo()->setCurrentIndex(w->fxPluginCombo()->findData(kKey));
        auto* fx = static_cast<FxTool*>(w->tool().get());
        QVERIFY(w->setFxValue(0, 127));
        QCOMPARE(fx->spec().values.value("Freq"), 14000.0f);
        QVERIFY(w->setFxValue(1, 127));
        QCOMPARE(fx->spec().values.value("Wild"), 6.5f);
        QVERIFY(w->setFxValue(0, 0));
        QCOMPARE(fx->spec().values.value("Freq"), 10.0f);
        int v = -1;
        QVERIFY(w->fxValue(1, &v));
        QCOMPARE(v, 127);
        QVERIFY(!w->setFxValue(2, 5));
    }

    void anyTwoControlsCanBeChosenForTheController()
    {
        QTemporaryDir dir;
        DjtoolzPanel panel(nullptr, dir.path());
        auto* w = panel.addTool(DjtoolzTool::Fx);
        w->fxPluginCombo()->setCurrentIndex(w->fxPluginCombo()->findData(kKey));
        auto* fx = static_cast<FxTool*>(w->tool().get());
        QCOMPARE(w->fxKnobCombo()->count(), 2);
        QCOMPARE(w->fxKnobCombo()->currentText(), QString("Freq"));      // the defaults: first and second
        QCOMPARE(w->fxFaderCombo()->currentText(), QString("Wild"));
        w->fxKnobCombo()->setCurrentIndex(1);                          // the knob drives Wild, the fader Freq
        w->fxFaderCombo()->setCurrentIndex(0);
        QCOMPARE(fx->spec().knob, QString("Wild"));
        QVERIFY(w->setFxValue(0, 127));
        QCOMPARE(fx->spec().values.value("Wild"), 6.5f);
        QVERIFY(w->setFxValue(1, 127));
        QCOMPARE(fx->spec().values.value("Freq"), 14000.0f);
        // saved with the tool
        auto back = DjtoolzTool::fromVariant(fx->toVariant());
        QCOMPARE(static_cast<FxTool*>(back.get())->spec().knob, QString("Wild"));
        QCOMPARE(static_cast<FxTool*>(back.get())->spec().fader, QString("Freq"));
    }

    void widgetListsPluginsAndBuildsKnobs()
    {
        QTemporaryDir dir;
        DjtoolzPanel panel(nullptr, dir.path());
        auto* w = panel.addTool(DjtoolzTool::Fx);
        QVERIFY(w);
        QVERIFY(w->fxPluginCombo());
        QVERIFY(w->fxSliders().isEmpty());                     // "(none)"
        const int i = w->fxPluginCombo()->findData(kKey);
        QVERIFY(i > 0);
        w->fxPluginCombo()->setCurrentIndex(i);
        QCOMPARE(w->fxSliders().size(), 2);
        auto* fx = static_cast<FxTool*>(w->tool().get());
        QCOMPARE(fx->spec().plugin, kKey);
        w->fxSliders()[0]->setValue(1000);                     // Freq at the top
        QCOMPARE(fx->spec().values.value("Freq"), 14000.0f);
        QCOMPARE(panel.fxRack()->tools().size(), size_t(1));
        panel.removeTool(w->tool()->id());
        QCOMPARE(panel.fxRack()->tools().size(), size_t(0));
    }
};

QTEST_MAIN(TestFx)
#include "test_fx.moc"
