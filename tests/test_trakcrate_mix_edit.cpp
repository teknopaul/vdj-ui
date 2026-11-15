#include <QtTest/QtTest>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QMenu>
#include <QContextMenuEvent>
#include <QScrollBar>
#include <QTreeWidget>
#include <QUndoStack>

#include "trakcrate/analysis_wave.h"
#include "trakcrate/edit_output.h"
#include "trakcrate/library.h"
#include "trakcrate/mix_file.h"
#include "trakcrate/stretch_probe.h"
#include "ui/mix_filters_dialog.h"
#include "ui/mix_tree_dock.h"
#include "ui/trakcrate_edit_panel.h"
#include "ui/mix_stretch_dock.h"
#include "ui/studio_mix_window.h"

#include <fstream>

using namespace trakcrate;
using namespace vdj;

namespace {

struct World {
    fs::path root = makeTempDir("mixedit");
    Library lib;
    World(int tracks)
    {
        Library::create(root, nullptr);
        Library::open(root, &lib, nullptr);
        lib.createPlaylist("Set", nullptr);
        for (int i = 0; i < tracks; ++i) {
            const fs::path dir = lib.createTrackDir("House", "T" + std::to_string(i), nullptr);
            const DecodedPcm p = probeMaterial(20, 120);
            AudioTags tags;
            writeWav(dir / "track.wav", p, 120, tags, nullptr);
            KvDoc g;
            BeatGrid grid;
            grid.bpm = 120;
            setBeatGrid(g, grid);
            std::ofstream(dir / "beatgrid.txt") << g.serialise();
            std::ofstream(dir / "waveform.bin", std::ios::binary) << serialiseWaveform(analyseWaveform(p));
            lib.addToPlaylist("Set", dir, -1, nullptr);
        }
    }
    ~World() { std::error_code ec; fs::remove_all(root, ec); }
};

std::map<std::string, std::string> snapshot(const fs::path& dir)
{
    std::map<std::string, std::string> m;
    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        std::string b;
        readFile(e.path(), &b);
        m[e.path().string()] = b;
    }
    return m;
}

std::string xml(const Mix& m) { return mixToXml(m, fs::path()); }

using Pts = std::vector<std::pair<double, double>>;
Pts pts(const Curve& c)
{
    Pts p;
    for (const CurvePoint& q : c.points) p.push_back({q.beat, q.value});
    return p;
}

} // namespace

class TestMixEdit : public QObject
{
    Q_OBJECT
    std::unique_ptr<World> w_;
    std::unique_ptr<StudioMixWindow> win_;
    MixCanvas* c_ = nullptr;

    void drag(const QPoint& from, const QPoint& to, Qt::KeyboardModifiers mod = Qt::NoModifier, int steps = 4)
    {
        QTest::mousePress(c_, Qt::LeftButton, mod, from);
        for (int i = 1; i <= steps; ++i) QTest::mouseEvent(QTest::MouseMove, c_, Qt::NoButton, mod, from + (to - from) * i / steps);      // mouseMove() cannot carry modifiers
        QTest::mouseRelease(c_, Qt::LeftButton, mod, to);
    }
    QPoint laneMid(int lane, int x) { return QPoint(x, c_->laneRect(lane).center().y()); }

private slots:
    void initTestCase() { qputenv("XDG_CACHE_HOME", QByteArray((fs::temp_directory_path() / "vdj-mixedit-cache").c_str())); }

    void init()
    {
        w_ = std::make_unique<World>(3);
        win_ = std::make_unique<StudioMixWindow>([this]() -> const Library* { return &w_->lib; }, nullptr);
        NewMixDialog dlg(&w_->lib);
        dlg.selectPlaylist("Set");
        win_->showPlaylistMix("Set", dlg.tracks());
        win_->resize(1200, 700);
        win_->show();
        c_ = win_->canvas();
        c_->zoomFit();
    }
    void cleanup()
    {
        win_.reset();
        w_.reset();
    }

    void removingATrackKeepsThePlaylistAndUndoBringsItBack()
    {
        const std::vector<std::string> playlist = w_->lib.listPlaylists();
        const Mix before = win_->mix();
        QCOMPARE(before.tracks.size(), size_t(3));
        win_->removeTrack(1);
        const Mix& m = win_->mix();
        QCOMPARE(m.tracks.size(), size_t(2));
        QCOMPARE(m.tracks[0].file, before.tracks[0].file);
        QCOMPARE(m.tracks[1].file, before.tracks[2].file);
        QCOMPARE(int(win_->facts().size()), 2);
        QCOMPARE(c_->laneCount(), 2);
        QVERIFY(validateMix(m, win_->facts()).empty());
        QCOMPARE(m.tracks[0].next.lenBeats, 32.0);
        QCOMPARE(m.tracks[0].next.inStartBeat, 0.0);
        // the curves of the old pairs are gone: the new pair has exactly the cross-fade points
        QCOMPARE(pts(m.tracks[1].volume), (Pts{{0, 0}, {32, 100}}));
        QCOMPARE(w_->lib.listPlaylists(), playlist);
        win_->undoStack()->undo();
        QCOMPARE(win_->mix().tracks.size(), size_t(3));
        QCOMPARE(xml(win_->mix()), xml(before));
        QCOMPARE(c_->laneCount(), 3);
        QCOMPARE(int(win_->facts().size()), 3);
        win_->undoStack()->redo();
        QCOMPARE(win_->mix().tracks.size(), size_t(2));
    }

    void removingTheFirstOrLastTrackAndTheOnlyTrack()
    {
        win_->removeTrack(0);
        QCOMPARE(win_->mix().tracks.size(), size_t(2));
        QVERIFY(win_->mix().tracks[0].volume.points.empty() || win_->mix().tracks[0].volume.points.front().beat > 0);
        win_->removeTrack(1);
        QCOMPARE(win_->mix().tracks.size(), size_t(1));
        QVERIFY(win_->mix().tracks[0].volume.points.empty());
        win_->removeTrack(0);                                                    // refused
        QCOMPARE(win_->mix().tracks.size(), size_t(1));
        win_->undoStack()->undo();
        win_->undoStack()->undo();
        QCOMPARE(win_->mix().tracks.size(), size_t(3));
    }

    void draggingTheOutgoingStartMovesTheIncomingTrack()
    {
        const Mix before = win_->mix();
        QCOMPARE(before.tracks[0].next.outStartBeat, 8.0);
        drag(laneMid(0, c_->periodStartX(0)), laneMid(0, c_->xAt(6000)));            // beat 12
        const MixPair& p = win_->mix().tracks[0].next;
        QCOMPARE(p.outStartBeat, 12.0);
        QCOMPARE(p.lenBeats, 28.0);                                                    // shrunk to fit track 1 (40 beats)
        QVERIFY(std::fabs(c_->laneStartMs(1) - 6000) < 1);                             // the incoming track slid with it
        QCOMPARE(pts(win_->mix().tracks[0].volume), pts(before.tracks[0].volume));     // curves stay where they were
        QCOMPARE(win_->undoStack()->count(), 1);                                       // one drag, one step
        win_->undoStack()->undo();
        QVERIFY(xml(win_->mix()) == xml(before));
        win_->undoStack()->redo();
        QCOMPARE(win_->mix().tracks[0].next.outStartBeat, 12.0);
    }

    void draggingTheIncomingStartSlidesItTheOtherWay()
    {
        drag(laneMid(1, c_->periodStartX(0)), laneMid(1, c_->xAt(4000 + 2000)));
        const MixPair& p = win_->mix().tracks[0].next;
        QCOMPARE(p.inStartBeat, 4.0);
        QCOMPARE(p.outStartBeat, 8.0);
        QVERIFY(std::fabs(c_->laneStartMs(1) - 2000) < 1);                             // track 2 moved 2 s earlier
        QCOMPARE(win_->undoStack()->count(), 1);
    }

    void draggingTheIncomingStartAgainContinuesFromWhereItIs()
    {
        drag(laneMid(1, c_->periodStartX(0)), laneMid(1, c_->periodStartX(0) + (c_->xAt(2000) - c_->xAt(0))));
        QCOMPARE(win_->mix().tracks[0].next.inStartBeat, 4.0);
        drag(laneMid(1, c_->periodStartX(0)), laneMid(1, c_->periodStartX(0) + (c_->xAt(2000) - c_->xAt(0))));      // a second drag starts at beat 4, not 0
        QCOMPARE(win_->mix().tracks[0].next.inStartBeat, 8.0);
        drag(laneMid(1, c_->periodStartX(0)), laneMid(1, c_->periodStartX(0) + 1));                                  // a click without moving changes nothing
        QCOMPARE(win_->mix().tracks[0].next.inStartBeat, 8.0);
    }

    void aCurveDotOnThePeriodBorderIsGrabbedFromAFewPixelsAway()
    {
        const QPoint dot = c_->pointPos(0, MixCanvas::VolumeCurve, 0);               // (beat 8, 100 %) on the period start
        const double before = win_->mix().tracks[0].next.outStartBeat;
        drag(dot + QPoint(-9, 6), dot + QPoint(-9, 40), Qt::NoModifier);             // 9 px left and 6 px below: still the dot, not the border line
        QCOMPARE(win_->mix().tracks[0].next.outStartBeat, before);
        QVERIFY(win_->mix().tracks[0].volume.points.front().value < 100);
    }

    void rightClickOnADotOffersDeletingIt()
    {
        const size_t n = win_->mix().tracks[0].volume.points.size();
        const QPoint dot = c_->pointPos(0, MixCanvas::VolumeCurve, 0);
        bool offered = false;
        QTimer::singleShot(200, [&] {
            auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!m) return;
            for (QAction* a : m->actions())
                if (a->text().startsWith(QStringLiteral("Delete"))) {
                    offered = true;
                    m->setActiveAction(a);
                    QTest::keyClick(m, Qt::Key_Return);
                    return;
                }
            m->close();
        });
        QContextMenuEvent ev(QContextMenuEvent::Mouse, dot, c_->mapToGlobal(dot));
        QApplication::sendEvent(c_, &ev);
        QVERIFY(offered);
        QCOMPARE(win_->mix().tracks[0].volume.points.size(), n - 1);
        win_->undoStack()->undo();
        QCOMPARE(win_->mix().tracks[0].volume.points.size(), n);
    }

    void draggingTheEndChangesTheLengthOfBothSides()
    {
        drag(laneMid(0, c_->periodEndX(0)), laneMid(0, c_->xAt(12000)));              // beat 24: 16 beats after the start at 8
        QCOMPARE(win_->mix().tracks[0].next.lenBeats, 16.0);
        QVERIFY(std::abs((c_->periodEndX(0) - c_->periodStartX(0)) - (c_->xAt(8000) - c_->xAt(0))) <= 1);
        // dragging on the incoming lane does the same
        drag(laneMid(1, c_->periodEndX(0)), laneMid(1, c_->periodStartX(0) + (c_->xAt(2000) - c_->xAt(0))));
        QCOMPARE(win_->mix().tracks[0].next.lenBeats, 4.0);
        // Shift snaps to beats instead of bars
        drag(laneMid(0, c_->periodEndX(0)), laneMid(0, c_->periodStartX(0) + (c_->xAt(3000) - c_->xAt(0))), Qt::ShiftModifier);
        QVERIFY(std::fabs(win_->mix().tracks[0].next.lenBeats - 6.0) < 1.01);
        QCOMPARE(win_->undoStack()->count(), 3);
    }

    void mixTypeRewritesTheCurvesAndUndoRestoresThem()
    {
        const Mix before = win_->mix();
        win_->setMixType(0, MixType::RoughCut);
        QCOMPARE(int(win_->mix().tracks[0].next.type), int(MixType::RoughCut));
        QCOMPARE(pts(win_->mix().tracks[1].volume).size() >= 3, true);
        QVERIFY(xml(win_->mix()) != xml(before));
        win_->undoStack()->undo();
        QVERIFY(xml(win_->mix()) == xml(before));
        win_->undoStack()->redo();
        QCOMPARE(int(win_->mix().tracks[0].next.type), int(MixType::RoughCut));
        // the same type again changes nothing, so no undo step
        const int n = win_->undoStack()->count();
        win_->setMixType(0, MixType::RoughCut);
        QCOMPARE(win_->undoStack()->count(), n);
    }

    void curvePointsAddMoveRemoveWithTheMouse()
    {
        c_->setFocus();
        QTest::keyClick(c_, Qt::Key_2);
        QCOMPARE(c_->curveMode(), int(MixCanvas::BassCurve));
        QTest::keyClick(c_, Qt::Key_3);
        QCOMPARE(c_->curveMode(), int(MixCanvas::HighCurve));
        QTest::keyClick(c_, Qt::Key_2);
        // track 3 has no bass curve yet: its line (100 %) runs along the top of the lane
        const QRect r = c_->laneRect(2);
        const int x10 = c_->xAt(c_->laneStartMs(2) + 5000);                          // beat 10 of track 3
        drag(QPoint(x10, r.top() + 1), QPoint(x10, r.top() + r.height() / 2));
        const Curve& cv = win_->mix().tracks[2].bass;
        QCOMPARE(cv.points.size(), size_t(1));
        QCOMPARE(cv.points[0].beat, 10.0);
        QVERIFY(std::fabs(cv.points[0].value - 50) < 4);
        QCOMPARE(win_->undoStack()->count(), 1);                                       // add + drag = one step
        QCOMPARE(c_->selectedPoint().index, 0);

        // move it to another beat with Shift (value locked)
        const double v = cv.points[0].value;
        const QPoint at = c_->pointPos(2, 1, 0);
        drag(at, QPoint(at.x() + c_->xAt(2000) - c_->xAt(0), at.y() + 20), Qt::ShiftModifier);
        QCOMPARE(win_->mix().tracks[2].bass.points[0].beat, 14.0);
        QVERIFY(std::fabs(win_->mix().tracks[2].bass.points[0].value - v) < 0.01);
        // Ctrl locks the beat
        const QPoint at2 = c_->pointPos(2, 1, 0);
        drag(at2, QPoint(at2.x() + 40, r.top() + r.height() / 4), Qt::ControlModifier);
        QCOMPARE(win_->mix().tracks[2].bass.points[0].beat, 14.0);
        QVERIFY(std::fabs(win_->mix().tracks[2].bass.points[0].value - 75) < 4);

        // a second dot dropped on the same beat makes a vertical change
        const QPoint line = QPoint(c_->xAt(c_->laneStartMs(2) + 2000), c_->pointPos(2, 1, 0).y());
        drag(line, QPoint(c_->pointPos(2, 1, 0).x(), r.top() + r.height() / 8));
        const auto p = pts(win_->mix().tracks[2].bass);
        QCOMPARE(p.size(), size_t(2));

        // double click removes a dot, Delete removes the selected one
        const int count = win_->undoStack()->count();
        QTest::mouseDClick(c_, Qt::LeftButton, Qt::NoModifier, c_->pointPos(2, 1, 0));
        QCOMPARE(win_->mix().tracks[2].bass.points.size(), size_t(1));
        const QPoint last = c_->pointPos(2, 1, 0);
        QTest::mousePress(c_, Qt::LeftButton, Qt::NoModifier, last);
        QTest::mouseRelease(c_, Qt::LeftButton, Qt::NoModifier, last);
        QTest::keyClick(c_, Qt::Key_Delete);
        QVERIFY(win_->mix().tracks[2].bass.points.empty());
        QVERIFY(win_->undoStack()->count() > count);
        win_->undoStack()->undo();
        QCOMPARE(win_->mix().tracks[2].bass.points.size(), size_t(1));
    }

    void trimIsClampedAndUndoable()
    {
        const std::string before = xml(win_->mix());
        win_->setTrim(1, 150);
        QCOMPARE(win_->mix().tracks[1].trim, 150.0);
        win_->setTrim(1, 500);
        QCOMPARE(win_->mix().tracks[1].trim, 200.0);
        win_->setTrim(1, -5);
        QCOMPARE(win_->mix().tracks[1].trim, 0.0);
        QCOMPARE(win_->undoStack()->count(), 1);                                       // consecutive trim edits of one track merge
        win_->undoStack()->undo();
        QVERIFY(xml(win_->mix()) == before);
        // the dock follows the selected track
        QTest::mouseClick(c_, Qt::LeftButton, Qt::NoModifier, QPoint(c_->xAt(c_->laneStartMs(1) + 1000), c_->laneRect(1).top() + 6));
        QCOMPARE(win_->selectedLane(), 1);
        QCOMPARE(win_->trimSpin()->value(), 100.0);
    }

    void eqAndFilterEditsAreUndoable()
    {
        MixFiltersDialog* d = win_->filtersDialog();
        const Mix before = win_->mix();
        d->bassSpin()->setValue(150);
        emit d->bassSpin()->editingFinished();
        QCOMPARE(win_->mix().bassHz, 150.0);
        d->slopeCombo()->setCurrentIndex(2);
        emit d->slopeCombo()->activated(2);
        QCOMPARE(win_->mix().slopeDb, 48);
        QCOMPARE(d->filterList()->count(), 1);                                         // the built-in compressor
        d->filterList()->item(0)->setCheckState(Qt::Unchecked);
        QVERIFY(!win_->mix().filters[0].on);
        QTest::qWait(20);
        win_->edit("add", [](Mix& m) { MixFilter f; f.kind = MixFilterKind::Ladspa; f.plugin = "x.so|y"; m.filters.push_back(f); });
        QTest::qWait(20);                                                              // the dialog refreshes after the edit has finished
        QCOMPARE(d->filterList()->count(), 2);
        d->filterList()->setCurrentRow(1);
        d->moveCurrent(-1);
        QTest::qWait(20);
        QCOMPARE(int(win_->mix().filters[0].kind), int(MixFilterKind::Ladspa));
        d->removeCurrent();
        QTest::qWait(20);
        QCOMPARE(win_->mix().filters.size(), size_t(1));
        while (win_->undoStack()->canUndo()) win_->undoStack()->undo();
        QVERIFY(xml(win_->mix()) == xml(before));
    }

    void gridNudgeMovesTheEffectiveGridAndNeverTheTrackFile()
    {
        const fs::path grid = fs::path(win_->mix().tracks[0].file).parent_path() / "beatgrid.txt";
        std::string bytes;
        readFile(grid, &bytes);
        const long long first = c_->effective(0).grid.firstBeatUs;
        win_->nudgeGrid(0, 1.0);
        win_->nudgeGrid(0, 1.0);
        QCOMPARE(win_->mix().tracks[0].gridNudgeMs, 2.0);
        QVERIFY(std::llabs(c_->effective(0).grid.firstBeatUs - first - 2000LL) <= 25);      // quantised to frames
        std::string after;
        readFile(grid, &after);
        QVERIFY(bytes == after);
        win_->nudgeGrid(0, 0);
        QCOMPARE(win_->mix().tracks[0].gridNudgeMs, 0.0);
        // positions are in beats: the curve points stay on their beats
        QCOMPARE(pts(win_->mix().tracks[0].volume), (Pts{{8, 100}, {40, 0}}));
    }

    void masterTempoRecomputesRatiosAndWarnsAboutDifferentTempos()
    {
        win_->setMasterBpm(130);
        QCOMPARE(win_->bpmSpin()->value(), 130.0);
        QVERIFY(std::fabs(c_->effective(0).ratio - 130.0 / 120) < 1e-9);
        QVERIFY(std::fabs(c_->laneStartMs(1) - 8 * 60000.0 / 130) < 1);               // periods stay on their beats
        QVERIFY(!pairTempoDiffers({c_->effective(0), c_->effective(1)}, 0));
        win_->edit("off", [](Mix& m) { m.tracks[1].stretchOff = true; });
        QVERIFY(pairTempoDiffers({c_->effective(0), c_->effective(1)}, 0));            // 130 against 120
        QCOMPARE(win_->undoStack()->count(), 2);
        win_->undoStack()->undo();
        win_->undoStack()->undo();
        QCOMPARE(win_->mix().masterBpm, 120.0);
        QCOMPARE(win_->bpmSpin()->value(), 120.0);
    }

    void stretchDockEditsTheEngineAndTheOverrides()
    {
        MixStretchDock* dock = win_->stretchDock();
        QVERIFY(dock->defaultCombo()->count() >= 6);
        const int wsola = dock->defaultCombo()->findData(QStringLiteral("wsola"));
        QVERIFY(wsola >= 0);
        dock->defaultCombo()->setCurrentIndex(wsola);
        emit dock->defaultCombo()->activated(wsola);
        QCOMPARE(win_->mix().stretch.engine, std::string("wsola"));
        QVERIFY(win_->mix().stretch.options.count("window"));                          // options built from the engine's schema
        QTest::mouseClick(c_, Qt::LeftButton, Qt::NoModifier, QPoint(c_->xAt(c_->laneStartMs(1) + 1000), c_->laneRect(1).top() + 6));
        const int off = dock->trackCombo()->findData(QStringLiteral("!off"));
        dock->trackCombo()->setCurrentIndex(off);
        emit dock->trackCombo()->activated(off);
        QVERIFY(win_->mix().tracks[1].stretchOff);
        win_->undoStack()->undo();
        QVERIFY(!win_->mix().tracks[1].stretchOff);
    }

    void fitPeriodCentresTheNearestPeriodWithEightBarsEachSide()
    {
        c_->fitPeriod(1);
        // period 2: starts at beat 8 of track 2 = 4 s + 4 s, 32 beats = 16 s; 8 bars = 16 s each side
        const double start = c_->laneStartMs(1) + 4000, len = 16000;
        QVERIFY(std::fabs(c_->spanMs() - std::min(len + 32000, std::max(20000.0, c_->mixLengthMs()))) < 1);      // never wider than the (short) test mix
        QVERIFY(std::fabs(c_->viewStartMs() + c_->spanMs() / 2 - (start + len / 2)) < 1 || c_->viewStartMs() == 0 || c_->viewStartMs() + c_->spanMs() >= c_->mixLengthMs() - 1);
        // nearest to the play position when no pair is given
        c_->setPlayPosition(1000);
        c_->fitPeriod();
        QVERIFY(c_->viewStartMs() <= 4000);
        c_->fitNextPeriod(1);
        QVERIFY(c_->viewStartMs() > 0 || c_->spanMs() >= c_->mixLengthMs() - 1);
        QCOMPARE(win_->undoStack()->count(), 0);
    }

    void aRangeBecomesACutThroughThePanelAndUndoRestores()
    {
        const Mix before = win_->mix();
        QTest::mouseClick(c_, Qt::LeftButton, Qt::NoModifier, QPoint(c_->xAt(2000), c_->laneRect(0).top() + 20));
        QCOMPARE(win_->selectedLane(), 0);
        QVERIFY(win_->regionPanel()->isEnabled());
        const int plain = c_->editedColumns(0).size();
        c_->setRegionMode(true);
        drag(laneMid(0, c_->xAt(8000)), laneMid(0, c_->xAt(16000)));                      // beats 16 .. 32 of track 1
        QVERIFY(win_->regionPanel()->hasRange());
        QVERIFY(c_->regionMode());
        QVERIFY(win_->regionPanel()->addCutFromRange());
        QCOMPARE(win_->mix().tracks[0].regions.size(), size_t(1));
        QCOMPARE(win_->mix().tracks[0].regions[0].startBeat, 16);
        QCOMPARE(win_->mix().tracks[0].regions[0].lenBeats, 16);
        QCOMPARE(c_->editedColumns(0).size(), plain - 8 * 150);                         // the lane got shorter
        QCOMPARE(win_->regionPanel()->regionList()->count(), 1);
        QCOMPARE(win_->mix().tracks[0].next.lenBeats, 16.0);                           // the period was kept inside the shorter track
        QVERIFY(win_->windowTitle().contains('*'));
        QCOMPARE(win_->undoStack()->count(), 1);
        win_->undoStack()->undo();
        QVERIFY(xml(win_->mix()) == xml(before));
        QCOMPARE(c_->editedColumns(0).size(), plain);
        QCOMPARE(win_->regionPanel()->regionList()->count(), 0);
        QVERIFY(!win_->windowTitle().contains('*'));
        c_->setRegionMode(false);
    }

    void saveReopenSaveAsAndTheTreeNeverTouchTheTracks()
    {
        const auto tracksBefore = snapshot(w_->root / "track-master-copy");
        int asked = 0;
        win_->prompts.saveDetails = [&](QString* n, QString* a, QString*) { ++asked; *n = "Friday"; *a = "Alice"; return true; };
        win_->prompts.notify = [](const QString&) {};
        win_->setTrim(1, 120);
        QVERIFY(win_->windowTitle().contains('*'));
        QVERIFY(win_->save());
        QCOMPARE(asked, 1);
        const fs::path file = w_->lib.mixesDir() / "Friday.mix";
        QVERIFY(fs::exists(file));
        QVERIFY(!win_->windowTitle().contains('*'));
        QCOMPARE(win_->mix().author, std::string("Alice"));
        win_->setTrim(1, 130);
        QVERIFY(win_->windowTitle().contains('*'));
        QVERIFY(win_->save());                                                          // later saves are silent
        QCOMPARE(asked, 1);

        // reopen in another window: the same model
        StudioMixWindow other([this]() -> const Library* { return &w_->lib; }, nullptr);
        other.prompts.notify = [](const QString&) {};
        QVERIFY(other.openMixFile(QString::fromStdString(file.string())));
        QVERIFY(xml(other.mix()) == xml(win_->mix()));
        QCOMPARE(other.canvas()->laneCount(), 3);
        QCOMPARE(other.mix().tracks[1].trim, 130.0);
        QVERIFY(!other.windowTitle().contains('*'));

        // Save As into a folder
        win_->prompts.saveDetails = [&](QString* n, QString* a, QString*) { ++asked; *n = "Sets/Warm"; *a = "Bob"; return true; };
        QVERIFY(win_->saveAs());
        QVERIFY(fs::exists(w_->lib.mixesDir() / "Sets" / "Warm.mix"));
        QCOMPARE(win_->mix().name, std::string("Warm"));
        win_->prompts.saveDetails = [&](QString* n, QString*, QString*) { *n = "../evil"; return true; };
        QVERIFY(!win_->saveAs());
        QVERIFY(!fs::exists(w_->root / "evil.mix"));

        // the tree lists the nested folders and opens a mix on double click
        QTreeWidget* tree = win_->treeDock()->tree();
        QCOMPARE(tree->topLevelItemCount(), 2);                                         // Sets/ and Friday
        QCOMPARE(tree->topLevelItem(0)->text(0), QString("Sets"));
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QString("Warm"));
        QSignalSpy spy(win_->treeDock(), &MixTreeDock::openRequested);
        emit tree->itemDoubleClicked(tree->topLevelItem(1), 0);
        QCOMPARE(spy.count(), 1);
        QVERIFY(win_->openMixFile(spy.at(0).at(0).toString()));
        QCOMPARE(win_->mix().name, std::string("Friday"));

        // rename, folders and delete stay inside mixes/
        MixTreeDock* dock = win_->treeDock();
        dock->setAskYesNo([](const QString&, const QString&) { return true; });
        QVERIFY(dock->renameMix(QString::fromStdString(file.string()), "Saturday"));
        QVERIFY(fs::exists(w_->lib.mixesDir() / "Saturday.mix") && !fs::exists(file));
        QVERIFY(!dock->renameMix(QString::fromStdString((w_->lib.mixesDir() / "Saturday.mix").string()), "../x"));
        QVERIFY(!dock->renameMix(QString::fromStdString(w_->lib.trackDir("House", "T0").string()), "x"));
        QVERIFY(!dock->deleteEntry(QString::fromStdString(w_->lib.trackDir("House", "T0").string())));
        QVERIFY(!dock->deleteEntry(QString::fromStdString(w_->root.string())));
        QVERIFY(dock->newFolder("", "Archive"));
        QVERIFY(fs::is_directory(w_->lib.mixesDir() / "Archive"));
        QVERIFY(!dock->newFolder("", "../up"));
        QVERIFY(dock->deleteEntry(QString::fromStdString((w_->lib.mixesDir() / "Archive").string())));
        QVERIFY(!fs::exists(w_->lib.mixesDir() / "Archive"));
        QVERIFY(dock->deleteEntry(QString::fromStdString((w_->lib.mixesDir() / "Saturday.mix").string())));
        QVERIFY(snapshot(w_->root / "track-master-copy") == tracksBefore);              // not one track file changed
    }

    void openingAMixWithAMissingTrackShowsItAsMissing()
    {
        win_->prompts.saveDetails = [](QString* n, QString*, QString*) { *n = "Gappy"; return true; };
        QVERIFY(win_->save());
        fs::remove(w_->lib.trackDir("House", "T1") / "track.wav");
        QString said;
        StudioMixWindow other([this]() -> const Library* { return &w_->lib; }, nullptr);
        other.prompts.notify = [&](const QString& t) { said = t; };
        QVERIFY(other.openMixFile(QString::fromStdString((w_->lib.mixesDir() / "Gappy.mix").string())));
        QVERIFY(other.mix().tracks[1].missing);
        QVERIFY(!other.mix().tracks[0].missing);
        QCOMPARE(other.canvas()->laneCount(), 3);
        QVERIFY(said.contains("missing") && said.contains("track 2"));
        QVERIFY(!other.windowTitle().contains('*'));
    }

    void unsavedChangesAreAskedAbout()
    {
        win_->prompts.notify = [](const QString&) {};
        win_->prompts.saveDetails = [](QString* n, QString*, QString*) { *n = "Asked"; return true; };
        QVERIFY(win_->maybeSave());                                                     // nothing to save
        win_->setTrim(0, 50);
        win_->prompts.askUnsaved = [](const QString&) { return 2; };
        QVERIFY(!win_->maybeSave());
        QVERIFY(!win_->close());                                                        // Cancel keeps the window
        QVERIFY(win_->isVisible());
        win_->prompts.askUnsaved = [](const QString&) { return 1; };
        QVERIFY(win_->maybeSave());                                                     // Discard
        QVERIFY(!fs::exists(w_->lib.mixesDir() / "Asked.mix"));
        win_->prompts.askUnsaved = [](const QString&) { return 0; };
        QVERIFY(win_->maybeSave());                                                     // Save
        QVERIFY(fs::exists(w_->lib.mixesDir() / "Asked.mix"));
        QVERIFY(!win_->isModified());
    }

    void unrelatedClickStillSetsThePlayPosition()
    {
        QSignalSpy spy(c_, &MixCanvas::playPositionChanged);
        QTest::mouseClick(c_, Qt::LeftButton, Qt::NoModifier, QPoint(c_->xAt(2000), 5));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(win_->undoStack()->count(), 0);                                        // looking is not editing
    }
};

QTEST_MAIN(TestMixEdit)
#include "test_trakcrate_mix_edit.moc"
