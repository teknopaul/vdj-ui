#include <QtTest/QtTest>

#include <QComboBox>
#include <QDockWidget>
#include <QMainWindow>
#include <QScreen>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QSignalSpy>
#include <QSpinBox>
#include <QUndoStack>

#include "trakcrate_wav.h"
#include "trakcrate/importer.h"
#include "ui/trakcrate_edit_panel.h"
#include "ui/trakcrate_edit_previewer.h"
#include "ui/trakcrate_track_editor.h"
#include "ui/waveform_widget.h"

namespace tc = trakcrate;
using namespace vdj;

namespace {

const double kBeatMs = 500.0;                // 120 bpm

tc::BeatGrid grid120()
{
    tc::BeatGrid g;
    g.bpm = 120;
    return g;
}

double ms(double beat) { return beat * kBeatMs; }

} // namespace

class TestEditUi : public QObject
{
    Q_OBJECT

private slots:
    void cutFromRangeIsWholeBarsFromABarLine()
    {
        QUndoStack undo;
        TrakcrateEditPanel p(&undo);
        p.setTrack(grid120(), 64000);
        QVERIFY(!p.cutButton()->isEnabled());                             // nothing selected
        QVERIFY(p.setRange(ms(9), ms(17)));
        QVERIFY(p.cutButton()->isEnabled());
        QSignalSpy consumed(&p, &TrakcrateEditPanel::rangeConsumed);
        QVERIFY(p.addCutFromRange());
        QCOMPARE(consumed.count(), 1);
        QCOMPARE(int(p.regions().size()), 1);
        QCOMPARE(p.regions()[0].startBeat, 8);                            // 9 -> bar line 8
        QCOMPARE(p.regions()[0].lenBeats, 8);                             // 17 -> 16
        QVERIFY(p.regions()[0].kind == tc::EditKind::Cut);
        QCOMPARE(p.regionList()->item(0)->text(), QStringLiteral("#1 cut bars 3-4 algo=cut 10 ms"));
        QVERIFY(!p.cutButton()->isEnabled());
        // A tiny range still removes one bar.
        QVERIFY(p.setRange(ms(40), ms(41)));
        QVERIFY(p.addCutFromRange());
        QCOMPARE(p.regions()[1].lenBeats, 4);
        QCOMPARE(p.regions()[1].startBeat, 40);
        // The waveform gets the bands where the beats are.
        const QList<WaveEditRegion> w = p.waveRegions();
        QCOMPARE(w.size(), 2);
        QCOMPARE(w[0].startMs, ms(8));
        QCOMPARE(w[0].endMs, ms(16));
        QVERIFY(!w[0].loop);
        QCOMPARE(w[1].number, 2);
    }

    void loopFromRangeSnapsLengthAndStartsOnABeat()
    {
        QUndoStack undo;
        TrakcrateEditPanel p(&undo);
        p.setTrack(grid120(), 64000);
        QVERIFY(p.setRange(ms(5), ms(11)));                               // 6 beats: nearer 8 than 4 by ratio
        QVERIFY(p.addLoopFromRange());
        const tc::EditRegion& r = p.regions()[0];
        QVERIFY(r.kind == tc::EditKind::Loop);
        QCOMPARE(r.startBeat, 5);
        QCOMPARE(r.lenBeats, 8);
        QCOMPARE(r.loopTimes, 4);
        QVERIFY(p.waveRegions()[0].loop);
        QVERIFY(p.setRange(ms(20), ms(100)));                             // longer than 16 bars: the biggest allowed
        QVERIFY(p.addLoopFromRange());
        QCOMPARE(p.regions()[1].lenBeats, 64);
    }

    void overlapAndMixingAreRefused()
    {
        QUndoStack undo;
        TrakcrateEditPanel p(&undo);
        p.setTrack(grid120(), 64000);
        QSignalSpy status(&p, &TrakcrateEditPanel::status);
        QVERIFY(p.setRange(ms(8), ms(16)));
        QVERIFY(p.addCutFromRange());
        QVERIFY(p.setRange(ms(12), ms(20)));                              // bars cannot be both cut and looped
        QVERIFY(!p.addLoopFromRange());
        QVERIFY(!p.addCutFromRange());
        QCOMPARE(int(p.regions().size()), 1);
        QVERIFY(status.count() >= 2);
        QVERIFY(status.last().first().toString().contains(QStringLiteral("overlaps")));
        QVERIFY(p.hasRange());                                            // the selection stays so the user can adjust
        QVERIFY(p.setRange(ms(16), ms(20)));
        QVERIFY(!p.addCutFromRange());                                    // touches the first cut: make it one cut
        QVERIFY(p.addLoopFromRange());                                    // a loop may follow a cut
    }

    void undoRedo()
    {
        QUndoStack undo;
        TrakcrateEditPanel p(&undo);
        p.setTrack(grid120(), 64000);
        QSignalSpy changed(&p, &TrakcrateEditPanel::regionsChanged);
        p.setRange(ms(8), ms(16));
        p.addCutFromRange();
        QCOMPARE(p.selected(), 0);
        undo.undo();
        QVERIFY(p.regions().empty());
        QCOMPARE(p.regionList()->count(), 0);
        undo.redo();
        QCOMPARE(int(p.regions().size()), 1);
        QVERIFY(p.setAlgo(tc::EditAlgo::Cross));
        QVERIFY(p.regions()[0].algo == tc::EditAlgo::Cross);
        undo.undo();
        QVERIFY(p.regions()[0].algo == tc::EditAlgo::Cut);
        undo.redo();
        QVERIFY(p.removeRegion(0));
        QVERIFY(p.regions().empty());
        undo.undo();
        QCOMPARE(int(p.regions().size()), 1);
        QVERIFY(changed.count() > 4);
    }

    void changingTheAlgoGivesALegalParameter()
    {
        QUndoStack undo;
        TrakcrateEditPanel p(&undo);
        p.setTrack(grid120(), 64000);
        p.setRange(ms(8), ms(16));
        p.addCutFromRange();
        QVERIFY(p.setParam(50));
        QCOMPARE(p.regions()[0].algoParam, 50.0);
        QVERIFY(!p.setParam(7));                                          // 7 ms is not offered: refused, nothing changes
        QCOMPARE(p.regions()[0].algoParam, 50.0);
        QVERIFY(p.setAlgo(tc::EditAlgo::Cross));
        QCOMPARE(p.regions()[0].algoParam, 1.0);
        QVERIFY(tc::validateRegions(grid120(), p.regions(), 64 * 44100).empty());
        QVERIFY(p.setParam(0.5));
        QVERIFY(p.setAlgo(tc::EditAlgo::TailOverlap));
        QVERIFY(tc::validateRegions(grid120(), p.regions(), 64 * 44100).empty());
        QVERIFY(p.setAlgo(tc::EditAlgo::EarlyCut));
        QVERIFY(tc::validateRegions(grid120(), p.regions(), 64 * 44100).empty());
        QVERIFY(p.setAlgo(tc::EditAlgo::Fadeout));
        QCOMPARE(p.paramBox()->count(), 3);
        QVERIFY(!p.setAlgo(tc::EditAlgo::Fadeout));                       // unchanged
        // A loop cannot take the cut-only algos.
        p.setRange(ms(32), ms(36));
        QVERIFY(p.addLoopFromRange());
        QCOMPARE(p.selected(), 2 - 1);
        QVERIFY(!p.setAlgo(tc::EditAlgo::TailOverlap));
        QVERIFY(!p.setAlgo(tc::EditAlgo::EarlyCut));
        QVERIFY(p.setAlgo(tc::EditAlgo::Cross));
    }

    void widgetsDriveTheSelectedRegion()
    {
        QUndoStack undo;
        TrakcrateEditPanel p(&undo);
        p.setTrack(grid120(), 64000);
        p.setRange(ms(8), ms(16));
        p.addCutFromRange();
        QComboBox* algo = p.algoBox();
        QCOMPARE(algo->count(), 5);                                       // cut, cross, fadeout, tailoverlap, earlycut
        algo->setCurrentIndex(algo->findData(int(tc::EditAlgo::Cross)));
        emit algo->activated(algo->currentIndex());
        QVERIFY(p.regions()[0].algo == tc::EditAlgo::Cross);
        QCOMPARE(p.paramBox()->currentData().toDouble(), 1.0);
        p.paramBox()->setCurrentIndex(0);
        emit p.paramBox()->activated(0);
        QCOMPARE(p.regions()[0].algoParam, 0.5);
        // A loop shows bars and times.
        p.setRange(ms(32), ms(36));
        p.addLoopFromRange();
        QCOMPARE(algo->count(), 3);
        p.loopTimesSpin()->setValue(8);
        QCOMPARE(p.regions()[1].loopTimes, 8);
        p.loopBarsBox()->setCurrentIndex(p.loopBarsBox()->findData(2));
        emit p.loopBarsBox()->activated(p.loopBarsBox()->currentIndex());
        QCOMPARE(p.regions()[1].lenBeats, 8);
        // Selecting through the list shows the other region's settings.
        p.regionList()->setCurrentRow(0);
        QCOMPARE(p.selected(), 0);
        QCOMPARE(algo->currentData().toInt(), int(tc::EditAlgo::Cross));
        // Early cut: settings are all legal.
        QVERIFY(p.setAlgo(tc::EditAlgo::EarlyCut));
        p.earlyShiftBox()->setCurrentIndex(p.earlyShiftBox()->findData(2));
        emit p.earlyShiftBox()->activated(p.earlyShiftBox()->currentIndex());
        p.earlyDirectionBox()->setCurrentIndex(1);
        emit p.earlyDirectionBox()->activated(1);
        QCOMPARE(p.regions()[0].earlyShift, 2);
        QVERIFY(p.regions()[0].earlyAfter);
        QCOMPARE(p.waveRegions()[0].startMs, ms(10));                      // the band shows the range really removed
        QCOMPARE(p.waveRegions()[0].endMs, ms(18));
        p.earlyJoinBox()->setCurrentIndex(1);
        emit p.earlyJoinBox()->activated(1);
        QVERIFY(p.regions()[0].earlyJoinAlgo == tc::EditAlgo::Cross);
        QCOMPARE(p.regions()[0].earlyJoinParam, 1.0);
    }

    void readoutShowsTheNewLength()
    {
        QUndoStack undo;
        TrakcrateEditPanel p(&undo);
        p.setTrack(grid120(), 64000);
        QCOMPARE(p.readout(), QStringLiteral("No edits yet"));
        p.setRange(ms(8), ms(16));
        p.addCutFromRange();
        QCOMPARE(p.readout(), QStringLiteral("Edit: 1:04 -> 1:00"));       // 4 s removed
        p.setRange(ms(32), ms(36));
        p.addLoopFromRange();                                              // 1 bar (2 s) x4 adds 6 s
        QCOMPARE(p.readout(), QStringLiteral("Edit: 1:04 -> 1:06"));
        QVERIFY(p.problem().isEmpty());
    }

    void noGridMeansNoEdit()
    {
        QUndoStack undo;
        TrakcrateEditPanel p(&undo);
        p.setTrack(tc::BeatGrid(), 64000);
        QVERIFY(!p.isEnabled());
        QVERIFY(!p.setRange(1000, 5000));
        QVERIFY(!p.addCutFromRange());
        p.setTrack(grid120(), 64000);
        QVERIFY(p.isEnabled());
        p.setRange(ms(8), ms(16));
        p.addCutFromRange();
        p.resetRegions();
        QVERIFY(p.regions().empty());
        QVERIFY(p.waveRegions().isEmpty());
    }

    void editorWiresTheWaveformToThePanel()
    {
        tc::fs::path tmp = tc::makeTempDir("edit-ui");
        tc::Library lib;
        QVERIFY(tc::Library::create(tmp / "lib") && tc::Library::open(tmp / "lib", &lib));
        tc::DecodedPcm pcm = tcwav::clicks(120, 0.25, 40);
        tcwav::writeWavMono(tmp / "src" / "a.wav", tcwav::mono(pcm), pcm.sampleRate);
        tc::ImportOptions io;
        io.analyse = true;
        QVERIFY(tc::importFolder(lib, tmp / "src", io).ok);
        const QString dir = QString::fromStdString(lib.trackDir("src", "a").string());

        TrakcrateTrackEditor ed;
        ed.resize(900, 700);
        ed.show();
        ed.setTrack(dir);
        WaveformWidget* w = ed.waveform();
        TrakcrateEditPanel* panel = ed.editPanel();
        QVERIFY(!panel->isVisible());
        QVERIFY(ed.editRegionsButton()->isEnabled());
        tc::Track t;
        QVERIFY(tc::loadTrack(dir.toStdString(), &t));
        const tc::BeatGrid g = tc::beatGridFrom(t.grid);
        if (g.empty()) QSKIP("no grid was detected for the synthetic track");

        ed.editRegionsButton()->setChecked(true);
        QVERIFY(panel->isVisible());
        QVERIFY(w->regionEditing());
        ed.editMarksButton()->setChecked(true);                            // the edit modes exclude each other
        QVERIFY(!ed.editRegionsButton()->isChecked());
        QVERIFY(!w->regionEditing());
        ed.editRegionsButton()->setChecked(true);
        QVERIFY(!ed.editMarksButton()->isChecked());

        // drag over beats 8..16 of the grid
        const double period = 60000.0 / g.bpm, first = double(g.firstBeatUs) / 1000.0;
        const int y = w->height() / 2;
        const int x0 = w->timeX(first + 8 * period) + 3, x1 = w->timeX(first + 16 * period) - 2;
        QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, QPoint(x0, y));
        QTest::mouseMove(w, QPoint((x0 + x1) / 2, y));
        QTest::mouseMove(w, QPoint(x1, y));
        QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, QPoint(x1, y));
        QVERIFY(panel->hasRange());
        QVERIFY(panel->addCutFromRange());
        QCOMPARE(w->editRegions().size(), 1);
        QCOMPARE(w->editRegionSelected(), 0);
        QVERIFY(!w->editRegions()[0].loop);
        QVERIFY(std::fabs(w->editRegions()[0].startMs - (first + 8 * period)) < 0.01);       // exactly on the grid
        QVERIFY(std::fabs(w->editRegions()[0].endMs - (first + 16 * period)) < 0.01);
        // the band is drawn: the pixel inside differs from one outside
        const QImage img = w->grab().toImage();
        const int inside = (w->timeX(w->editRegions()[0].startMs) + w->timeX(w->editRegions()[0].endMs)) / 2;
        w->setEditRegions({});
        const QImage bare = w->grab().toImage();
        QVERIFY(img.pixel(inside, y) != bare.pixel(inside, y));
        // a click inside the band selects it, and sets the point a preview starts from (snapped to the beat)
        QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, QPoint(inside + 7, y));
        QCOMPARE(panel->selected(), 0);
        QVERIFY(panel->previewStart() >= 0);
        QCOMPARE(w->previewPoint(), panel->previewStart());
        const double beats = (panel->previewStart() - first) / period;
        QVERIFY(std::fabs(beats - std::round(beats)) < 1e-3);
        const int emptyX = w->timeX(first + 3 * period) + 3;                      // outside every band
        QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, QPoint(emptyX, y));
        QVERIFY(std::fabs(panel->previewStart() - (first + 3 * period)) < period);
        QCOMPARE(panel->selected(), 0);                                           // the selection is kept
        // Space on the waveform plays the edit from the play marker and a time line moves; Space again stops it
        panel->setPreviewStart(first + 2 * period);
        QSignalSpy moved(ed.previewer(), &EditPreviewer::positionChanged);
        int sequencerSpace = 0;                                                   // the application wide Space of the main window (sequencer play / pause)
        auto* appSpace = new QShortcut(QKeySequence(Qt::Key_Space), &ed);
        appSpace->setContext(Qt::ApplicationShortcut);
        connect(appSpace, &QShortcut::activated, &ed, [&sequencerSpace] { ++sequencerSpace; });
        w->setFocus();
        QTest::keyClick(w, Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(ed.previewer()->isPlaying() && moved.count() > 2, 20000);
        QTRY_VERIFY(w->playPosition() > first + 2 * period - 100);                // the time line is on the waveform
        QTest::keyClick(w, Qt::Key_Space);
        QVERIFY(!ed.previewer()->isPlaying());
        QCOMPARE(sequencerSpace, 0);                                              // the preview got the key, the sequencer did not
        QCOMPARE(w->playPosition(), -1.0);
        ed.editRegionsButton()->setFocus();                                       // focus on a button of the editor: Space still belongs to the preview
        QTest::keyClick(ed.editRegionsButton(), Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(ed.previewer()->isPlaying() || ed.previewer()->isBusy(), 5000);
        ed.previewer()->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!ed.previewer()->isBusy(), 20000);               // a cancelled render finishes first
        QCOMPARE(sequencerSpace, 0);
        ed.editRegionsButton()->setChecked(false);                                // not in edit mode: the shortcut works as before
        w->setFocus();
        QTest::keyClick(w, Qt::Key_Space);
        QCOMPARE(sequencerSpace, 1);
        ed.editRegionsButton()->setChecked(true);
        w->setFocus();
        // changing the edit while it plays stops the preview
        QTest::keyClick(w, Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(ed.previewer()->isPlaying(), 20000);
        QVERIFY(panel->removeRegion(0));
        QVERIFY(!ed.previewer()->isPlaying());
        ed.undoStack()->undo();
        // Ctrl+Z of the editor takes the cut back, and another track resets the list
        ed.undoStack()->undo();
        QVERIFY(panel->regions().empty());
        QCOMPARE(w->editRegions().size(), 0);
        ed.undoStack()->redo();
        QCOMPARE(int(panel->regions().size()), 1);
        ed.setTrack(QString());
        QVERIFY(panel->regions().empty());
        QVERIFY(!ed.editRegionsButton()->isChecked());
        std::error_code ec;
        tc::fs::remove_all(tmp, ec);
    }

    void editModePopsTheDockOutFullWidth()
    {
        tc::fs::path tmp = tc::makeTempDir("edit-ui-dock");
        tc::Library lib;
        QVERIFY(tc::Library::create(tmp / "lib") && tc::Library::open(tmp / "lib", &lib));
        tc::DecodedPcm pcm = tcwav::clicks(120, 0.25, 40);
        tcwav::writeWavMono(tmp / "src" / "a.wav", tcwav::mono(pcm), pcm.sampleRate);
        tc::ImportOptions io;
        io.analyse = true;
        QVERIFY(tc::importFolder(lib, tmp / "src", io).ok);
        QMainWindow win;
        auto* ed = new TrakcrateTrackEditor;
        auto* dock = new QDockWidget(QStringLiteral("trakcrate track"), &win);
        dock->setWidget(ed);
        win.addDockWidget(Qt::BottomDockWidgetArea, dock);
        win.resize(500, 400);
        win.show();
        ed->setTrack(QString::fromStdString(lib.trackDir("src", "a").string()));
        tc::Track t;
        QVERIFY(tc::loadTrack(lib.trackDir("src", "a"), &t));
        if (tc::beatGridFrom(t.grid).empty()) QSKIP("no grid was detected for the synthetic track");
        QVERIFY(!dock->isFloating());
        ed->editRegionsButton()->setChecked(true);
        QVERIFY(dock->isFloating());
        const QRect avail = dock->screen()->availableGeometry();
        QCOMPARE(dock->width(), avail.width());
        ed->editRegionsButton()->setChecked(false);
        QVERIFY(!dock->isFloating());                                             // back where it was
        std::error_code ec;
        tc::fs::remove_all(tmp, ec);
    }
};

QTEST_MAIN(TestEditUi)
#include "test_trakcrate_edit_ui.moc"
