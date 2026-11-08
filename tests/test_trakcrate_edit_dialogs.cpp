#include <QtTest/QtTest>

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QUndoStack>

#include "config/config_manager.h"
#include "trakcrate_wav.h"
#include "trakcrate/grid_edit.h"
#include "trakcrate/importer.h"
#include "ui/edit_prefs.h"
#include "audio/wav_info.h"
#include "trakcrate/audio_probe.h"
#include "ui/trakcrate_edit_loop_dialog.h"
#include "ui/trakcrate_edit_panel.h"
#include "ui/trakcrate_edit_previewer.h"
#include "ui/trakcrate_edit_render_dialog.h"

namespace tc = trakcrate;
using namespace vdj;

class TestEditDialogs : public QObject
{
    Q_OBJECT

    tc::fs::path tmp_;
    tc::Library lib_;
    tc::fs::path a_, b_;
    tc::EditRequest req_;

    static std::string slurp(const tc::fs::path& p) { std::string s; tc::readFile(p, &s); return s; }
    static std::map<std::string, std::string> snapshot(const tc::fs::path& dir)
    {
        std::map<std::string, std::string> m;
        for (const auto& e : tc::fs::recursive_directory_iterator(dir))
            if (e.is_regular_file()) m[e.path().string()] = slurp(e.path());
        return m;
    }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void init()
    {
        tmp_ = tc::makeTempDir("edit-dialogs");
        QVERIFY(tc::Library::create(tmp_ / "lib") && tc::Library::open(tmp_ / "lib", &lib_));
        for (const char* n : {"a", "b"}) {
            tc::DecodedPcm p = tcwav::clicks(120, 0.25, n[0] == 'a' ? 40 : 41);          // different audio, or the importer sees a duplicate
            tcwav::writeWavMono(tmp_ / "src" / (std::string(n) + ".wav"), tcwav::mono(p), p.sampleRate);
        }
        QVERIFY(tc::importFolder(lib_, tmp_ / "src", tc::ImportOptions()).ok);
        const auto order = lib_.playlistTracks("src");
        QCOMPARE(int(order.size()), 2);
        a_ = order[0];
        b_ = order[1];
        tc::BeatGrid g;
        g.bpm = 120;
        tc::Track t;
        QVERIFY(tc::loadTrack(b_, &t));
        QVERIFY(tc::saveGridEdit(&t, g, nullptr, "edited"));
        req_.trackDir = b_;
        req_.grid = g;
        tc::EditRegion cut;
        cut.startBeat = 8;
        cut.lenBeats = 8;
        cut.algo = tc::EditAlgo::Cross;
        cut.algoParam = 1;
        tc::EditRegion loop;
        loop.kind = tc::EditKind::Loop;
        loop.startBeat = 24;
        loop.lenBeats = 4;
        loop.loopTimes = 3;
        req_.regions = {cut, loop};
        ConfigManager cm;
        cm.clearSection(QStringLiteral("edit"));
    }
    void cleanup()
    {
        std::error_code ec;
        tc::fs::remove_all(tmp_, ec);
    }

    void ensureRenderedCachesAndLengthIsRight()
    {
        tc::EditRenderCache cache;
        std::string err;
        QVERIFY2(tc::ensureRendered(&cache, req_, &err), err.c_str());
        QCOMPARE((long long)cache.result->frames(), (long long)cache.source->frames() - 4 * 22050 + 2 * 2 * 22050);       // 4 s cut, 2 repeats of 2 s
        const auto first = cache.result;
        QVERIFY(tc::ensureRendered(&cache, req_, &err));
        QVERIFY(cache.result == first);                                      // same request: nothing rendered again
        req_.regions.pop_back();
        QVERIFY(tc::ensureRendered(&cache, req_, &err));
        QVERIFY(cache.result != first);
        QCOMPARE(cache.outGrid.bpm, 120.0);
        QVERIFY(!tc::joinOutputMs(cache.plan).empty());
        req_.regions.clear();
        QVERIFY(!tc::ensureRendered(&cache, req_, &err));
    }

    void fileOnlyOutputAndNumbering()
    {
        const auto before = snapshot(b_);
        ConfigManager cm;
        tc::EditRenderCache cache;
        const tc::fs::path out = tmp_ / "out";
        tc::fs::create_directories(out);
        for (int round = 0; round < 2; ++round) {
            EditRenderDialog dlg(req_, &cache, &lib_, &cm);
            QVERIFY(dlg.libraryRadio()->isChecked());                          // the default: into the library
            QVERIFY(dlg.targetLabel()->text().contains(QStringLiteral("playlist src")));
            dlg.fileRadio()->setChecked(true);
            dlg.folderEdit()->setText(QString::fromStdString(out.string()));
            dlg.authorEdit()->setText(QStringLiteral("Tester"));
            dlg.formatBox()->setCurrentIndex(0);
            QCOMPARE(dlg.nameLabel()->text(), round == 0 ? QStringLiteral("b_tester_edit.wav") : QStringLiteral("b_tester_edit_2.wav"));
            QSignalSpy created(&dlg, &EditRenderDialog::created);
            dlg.startRender();
            QVERIFY(dlg.isBusy());
            QVERIFY(created.wait(20000));
            QVERIFY(created.first().first().toString().isEmpty());              // no track made
        }
        QVERIFY(tc::fs::exists(out / "b_tester_edit.wav"));
        QVERIFY(tc::fs::exists(out / "b_tester_edit.txt"));
        QVERIFY(tc::fs::exists(out / "b_tester_edit_2.wav"));
        QVERIFY(slurp(out / "b_tester_edit.txt").find("#1 cut bars 3-4 algo=cross 1 beat") != std::string::npos);
        QCOMPARE(snapshot(b_), before);                                       // the original track is untouched
        // remembered
        EditPrefs prefs(&cm);
        QCOMPARE(prefs.author(), QStringLiteral("Tester"));
        QCOMPARE(prefs.format(), QStringLiteral("wav"));
        QCOMPARE(prefs.folder(), QString::fromStdString(out.string()));
        EditRenderDialog again(req_, &cache, &lib_, &cm);
        QCOMPARE(again.authorEdit()->text(), QStringLiteral("Tester"));
        QCOMPARE(again.folderEdit()->text(), QString::fromStdString(out.string()));
    }

    void mp3FormatIsRememberedWithTheBitrate()
    {
        ConfigManager cm;
        tc::EditRenderCache cache;
        const tc::fs::path out = tmp_ / "out";
        tc::fs::create_directories(out);
        {
            EditRenderDialog dlg(req_, &cache, nullptr, &cm);                   // no library: only files
            QVERIFY(!dlg.libraryRadio()->isEnabled());
            QVERIFY(dlg.fileRadio()->isChecked());
            QVERIFY(!dlg.bitrateBox()->isEnabled());
            dlg.formatBox()->setCurrentIndex(1);
            QVERIFY(dlg.bitrateBox()->isEnabled());
            dlg.bitrateBox()->setCurrentIndex(dlg.bitrateBox()->findData(192));
            dlg.folderEdit()->setText(QString::fromStdString(out.string()));
            QSignalSpy created(&dlg, &EditRenderDialog::created);
            dlg.startRender();
            QVERIFY(created.wait(30000));
        }
        EditPrefs prefs(&cm);
        QCOMPARE(prefs.format(), QStringLiteral("mp3"));
        QCOMPARE(prefs.mp3Kbps(), 192);
        EditRenderDialog again(req_, &cache, nullptr, &cm);
        QCOMPARE(again.formatBox()->currentData().toString(), QStringLiteral("mp3"));
        QCOMPARE(again.bitrateBox()->currentData().toInt(), 192);
    }

    void libraryOutputIsAnalysedAndPlacedBeforeTheOriginal()
    {
        const auto before = snapshot(b_);
        ConfigManager cm;
        tc::EditRenderCache cache;
        EditRenderDialog dlg(req_, &cache, &lib_, &cm);
        dlg.authorEdit()->setText(QStringLiteral("Tester"));
        QSignalSpy created(&dlg, &EditRenderDialog::created);
        dlg.startRender();
        QVERIFY(created.wait(20000));
        const QString dir = created.first().first().toString();
        QVERIFY(!dir.isEmpty());
        QVERIFY(created.first().at(1).toString().contains(QStringLiteral("playlist src")));
        const tc::fs::path nd = dir.toStdString();
        QCOMPARE(nd.parent_path().filename().string(), std::string("src"));
        QVERIFY(tc::fs::exists(nd / "waveform.bin"));
        tc::Track t;
        QVERIFY(tc::loadTrack(nd, &t));
        QVERIFY(t.hasGrid);
        QCOMPARE(tc::beatGridFrom(t.grid).bpm, 120.0);
        QCOMPARE(t.title().empty(), false);
        const auto order = lib_.playlistTracks("src");
        QCOMPARE(int(order.size()), 3);
        QCOMPARE(order[1].filename().string(), nd.filename().string());         // a, edit, b
        QCOMPARE(order[2].filename().string(), b_.filename().string());
        QCOMPARE(snapshot(b_), before);
        QVERIFY(dlg.result() == QDialog::Accepted);
    }

    void aFailureKeepsTheDialogOpen()
    {
        ConfigManager cm;
        tc::EditRenderCache cache;
        EditRenderDialog dlg(req_, &cache, &lib_, &cm);
        dlg.fileRadio()->setChecked(true);
        dlg.folderEdit()->setText(QString::fromStdString((tmp_ / "does-not-exist").string()));
        QSignalSpy failed(&dlg, &EditRenderDialog::failed);
        dlg.startRender();
        QVERIFY(failed.wait(20000));
        QVERIFY(dlg.resultLabel()->text().contains(QStringLiteral("folder")));
        QVERIFY(dlg.result() != QDialog::Accepted);
        QVERIFY(!dlg.isBusy());
    }

    void theRenderDialogReusesWhatThePreviewMade()
    {
        tc::EditRenderCache cache;
        EditPreviewer pv(&cache);
        QSignalSpy state(&pv, &EditPreviewer::stateChanged);
        pv.start(req_);
        QTRY_VERIFY_WITH_TIMEOUT(pv.isPlaying(), 20000);
        QVERIFY(std::fabs(pv.player()->lengthMs() - double(cache.result->frames()) * 1000.0 / 44100) < 2.0);
        pv.stop();
        QVERIFY(!pv.isPlaying());
        const auto result = cache.result;
        EditRenderDialog rd(req_, &cache, &lib_, nullptr);
        QSignalSpy created(&rd, &EditRenderDialog::created);
        rd.startRender();
        QVERIFY(created.wait(20000));
        QVERIFY(cache.result == result);
    }

    void loopSectionComesFromTheSelectionOrTheSelectedLoop()
    {
        QUndoStack undo;
        TrakcrateEditPanel p(&undo);
        p.setTrack(req_.grid, 40000);
        int a = -1, n = -1;
        QVERIFY(!p.loopSection(&a, &n));
        QVERIFY(!p.loopExportButton()->isEnabled());
        p.setRange(5200, 9000);                                                  // nearest beat 10 (5000 ms) ...
        QVERIFY(p.loopSection(&a, &n));
        QCOMPARE(a, 10);
        QCOMPARE(n, 4);                                                          // ... and 4 beats, the prime use
        QVERIFY(p.loopExportButton()->isEnabled());
        QSignalSpy spy(&p, &TrakcrateEditPanel::loopExportRequested);
        p.loopExportButton()->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 10);
        p.setRange(8000, 16000);
        p.addLoopFromRange();                                                    // a loop region wins over a range
        QVERIFY(p.loopSection(&a, &n));
        QCOMPARE(a, 16);
        QCOMPARE(n, 16);
        p.removeRegion(0);
        QVERIFY(!p.loopSection(&a, &n));
    }

    void loopFileHasTheTempoAndTheTitle()
    {
        tc::EditRenderCache cache;
        const tc::fs::path out = tmp_ / "loops";
        tc::fs::create_directories(out);
        tc::LoopExportParams p;
        p.name = "bass";
        p.author = "Tester";
        p.startBeat = 8;
        p.lenBeats = 4;
        p.algo = tc::EditAlgo::Cross;
        p.algoParam = 0.5;
        p.folder = out;
        QCOMPARE(tc::plannedLoopFileName(b_, p), std::string("b_bass.wav"));
        const auto before = snapshot(b_);
        tc::LoopOutcome o = tc::exportLoop(&cache, b_, req_.grid, p);
        QVERIFY2(o.ok, o.error.c_str());
        QCOMPARE(o.file.filename().string(), std::string("b_bass.wav"));
        QCOMPARE(vdj::wavBpmFromFile(QString::fromStdString(o.file.string())), 120.0);
        tc::DecodedPcm pcm;
        QVERIFY(tc::decodeAudio(o.file, &pcm));
        QCOMPARE((long long)pcm.frames(), 4LL * 22050);                          // exactly 4 beats
        tc::AudioTags tags;
        QVERIFY(tc::readAudioTags(o.file, &tags));
        QCOMPARE(tags.get("title"), std::string("b (bass Tester Edit)"));
        // never overwrites
        tc::LoopOutcome again = tc::exportLoop(&cache, b_, req_.grid, p);
        QVERIFY(again.ok);
        QCOMPARE(again.file.filename().string(), std::string("b_bass_2.wav"));
        // mp3 carries the tempo as TBPM
        p.format = tc::OutFormat::Mp3;
        p.mp3Kbps = 192;
        tc::LoopOutcome mp3 = tc::exportLoop(&cache, b_, req_.grid, p);
        QVERIFY2(mp3.ok, mp3.error.c_str());
        tc::AudioInfo info;
        QVERIFY(tc::probeAudio(mp3.file, &info));
        QCOMPARE(info.tags.count("tbpm") ? info.tags["tbpm"] : info.tags["bpm"], std::string("120"));
        // refused: no grid, no folder, past the end
        QVERIFY(!tc::exportLoop(&cache, b_, tc::BeatGrid(), p).ok);
        p.folder = tmp_ / "nope";
        QVERIFY(!tc::exportLoop(&cache, b_, req_.grid, p).ok);
        p.folder = out;
        p.startBeat = 200;
        QVERIFY(!tc::exportLoop(&cache, b_, req_.grid, p).ok);
        QCOMPARE(snapshot(b_), before);
    }

    void loopDialogRemembersTheFolderAndLeavesTheLibraryAlone()
    {
        ConfigManager cm;
        EditPrefs(&cm).setAuthor(QStringLiteral("Tester"));
        tc::EditRenderCache cache;
        const tc::fs::path out = tmp_ / "loops";
        tc::fs::create_directories(out);
        const auto tracksBefore = lib_.playlistTracks("src").size();
        {
            EditLoopDialog dlg(b_, req_.grid, 8, 4, &cache, &cm);
            QCOMPARE(dlg.lengthBox()->currentData().toInt(), 4);
            QVERIFY(dlg.folderEdit()->text() != QString::fromStdString(out.string()));
            dlg.folderEdit()->setText(QString::fromStdString(out.string()));
            dlg.nameEdit()->setText(QString());
            QVERIFY(!dlg.exportButton()->isEnabled());                           // a name is needed
            dlg.nameEdit()->setText(QStringLiteral("acid one"));
            QCOMPARE(dlg.fileLabel()->text(), QStringLiteral("b_acid one.wav"));
            QSignalSpy previewed(&dlg, &EditLoopDialog::previewReady);
            QSignalSpy failed(&dlg, &EditLoopDialog::failed);
            dlg.startPreview();
            QTRY_VERIFY_WITH_TIMEOUT(previewed.count() + failed.count() > 0, 20000);
            QCOMPARE(previewed.count(), 1);
            QVERIFY(std::fabs(dlg.player()->lengthMs() - 8 * 2000.0) < 2.0);     // 8 repeats of 2 s
            QSignalSpy exported(&dlg, &EditLoopDialog::exported);
            dlg.startExport();
            QVERIFY(exported.wait(20000));
            QVERIFY(tc::fs::exists(out / "b_acid one.wav"));
        }
        QCOMPARE(EditPrefs(&cm).loopFolder(), QString::fromStdString(out.string()));
        EditLoopDialog again(b_, req_.grid, 8, 4, &cache, &cm);
        QCOMPARE(again.folderEdit()->text(), QString::fromStdString(out.string()));          // the default from then on
        QCOMPARE(lib_.playlistTracks("src").size(), tracksBefore);               // nothing imported
        QCOMPARE(lib_.listTracks("src").size(), size_t(2));
    }


    void sourcePositionsMapIntoTheEdit()
    {
        tc::EditRenderCache cache;
        std::string err;
        QVERIFY(tc::ensureRendered(&cache, req_, &err));                        // cut beats 8-16 (4-8 s), loop beats 24-28 (12-14 s) x3
        QCOMPARE(tc::sourceToOutputMs(cache.plan, 3000), 3000.0);
        QCOMPARE(tc::sourceToOutputMs(cache.plan, 5000), 4000.0);               // inside the cut: where the audio after it starts
        QCOMPARE(tc::sourceToOutputMs(cache.plan, 9000), 5000.0);
        QCOMPARE(tc::sourceToOutputMs(cache.plan, 12500), 8500.0);              // first play of the loop
        QCOMPARE(tc::sourceToOutputMs(cache.plan, 12500, true), 12500.0);       // last play: two repeats of 2 s later
        QCOMPARE(tc::sourceToOutputMs(cache.plan, 20000, true), 20000.0 - 4000 + 4000);
        QVERIFY(tc::sourceToOutputMs(cache.plan, 1e9) > 30000);                 // past the end: the end
    }

    void outputPositionsMapBackToTheOriginal()
    {
        tc::EditRenderCache cache;
        std::string err;
        QVERIFY(tc::ensureRendered(&cache, req_, &err));                        // cut 4-8 s, loop 12-14 s x3
        QCOMPARE(tc::outputToSourceMs(cache.plan, 3000), 3000.0);
        QCOMPARE(tc::outputToSourceMs(cache.plan, 5000), 9000.0);               // after the cut
        QCOMPARE(tc::outputToSourceMs(cache.plan, 9000), 13000.0);              // inside the first play of the loop (starts at 8 s out)
        QCOMPARE(tc::outputToSourceMs(cache.plan, 10500), 12500.0);             // second play: back at the loop's start
        QVERIFY(tc::outputToSourceMs(cache.plan, 1e9) > 30000);
    }

    void previewPlaysFromTheMarkerAndMovesTheTimeLine()
    {
        tc::EditRenderCache cache;
        EditPreviewer pv(&cache);
        QSignalSpy pos(&pv, &EditPreviewer::positionChanged);
        QSignalSpy state(&pv, &EditPreviewer::stateChanged);
        pv.start(req_, 9000, 12000);                                           // from after the cut (5 s out)
        QTRY_VERIFY_WITH_TIMEOUT(pos.count() > 2, 20000);
        QVERIFY(pv.isPlaying());
        const double first = pos.first().first().toDouble();
        QVERIFY2(first > 8900 && first < 10500, qPrintable(QString::number(first)));       // positions are in the original
        QTRY_VERIFY_WITH_TIMEOUT(!pv.isPlaying(), 15000);                       // stops by itself at the end of the window
        QVERIFY(state.count() >= 2);
        QVERIFY(pos.last().first().toDouble() > first);
        // toggle: a second call while playing stops it
        pv.toggle(req_, 9000, -1);
        QTRY_VERIFY_WITH_TIMEOUT(pv.isPlaying(), 20000);
        pv.toggle(req_, 9000, -1);
        QVERIFY(!pv.isPlaying());
    }

    void stoppingWhileItRendersPlaysNothing()
    {
        tc::EditRenderCache cache;
        EditPreviewer pv(&cache);
        pv.start(req_, 9000, -1);
        QVERIFY(pv.isBusy());
        pv.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!pv.isBusy(), 20000);
        QTest::qWait(300);
        QVERIFY(!pv.isPlaying());
    }

    void regionPreviewWindowIsTwoBarsEitherSide()
    {
        QUndoStack undo;
        TrakcrateEditPanel p(&undo);
        p.setTrack(req_.grid, 40000);
        double a, b;
        QVERIFY(!p.regionPreviewWindow(&a, &b));
        QVERIFY(!p.previewRegionButton()->isEnabled());
        p.setRange(8000, 16000);
        p.addCutFromRange();                                                    // beats 16-32 (8-16 s)
        QVERIFY(p.regionPreviewWindow(&a, &b));
        QCOMPARE(a, 4000.0);
        QCOMPARE(b, 20000.0);
        QVERIFY(p.previewRegionButton()->isEnabled());
        QSignalSpy spy(&p, &TrakcrateEditPanel::previewRequested);
        p.previewRegionButton()->click();
        QCOMPARE(spy.first().at(0).toDouble(), 4000.0);
        QCOMPARE(spy.first().at(1).toDouble(), 20000.0);
        p.setPreviewStart(7000);
        p.previewButton()->click();
        QCOMPARE(spy.last().at(0).toDouble(), 7000.0);
        QCOMPARE(spy.last().at(1).toDouble(), -1.0);
        p.resetRegions();
        QCOMPARE(p.previewStart(), -1.0);
    }

    void previewOfAnInvalidEditSaysWhy()
    {
        tc::EditRenderCache cache;
        req_.regions[0].lenBeats = 6;                                           // not whole bars
        EditPreviewer pv(&cache);
        QSignalSpy status(&pv, &EditPreviewer::status);
        pv.start(req_);
        QTRY_VERIFY_WITH_TIMEOUT(!pv.isBusy(), 20000);
        QVERIFY(status.last().first().toString().contains(QStringLiteral("whole number of bars")));
        QVERIFY(!pv.isPlaying());
        req_.regions.clear();
        pv.start(req_);
        QVERIFY(status.last().first().toString().contains(QStringLiteral("Add a cut")));
    }
};

QTEST_MAIN(TestEditDialogs)
#include "test_trakcrate_edit_dialogs.moc"
