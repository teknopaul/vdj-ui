#include <QtTest/QtTest>

#include <QApplication>
#include <QListWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollBar>

#include "config/config_manager.h"
#include "trakcrate/analysis_wave.h"
#include "trakcrate/edit_output.h"
#include "trakcrate/library.h"
#include "trakcrate/stretch_probe.h"
#include "ui/studio_mix_window.h"

#include <fstream>

using namespace trakcrate;
using namespace vdj;

namespace {

struct World {
    fs::path root = makeTempDir("mixui");
    Library lib;
    World(int tracks, int unanalysed = 0, double seconds = 20)
    {
        Library::create(root, nullptr);
        Library::open(root, &lib, nullptr);
        lib.createPlaylist("Set", nullptr);
        for (int i = 0; i < tracks; ++i) {
            const fs::path dir = lib.createTrackDir("House", "T" + std::to_string(i), nullptr);
            const DecodedPcm p = probeMaterial(seconds, 120);
            AudioTags tags;
            writeWav(dir / "track.wav", p, 120, tags, nullptr);
            KvDoc g;
            BeatGrid grid;
            grid.bpm = 120;
            setBeatGrid(g, grid);
            std::ofstream(dir / "beatgrid.txt") << g.serialise();
            KvDoc m;
            m.set("title", "Track " + std::to_string(i + 1));
            std::ofstream(dir / "meta.txt") << m.serialise();
            if (i >= unanalysed) std::ofstream(dir / "waveform.bin", std::ios::binary) << serialiseWaveform(analyseWaveform(p));
            lib.addToPlaylist("Set", dir, -1, nullptr);
        }
    }
    ~World() { std::error_code ec; fs::remove_all(root, ec); }
    StudioMixWindow::LibraryProvider provider() { return [this]() -> const Library* { return &lib; }; }
};

std::unique_ptr<StudioMixWindow> windowWith(World& w, ConfigManager* cfg = nullptr)
{
    auto win = std::make_unique<StudioMixWindow>(w.provider(), cfg);
    NewMixDialog dlg(&w.lib);
    dlg.selectPlaylist("Set");
    win->showPlaylistMix("Set", dlg.tracks());
    return win;
}

} // namespace

class TestMixUi : public QObject
{
    Q_OBJECT
private slots:
    void windowIsATopLevelWindowWithMinMaxButtons()
    {
        StudioMixWindow win([]() -> const Library* { return nullptr; }, nullptr);
        QCOMPARE(win.windowType(), Qt::Window);
        QVERIFY(win.windowFlags() & Qt::WindowMinMaxButtonsHint);
        QVERIFY(win.windowFlags() & Qt::WindowMaximizeButtonHint);
        QVERIFY(!(win.windowFlags() & Qt::Dialog & ~Qt::Window));
        QVERIFY(win.menuBar()->actions().size() >= 3);
    }

    void playlistDialogLeavesOutUnanalysedTracksAndSaysWhy()
    {
        World w(3, 1);                                  // track 0 has no waveform
        NewMixDialog dlg(&w.lib);
        dlg.selectPlaylist("Set");
        QCOMPARE(dlg.playlist(), QString("Set"));
        QCOMPARE(dlg.trackList()->count(), 3);
        QCOMPARE(dlg.unanalysedCount(), 1);
        QVERIFY(dlg.trackList()->item(0)->text().contains("not analysed"));
        QCOMPARE(dlg.trackList()->item(0)->foreground().color(), QColor(220, 50, 50));
        QVERIFY(dlg.analyseButton()->isEnabled());
        QCOMPARE(dlg.tracks().size(), size_t(2));
        QVERIFY(dlg.tracks()[0].file.string().find("T1") != std::string::npos);
    }

    void aPlaylistWithoutAnalysedTracksIsRefused()
    {
        World w(2, 2);
        NewMixDialog dlg(&w.lib);
        dlg.selectPlaylist("Set");
        QVERIFY(dlg.tracks().empty());
        QCOMPARE(dlg.unanalysedCount(), 2);
    }

    void threeTrackPlaylistMakesThreeLanesInOrder()
    {
        World w(3);
        auto win = windowWith(w);
        MixCanvas* c = win->canvas();
        QCOMPARE(c->laneCount(), 3);
        QCOMPARE(c->laneStartMs(0), 0.0);
        QVERIFY(std::fabs(c->laneStartMs(1) - 4000) < 1);       // 40 beats: the period starts at beat 8 = 4 s
        QVERIFY(std::fabs(c->laneStartMs(2) - 8000) < 1);
        QVERIFY(std::fabs(c->mixLengthMs() - 28000) < 2);
        QCOMPARE(win->mix().playlist, std::string("Set"));
        QVERIFY(win->windowTitle().contains("Set"));
        QVERIFY(!c->editedColumns(0).isEmpty());
    }

    void rulerLabelsAndTickSpacing()
    {
        QCOMPARE(MixCanvas::formatTime(120.0 * 60 * 1000), QString("120:00"));
        QCOMPARE(MixCanvas::formatTime(65000), QString("1:05"));
        QCOMPARE(MixCanvas::formatTime(0), QString("0:00"));
        QVERIFY(MixCanvas::tickStepMs(100) >= 7000);             // 100 ms per pixel: ticks every 10 s or more
        QVERIFY(MixCanvas::tickStepMs(1) <= 2000);
    }

    void laneHeightsAreFiftyHundredTwoHundred()
    {
        World w(3);
        auto win = windowWith(w);
        for (int h : {50, 100, 200}) {
            win->canvas()->setLaneHeight(h);
            QCOMPARE(win->canvas()->laneRect(0).height(), h);
            QCOMPARE(win->canvas()->laneRect(1).top() - win->canvas()->laneRect(0).bottom() - 1, MixCanvas::kLaneGap);
        }
    }

    void zoomChangesTheSpanAndClamps()
    {
        World w(3);
        auto win = windowWith(w);
        MixCanvas* c = win->canvas();
        win->resize(900, 500);
        c->zoomFit();
        const double full = c->spanMs();
        QVERIFY(full >= 28000 - 1);
        c->zoomAt(10000, 0.8);
        QVERIFY(std::fabs(c->spanMs() - full * 0.8) < 1);
        c->zoomAt(10000, 0.0001);
        QCOMPARE(c->spanMs(), MixCanvas::kMinSpanMs);            // never closer than 20 s
        c->zoomAt(10000, 1000);
        QVERIFY(std::fabs(c->spanMs() - full) < 1);              // never wider than the mix
        // the pointer position stays put while zooming
        c->zoomFit();
        c->setView(0, 20000);
        const double before = c->viewStartMs() + 0.5 * c->spanMs();
        c->zoomAt(before, 0.9);
        QVERIFY(std::fabs(c->viewStartMs() + 0.5 * c->spanMs() - before) < 5);
    }

    void aClickSetsThePlayPosition()
    {
        World w(3);
        auto win = windowWith(w);
        win->resize(900, 500);
        win->show();
        MixCanvas* c = win->canvas();
        QSignalSpy spy(c, &MixCanvas::playPositionChanged);
        const int x = c->xAt(12000);
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, QPoint(x, c->laneRect(1).top() + 8));      // away from the curve line, which would add a point
        QCOMPARE(spy.count(), 1);
        QVERIFY(std::fabs(c->playPosition() - 12000) < c->spanMs() / c->width() * 2);
        QCOMPARE(c->selectedLane(), 1);
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, QPoint(c->xAt(3000), 5));      // the ruler
        QVERIFY(std::fabs(c->playPosition() - 3000) < c->spanMs() / c->width() * 2);
        QCOMPARE(c->trackAt(5), -1);
    }

    void scrollingVerticallyShowsLaterLanes()
    {
        World w(8, 0, 12);
        auto win = windowWith(w);
        win->resize(900, 420);
        win->show();
        MixCanvas* c = win->canvas();
        c->setLaneHeight(200);
        QVERIFY(c->verticalScrollBar()->isVisible());
        QVERIFY(c->laneRect(7).top() > c->height());
        c->verticalScrollBar()->setValue(c->verticalScrollBar()->maximum());
        QVERIFY(c->laneRect(7).bottom() <= c->height());
        QCOMPARE(c->trackAt(c->laneRect(7).center().y()), 7);
        QVERIFY(c->laneRect(0).bottom() < MixCanvas::kRulerHeight);   // scrolled away
    }

    void editedWaveformOfATrackWithACutIsShorterByTheCut()
    {
        World w(3);
        auto win = windowWith(w);
        MixCanvas* c = win->canvas();
        const int plain = c->editedColumns(0).size();
        QCOMPARE(plain, 20 * 150);
        // a cut of 4 bars (16 beats = 8 s) in a copy of the model
        auto* mix = const_cast<Mix*>(&win->mix());
        EditRegion cut;
        cut.startBeat = 8;
        cut.lenBeats = 16;
        mix->tracks[0].regions.push_back(cut);
        c->refresh();
        QCOMPARE(c->editedColumns(0).size(), plain - 8 * 150);
        QCOMPARE(c->laneStartMs(1), 4000.0);                     // the pair is in beats of the edited track, so it does not move
        QCOMPARE(c->editedColumns(1).size(), plain);
    }

    void stretchedWaveformScalesWithTheRatio()
    {
        World w(2);
        auto win = windowWith(w);
        auto* mix = const_cast<Mix*>(&win->mix());
        mix->masterBpm = 150;                                    // ratio 1.25: 20 s -> 16 s
        win->canvas()->refresh();
        QCOMPARE(win->canvas()->editedColumns(0).size(), 16 * 150);
    }

    void grabChangesWhenATrackIsAdded()
    {
        World w(3);
        auto win = windowWith(w);
        win->resize(900, 500);
        win->show();
        QImage a = win->canvas()->grab().toImage();
        if (qEnvironmentVariableIsSet("MIX_UI_SNAPSHOT")) win->grab().save(qEnvironmentVariable("MIX_UI_SNAPSHOT"));      // to look at by eye
        NewMixDialog dlg(&w.lib);
        dlg.selectPlaylist("Set");
        auto tracks = dlg.tracks();
        tracks.pop_back();
        win->showPlaylistMix("Set", tracks);
        QImage b = win->canvas()->grab().toImage();
        QVERIFY(a != b);
    }

    void settingsRememberTheLaneHeight()
    {
        World w(2);
        ConfigManager cfg;
        cfg.saveValue("studiomix", "laneHeight", "200");
        auto win = windowWith(w, &cfg);
        QCOMPARE(win->canvas()->laneHeight(), 200);
        cfg.saveValue("studiomix", "laneHeight", "100");
    }
};

QTEST_MAIN(TestMixUi)
#include "test_trakcrate_mix_ui.moc"
