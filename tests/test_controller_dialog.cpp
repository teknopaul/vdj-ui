#include <QtTest/QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <memory>
#include "midi/alsa_clock.h"
#include "midi/midi_map_input.h"
#include "midi/sampler_midi_control.h"
#include "ui/controller_dialog.h"
#include "ui/sampler_controller.h"
#include "ui/sampler_module_widget.h"
#include "ui/sampler_panel.h"

using namespace vdj;

class TestControllerDialog : public QObject {
    Q_OBJECT

    QTemporaryDir* dir_ = nullptr;
    SamplerMidiControl* ctl_ = nullptr;
    QString path_;

    static QByteArray slurp(const QString& p) { QFile f(p); f.open(QIODevice::ReadOnly); return f.readAll(); }

private slots:
    void init()
    {
        dir_ = new QTemporaryDir;
        path_ = dir_->filePath("c.adjmm");
        QFile f(path_);
        f.open(QIODevice::WriteOnly);
        f.write("# my deck map\nstart 0 80 127\nstrip1_mute 0 1 127\n");
        f.close();
        ctl_ = new SamplerMidiControl;
        QVERIFY(ctl_->loadFromFile(path_));
    }
    void cleanup()
    {
        delete ctl_;
        delete dir_;
    }

    void showsTheFile()
    {
        ControllerDialog d(ctl_);
        QCOMPARE(d.cellText(0, 1), QString("any / CC 1"));
        QCOMPARE(d.cellText(0, 2), QString("-"));
        QCOMPARE(d.table()->item(0, 0)->text(), QString("any / CC 1"));
        QVERIFY(d.pickupBox()->isChecked());
        QVERIFY(d.recForwardBox()->isChecked());
    }

    void learnAButtonKeepsTheValue()
    {
        ControllerDialog d(ctl_);
        d.startLearn(1, 3);                                        // solo, strip 3
        QVERIFY(d.learning());
        QVERIFY(ctl_->learning());
        ctl_->onControlEvent(4, 35, 127, "x");
        QVERIFY(!d.learning());
        QVERIFY(!ctl_->learning());
        QCOMPARE(d.cellText(1, 3), QString("any / CC 35"));        // any channel is the default
        QVERIFY(d.saveFile());
        std::unique_ptr<AdjmmMap> m(AdjmmMap::readFromFile(path_));
        QCOMPARE(m->named.at("strip3_solo").controller, uint8_t(35));
        QCOMPARE(m->named.at("strip3_solo").value, uint8_t(127));
        QCOMPARE(m->named.at("strip3_solo").channel, uint8_t(0));
    }

    void learnAFaderWritesAny()
    {
        ControllerDialog d(ctl_);
        d.startLearn(5, 2);
        ctl_->onControlEvent(2, 9, 77, "x");
        QVERIFY(d.saveFile());
        std::unique_ptr<AdjmmMap> m(AdjmmMap::readFromFile(path_));
        QCOMPARE(m->named.at("strip2_fader").value, uint8_t(ADJMM_ANY_VALUE));
        QVERIFY(slurp(path_).contains("strip2_fader"));
        QVERIFY(slurp(path_).contains("*"));
    }

    void anyChannelOffWritesTheChannel()
    {
        ControllerDialog d(ctl_);
        d.anyChannelBox()->setChecked(false);
        d.startLearn(0, 2);
        ctl_->onControlEvent(5, 50, 127, "x");
        QCOMPARE(d.cellText(0, 2), QString("ch 5 / CC 50"));
        QVERIFY(d.saveFile());
        std::unique_ptr<AdjmmMap> m(AdjmmMap::readFromFile(path_));
        QCOMPARE(m->named.at("strip2_mute").channel, uint8_t(5));
    }

    void learnANote()
    {
        ControllerDialog d(ctl_);
        d.startLearn(0, 4);
        ctl_->onNoteEvent(1, 40, 100, true, "x");
        QCOMPARE(d.cellText(0, 4), QString("any / N 40"));
        QVERIFY(d.saveFile());
        std::unique_ptr<AdjmmMap> m(AdjmmMap::readFromFile(path_));
        QCOMPARE(m->named.at("strip4_mute").controller, uint8_t(ADJMM_NOTEON));
        QCOMPARE(m->named.at("strip4_mute").value, uint8_t(40));
        // the loaded map now reacts to the note
        QCOMPARE(ctl_->map().decodeNote(1, 40, 90, true).strip, 4);
    }

    void aKnobRejectsANote()
    {
        ControllerDialog d(ctl_);
        d.startLearn(4, 1);
        ctl_->onNoteEvent(1, 40, 100, true, "x");
        QVERIFY(d.learning());                                     // still waiting
        QCOMPARE(d.cellText(4, 1), QString("-"));
        ctl_->onControlEvent(1, 16, 5, "x");
        QVERIFY(!d.learning());
        QCOMPARE(d.cellText(4, 1), QString("any / CC 16"));
    }

    void learnStripOneFillsTheRow()
    {
        ControllerDialog d(ctl_);
        d.fillBox()->setChecked(true);
        d.startLearn(5, 1);
        ctl_->onControlEvent(1, 10, 5, "x");
        for (int s = 1; s <= 8; ++s)
            QCOMPARE(d.cellText(5, s), QString("any / CC %1").arg(9 + s));
        // learning another strip does not fill
        d.startLearn(4, 3);
        ctl_->onControlEvent(1, 40, 5, "x");
        QCOMPARE(d.cellText(4, 4), QString("-"));
        // the fill stops at 127
        d.startLearn(2, 1);
        ctl_->onControlEvent(1, 125, 127, "x");
        QCOMPARE(d.cellText(2, 3), QString("any / CC 127"));
        QCOMPARE(d.cellText(2, 4), QString("-"));
    }

    void clearCellRemovesTheLine()
    {
        ControllerDialog d(ctl_);
        d.table()->setCurrentCell(0, 0);
        d.clearButton()->click();
        QCOMPARE(d.cellText(0, 1), QString("-"));
        QVERIFY(d.saveFile());
        QVERIFY(!slurp(path_).contains("strip1_mute"));
        QVERIFY(slurp(path_).contains("start 0 80 127"));
    }

    void trackAndModeRows()
    {
        ControllerDialog d(ctl_);
        d.startLearn(6, 1);
        ctl_->onControlEvent(1, 58, 127, "x");
        d.startLearn(7, 1);
        ctl_->onControlEvent(1, 59, 127, "x");
        d.startLearn(8, 1);
        ctl_->onControlEvent(1, 60, 127, "x");
        QVERIFY(d.saveFile());
        std::unique_ptr<AdjmmMap> m(AdjmmMap::readFromFile(path_));
        QCOMPARE(m->named.at("track_back").controller, uint8_t(58));
        QCOMPARE(m->named.at("track_fwd").controller, uint8_t(59));
        QCOMPARE(m->named.at("mode_toggle").controller, uint8_t(60));
        QVERIFY(d.table()->item(6, 3)->flags() == Qt::NoItemFlags);   // only strip 1 has a track button
    }

    void saveKeepsTheDeckLinesAndComments()
    {
        ControllerDialog d(ctl_);
        d.startLearn(0, 2);
        ctl_->onControlEvent(1, 50, 127, "x");
        QVERIFY(d.saveFile());
        const QByteArray t = slurp(path_);
        QVERIFY(t.startsWith("# my deck map\nstart 0 80 127\nstrip1_mute 0 1 127\n"));
        std::unique_ptr<AdjmmMap> m(AdjmmMap::readFromFile(path_));
        QCOMPARE(m->ops[ADJMM_OP_START - 1].controller, uint8_t(80));
    }

    void checkBoxesWriteTheDirectives()
    {
        ControllerDialog d(ctl_);
        d.pickupBox()->setChecked(false);
        d.recForwardBox()->setChecked(false);
        d.sourceEdit()->setText("Studio");
        QVERIFY(d.saveFile());
        QVERIFY(slurp(path_).contains("#! pickup 0"));
        QVERIFY(slurp(path_).contains("#! rec_is_forward 0"));
        QVERIFY(slurp(path_).contains("#! source Studio"));
        QVERIFY(!ctl_->map().pickup());
        QVERIFY(!ctl_->map().recIsForward());
        QCOMPARE(ctl_->map().source(), QString("Studio"));
        ControllerDialog again(ctl_);                              // a new dialog shows them
        QVERIFY(!again.pickupBox()->isChecked());
        QCOMPARE(again.sourceEdit()->text(), QString("Studio"));
    }

    void resetLoadsTheExampleButSavesOnlyOnSave()
    {
        ControllerDialog d(ctl_);
        d.resetToExample();
        QCOMPARE(d.cellText(5, 1), QString("any / CC 0"));         // fader 1
        QCOMPARE(d.cellText(0, 8), QString("any / CC 55"));        // mute 8
        QCOMPARE(d.cellText(0, 1), QString("any / CC 48"));        // the old strip1_mute is replaced
        QVERIFY(!slurp(path_).contains("strip8_mute"));            // not saved yet
        QCOMPARE(d.sourceEdit()->text(), QString("nanoKONTROL"));
        QVERIFY(d.saveFile());
        QVERIFY(slurp(path_).contains("strip8_mute"));
        QVERIFY(slurp(path_).contains("start 0 80 127"));
    }

    void exampleIsInstalledOnceAndNeverOverwritten()
    {
        const QString p = dir_->filePath("sub/sampler-control.adjmm");
        QVERIFY(SamplerMidiControl::installExample(p));
        QVERIFY(QFile::exists(p));
        QVERIFY(slurp(p).contains("strip1_fader"));
        QVERIFY(QFileInfo(p).isWritable());
        QFile f(p);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write("mine\n");
        f.close();
        QVERIFY(SamplerMidiControl::installExample(p));
        QCOMPARE(slurp(p), QByteArray("mine\n"));
    }

    void learnStopsWhenTheDialogCloses()
    {
        {
            ControllerDialog d(ctl_);
            d.startLearn(0, 1);
            QVERIFY(ctl_->learning());
        }
        QVERIFY(!ctl_->learning());
    }

    void wiringDeliversPortEventsToTheSampler()
    {
        AlsaClock clock;
        QTemporaryDir sd;
        SamplerPanel panel(&clock, sd.path());
        panel.setAudioEnabled(false);
        auto* w = panel.addModule(SamplerModule::StabPlayer);
        MidiMapInput in;                                           // not started: the signals are emitted by hand
        wireSamplerControl(&in, &panel, ctl_);
        QVERIFY(!w->module()->enabled());
        emit in.controlEvent(1, 1, 127, "nanoKONTROL Studio");     // strip1_mute 0 1 127 in the temp file
        QVERIFY(w->module()->enabled());
        emit in.controlEvent(1, 1, 0, "nanoKONTROL Studio");       // release: nothing
        QVERIFY(w->module()->enabled());
    }
};

QTEST_MAIN(TestControllerDialog)
#include "test_controller_dialog.moc"
