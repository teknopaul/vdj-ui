#include <QtTest/QtTest>

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QUndoStack>

#include "trakcrate_wav.h"
#include "ui/trakcrate_analysis.h"
#include "ui/trakcrate_track_editor.h"
#include "ui/waveform_widget.h"
#include "trakcrate/cuemark_edit.h"
#include "trakcrate/grid_edit.h"
#include "trakcrate/importer.h"
#include "trakcrate/track.h"

namespace tc = trakcrate;
using namespace vdj;

class TestTrakcrateNudge : public QObject
{
    Q_OBJECT

    tc::fs::path tmp_;
    tc::Library lib_;
    QString dir_;

    tc::Track load() { tc::Track t; tc::loadTrack(dir_.toStdString(), &t); return t; }

private slots:
    void init()
    {
        tmp_ = tc::makeTempDir("trakcrate-nudge");
        QVERIFY(!tmp_.empty());
        QVERIFY(tc::Library::create(tmp_ / "lib") && tc::Library::open(tmp_ / "lib", &lib_));
        tc::DecodedPcm p = tcwav::clicks(120, 0.25, 12);
        tcwav::writeWavMono(tmp_ / "src" / "a.wav", tcwav::mono(p), p.sampleRate);
        tc::ImportOptions io;
        io.analyse = true;
        QVERIFY(tc::importFolder(lib_, tmp_ / "src", io).ok);
        dir_ = QString::fromStdString(lib_.trackDir("src", "a").string());
    }
    void cleanup() { std::error_code ec; tc::fs::remove_all(tmp_, ec); }

    void arithmetic()
    {
        tc::BeatGrid g;
        g.bpm = 120;
        g.firstBeatUs = 250000;
        QCOMPARE(tc::nudgedGrid(g, 10).firstBeatUs, 260000LL);
        QCOMPARE(tc::nudgedGrid(g, -0.1).firstBeatUs, 249900LL);
        QCOMPARE(tc::nudgedGrid(g, -1000).firstBeatUs, 0LL);                    // never before the start
        QCOMPARE(tc::nudgedGrid(g, 3).bpm, 120.0);
        QVERIFY(tc::nudgedGrid(tc::BeatGrid(), 5).empty());
        QCOMPARE(tc::gridWithBeatAt(g, 1790).firstBeatUs, 290000LL);            // the beat at 1750 moves to 1790
        QCOMPARE(tc::gridWithBeatAt(g, 100).firstBeatUs, 100000LL);             // the first beat moves to 100
        QCOMPARE(tc::gridWithBeatAt(g, 5).firstBeatUs, 5000LL);                 // the beat at 250 moves to 5
        QVERIFY(tc::gridWithBeatAt(tc::BeatGrid(), 5).empty());
        QCOMPARE(tc::gridOffsetMs(tc::nudgedGrid(g, 3.4), g), 3.4);
    }
    void fileContentAndDependents()
    {
        tc::Track t = load();
        QVERIFY(t.hasGrid);
        tc::CuemarkFile f;
        tc::addCuemark(&f, tc::beatGridFrom(t.grid), 2000, "x");
        QVERIFY(tc::saveCuemarkEdit(&t, f));
        t = load();
        const double beatBefore = t.cuemarks.marks[0].beat;
        const tc::BeatGrid g = tc::nudgedGrid(tc::beatGridFrom(t.grid), 100);   // a fifth of a beat later
        QVERIFY(tc::saveGridEdit(&t, g));
        t = load();
        QCOMPARE(tc::beatGridFrom(t.grid).firstBeatUs, g.firstBeatUs);
        QCOMPARE(tc::origin(t.source, "beatgrid"), std::string("edited"));
        QCOMPARE(t.cuemarks.marks[0].ms, 2000.0);                                // ms is the truth
        QVERIFY(std::fabs(t.cuemarks.marks[0].beat - (beatBefore - 0.2)) < 0.01);  // only the beat column moved
    }
    void editorNudgesMergeAndUndo()
    {
        TrakcrateTrackEditor ed;
        QVERIFY(!ed.nudgeGrid(1));                                              // no track
        ed.setTrack(dir_);
        QSignalSpy grid(&ed, &TrakcrateTrackEditor::gridChanged);
        const long long first = tc::beatGridFrom(load().grid).firstBeatUs;
        QVERIFY(ed.nudgeGrid(1));
        QVERIFY(ed.nudgeGrid(1));
        QVERIFY(ed.nudgeGrid(0.5));
        QCOMPARE(tc::beatGridFrom(load().grid).firstBeatUs, first + 2500);
        QCOMPARE(ed.undoStack()->count(), 1);                                    // three nudges, one undo step
        QCOMPARE(ed.gridOffsetMs(), 2.5);
        QVERIFY(ed.gridReadout()->text().contains("+2.5"));
        QVERIFY(grid.count() >= 3);
        QCOMPARE(tc::origin(load().source, "beatgrid"), std::string("edited"));
        const tc::BeatGrid g2 = tc::beatGridFrom(load().grid);
        QVERIFY(!ed.nudgeGrid(0));                                               // nothing to do
        ed.undoStack()->undo();
        QCOMPARE(tc::beatGridFrom(load().grid).firstBeatUs, first);
        QCOMPARE(tc::origin(load().source, "beatgrid"), std::string("generated"));   // the origin goes back too
        ed.undoStack()->redo();
        QCOMPARE(tc::beatGridFrom(load().grid).firstBeatUs, g2.firstBeatUs);
        QVERIFY(ed.setBeatOneAt(1260));                                          // the beat near 1250 lands on 1260
        {
            const tc::BeatGrid now = tc::beatGridFrom(load().grid);
            const double periodUs = 60000000.0 / now.bpm;
            // some beat of the new grid sits exactly on 1260 ms
            const double k = (1260000.0 - double(now.firstBeatUs)) / periodUs;
            QVERIFY(std::fabs(k - std::round(k)) * periodUs < 1.0);
        }
        QVERIFY(ed.revertGrid());
        QCOMPARE(tc::beatGridFrom(load().grid).firstBeatUs, first);
        QCOMPARE(ed.gridOffsetMs(), 0.0);
    }
    void rekordboxGridsAskFirst()
    {
        tc::Track t = load();
        tc::setOrigin(t.source, "beatgrid", "rekordbox");
        QVERIFY(tc::saveSource(t));
        TrakcrateTrackEditor ed;
        int asked = 0;
        bool answer = false;
        ed.setGridConfirmer([&](const QString&) { ++asked; return answer; });
        ed.setTrack(dir_);
        const long long first = tc::beatGridFrom(load().grid).firstBeatUs;
        QVERIFY(!ed.nudgeGrid(5));                                               // declined: nothing changes
        QCOMPARE(asked, 1);
        QCOMPARE(tc::beatGridFrom(load().grid).firstBeatUs, first);
        QCOMPARE(tc::origin(load().source, "beatgrid"), std::string("rekordbox"));
        answer = true;
        QVERIFY(ed.nudgeGrid(5));
        QVERIFY(ed.nudgeGrid(5));                                                // asked once more at most: now confirmed
        QCOMPARE(asked, 2);
        ed.undoStack()->undo();
        QCOMPARE(tc::origin(load().source, "beatgrid"), std::string("rekordbox"));
    }
    void waveformDragAndKeysReportNudges()
    {
        TrakcrateTrackEditor ed;
        ed.setTrack(dir_);
        ed.resize(600, 600);
        ed.show();
        WaveformWidget* w = ed.waveform();
        ed.gridEditButton()->setChecked(true);
        QVERIFY(w->gridEditing());
        QSignalSpy nudged(w, &WaveformWidget::gridNudged);
        QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, QPoint(100, 20));
        QTest::mouseMove(w, QPoint(130, 20));
        QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, QPoint(130, 20));
        QCOMPARE(nudged.count(), 1);
        const double expect = 30.0 * w->viewSpan() / w->width();
        const double got = nudged.at(0).at(0).toDouble();
        const double span = w->viewSpan() > 0 ? w->viewSpan() : double(w->analysis().lengthMs());
        QVERIFY(std::fabs(got - 30.0 * span / w->width()) < 0.01);
        Q_UNUSED(expect);
        QTest::keyClick(w, Qt::Key_Right);
        QTest::keyClick(w, Qt::Key_Left, Qt::ShiftModifier);
        QTest::keyClick(w, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(nudged.count(), 4);
        QCOMPARE(nudged.at(1).at(0).toDouble(), 1.0);
        QCOMPARE(nudged.at(2).at(0).toDouble(), -10.0);
        QCOMPARE(nudged.at(3).at(0).toDouble(), 0.1);
        ed.editMarksButton()->setChecked(true);                                 // cuemark editing replaces grid editing
        QVERIFY(!w->gridEditing());
        QVERIFY(w->cuemarkEditing());
    }
};

QTEST_MAIN(TestTrakcrateNudge)
#include "test_trakcrate_nudge.moc"
