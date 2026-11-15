#include <QtTest/QtTest>

#include "audio/stream_player.h"
#include "trakcrate/analysis_wave.h"
#include "trakcrate/edit_output.h"
#include "trakcrate/library.h"
#include "trakcrate/stretch_probe.h"
#include "trakcrate/mix_render.h"
#include "ui/mix_previewer.h"
#include "ui/mix_render_dialog.h"
#include "ui/studio_mix_window.h"

#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <atomic>
#include <fstream>
#include <mutex>

using namespace trakcrate;
using namespace vdj;

namespace {

struct Capture {
    std::mutex mu;
    std::vector<float> data;
    long long frames = 0;
    bool fail = false;
};

class FakeSink : public StreamSink {
public:
    explicit FakeSink(Capture* c) : c_(c) {}
    bool write(const float* st, int frames, QString* err) override
    {
        if (c_->fail) { *err = QStringLiteral("boom"); return false; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::lock_guard<std::mutex> g(c_->mu);
        c_->data.insert(c_->data.end(), st, st + size_t(frames) * 2);
        c_->frames += frames;
        return true;
    }
private:
    Capture* c_;
};

StreamSinkFactory factoryFor(Capture* c, bool fails = false)
{
    return [c, fails](QString* err) -> std::unique_ptr<StreamSink> {
        if (fails) { *err = QStringLiteral("no device"); return nullptr; }
        return std::make_unique<FakeSink>(c);
    };
}

struct World {
    fs::path root = makeTempDir("mixprev");
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

} // namespace

class TestMixPreview : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("XDG_CACHE_HOME", QByteArray((fs::temp_directory_path() / "vdj-mixprev-cache").c_str())); }

    void streamPlayerPlaysBlocksThenFinishes()
    {
        Capture c;
        StreamPlayer p(nullptr, factoryFor(&c));
        QSignalSpy done(&p, &StreamPlayer::finished);
        int n = 0;
        QVERIFY(p.play([&](float* out, int frames) {
            for (int i = 0; i < frames * 2; ++i) out[i] = 0.25f;
            return ++n < 5;
        }));
        QVERIFY(done.wait(5000));
        QCOMPARE(c.frames, 5LL * StreamPlayer::kBlockFrames);
        QCOMPARE(p.positionFrames(), 5LL * StreamPlayer::kBlockFrames);
        QVERIFY(!p.isPlaying());
        QCOMPARE(c.data.front(), 0.25f);
    }

    void streamPlayerStopsWithoutFinished()
    {
        Capture c;
        StreamPlayer p(nullptr, factoryFor(&c));
        QSignalSpy done(&p, &StreamPlayer::finished);
        p.play([](float*, int) { return true; });
        QTRY_VERIFY(p.positionFrames() > 0);
        p.stop();
        QVERIFY(!p.isPlaying());
        QTest::qWait(50);
        QCOMPARE(done.count(), 0);
    }

    void streamPlayerReportsSinkFailures()
    {
        Capture c;
        StreamPlayer none(nullptr, factoryFor(&c, true));
        QSignalSpy f1(&none, &StreamPlayer::failed);
        none.play([](float*, int) { return true; });
        QVERIFY(f1.wait(3000));
        QCOMPARE(f1.first().first().toString(), QStringLiteral("no device"));

        c.fail = true;
        StreamPlayer bad(nullptr, factoryFor(&c));
        QSignalSpy f2(&bad, &StreamPlayer::failed);
        bad.play([](float*, int) { return true; });
        QVERIFY(f2.wait(3000));
        QCOMPARE(f2.first().first().toString(), QStringLiteral("boom"));
    }

    void streamPlayerClampsTheSamples()
    {
        Capture c;
        StreamPlayer p(nullptr, factoryFor(&c));
        QSignalSpy done(&p, &StreamPlayer::finished);
        p.play([](float* out, int frames) { for (int i = 0; i < frames * 2; ++i) out[i] = 3.0f; return false; });
        QVERIFY(done.wait(3000));
        QCOMPARE(c.data.front(), 1.0f);
    }

    void previewerPlaysTheMixWithGrowingPositions()
    {
        World w(3);
        NewMixDialog dlg(&w.lib);
        dlg.selectPlaylist("Set");
        Mix mix = defaultMix("m", dlg.tracks());
        std::vector<TrackFacts> facts;
        std::string err;
        QVERIFY(readMixFacts(mix, &facts, &err));
        StretchCache cache(fs::temp_directory_path() / "vdj-mixprev-cache");
        Capture c;
        MixPreviewer pv(&cache, nullptr, factoryFor(&c));
        pv.setMix(mix, facts);
        std::vector<double> positions;
        connect(&pv, &MixPreviewer::positionChanged, [&](double ms) { positions.push_back(ms); });
        pv.start(0);
        QVERIFY(pv.isActive());
        QTRY_COMPARE_WITH_TIMEOUT(int(pv.state()), int(MixPreviewer::Playing), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(positions.size() > 5, 5000);
        for (size_t i = 1; i < positions.size(); ++i) QVERIFY(positions[i] >= positions[i - 1]);
        pv.stop();
        QCOMPARE(int(pv.state()), int(MixPreviewer::Stopped));
        double peak = 0;
        {
            std::lock_guard<std::mutex> g(c.mu);
            for (float v : c.data) peak = std::max(peak, double(std::abs(v)));
        }
        QVERIFY(peak > 0.05);
    }

    void previewerStopsByItselfAtTheEnd()
    {
        World w(2);
        NewMixDialog dlg(&w.lib);
        dlg.selectPlaylist("Set");
        Mix mix = defaultMix("m", dlg.tracks());
        std::vector<TrackFacts> facts;
        std::string err;
        QVERIFY(readMixFacts(mix, &facts, &err));
        StretchCache cache(fs::temp_directory_path() / "vdj-mixprev-cache");
        Capture c;
        MixPreviewer pv(&cache, nullptr, factoryFor(&c));
        pv.setMix(mix, facts);
        const double len = mixLengthMs(trackStartMs(mix, effectiveTracks(mix, facts)), effectiveTracks(mix, facts));
        pv.start(len - 300);
        QTRY_COMPARE_WITH_TIMEOUT(int(pv.state()), int(MixPreviewer::Stopped), 20000);
        QVERIFY(c.frames > 0);
    }

    void previewerFailsCleanlyWithoutAudioOutput()
    {
        World w(2);
        NewMixDialog dlg(&w.lib);
        dlg.selectPlaylist("Set");
        Mix mix = defaultMix("m", dlg.tracks());
        std::vector<TrackFacts> facts;
        std::string err;
        QVERIFY(readMixFacts(mix, &facts, &err));
        StretchCache cache(fs::temp_directory_path() / "vdj-mixprev-cache");
        Capture c;
        MixPreviewer pv(&cache, nullptr, factoryFor(&c, true));
        pv.setMix(mix, facts);
        QSignalSpy status(&pv, &MixPreviewer::status);
        pv.start(0);
        QTRY_COMPARE_WITH_TIMEOUT(int(pv.state()), int(MixPreviewer::Stopped), 20000);
        QVERIFY(!status.isEmpty());
        QCOMPARE(status.last().first().toString(), QStringLiteral("no device"));
    }

    void renderDialogDefaultsAndWarnings()
    {
        World w(3);
        NewMixDialog dlg(&w.lib);
        dlg.selectPlaylist("Set");
        Mix mix = defaultMix("My set", dlg.tracks());
        const fs::path out = makeTempDir("mixout");
        MixRenderDialog d(mix, w.root, nullptr, nullptr);
        d.folderEdit()->setText(QString::fromStdString(out.string()));
        QCOMPARE(d.nameEdit()->text(), QStringLiteral("My set"));
        QVERIFY(d.plannedLabel()->text().contains(QStringLiteral("My set.mp3")));
        QVERIFY(d.plannedLabel()->text().contains(QStringLiteral("0:")) || d.plannedLabel()->text().contains(QStringLiteral(":")));
        QVERIFY(d.warnings().isEmpty());
        // a tempo difference and an unanalysed track show up as warnings
        mix.tracks[1].stretchOff = true;
        mix.masterBpm = 130;
        MixRenderDialog d2(mix, w.root, nullptr, nullptr);
        QVERIFY(!d2.warnings().isEmpty());
        QVERIFY(d2.warningsLabel()->text().contains(QStringLiteral("2 %")));
        fs::remove(mix.tracks[2].file.parent_path() / "waveform.bin");
        MixRenderDialog d3(mix, w.root, nullptr, nullptr);
        QVERIFY(d3.warnings().join(' ').contains(QStringLiteral("Track 3")));
        std::error_code ec;
        fs::remove_all(out, ec);
    }

    void renderDialogRendersWithTagsAndAvoidsClashes()
    {
        World w(2);
        NewMixDialog dlg(&w.lib);
        dlg.selectPlaylist("Set");
        Mix mix = defaultMix("Short", dlg.tracks());
        const fs::path out = makeTempDir("mixout");
        QImage img(32, 32, QImage::Format_RGB32);
        img.fill(Qt::red);
        const QString cover = QString::fromStdString((out / "cover.png").string());
        QVERIFY(img.save(cover, "PNG"));
        StretchCache cache(fs::temp_directory_path() / "vdj-mixprev-cache");
        MixRenderDialog d(mix, w.root, nullptr, &cache);
        d.folderEdit()->setText(QString::fromStdString((out / "o").string()));
        d.authorEdit()->setText(QStringLiteral("DJ Test"));
        d.imageEdit()->setText(cover);
        QSignalSpy done(&d, &MixRenderDialog::rendered);
        d.startRender();
        QVERIFY(d.isRendering());
        QVERIFY(done.wait(60000));
        QVERIFY(fs::exists(out / "o" / "Short.mp3"));
        QVERIFY(d.resultLabel()->text().startsWith(QStringLiteral("Saved")));
        QCOMPARE(d.progressBar()->value(), 1000);
        QCOMPARE(d.resultMix().author, std::string("DJ Test"));
        QCOMPARE(d.resultMix().image, cover.toStdString());
        QVERIFY(d.plannedLabel()->text().contains(QStringLiteral("Short_2.mp3")));      // the next one would not overwrite
        d.startRender();
        QVERIFY(done.wait(60000));
        QVERIFY(fs::exists(out / "o" / "Short_2.mp3"));
        std::error_code ec;
        fs::remove_all(out, ec);
    }

    void renderDialogCancelLeavesNothing()
    {
        World w(3);
        NewMixDialog dlg(&w.lib);
        dlg.selectPlaylist("Set");
        Mix mix = defaultMix("Cancelled", dlg.tracks());
        const fs::path out = makeTempDir("mixout");
        StretchCache cache(fs::temp_directory_path() / "vdj-mixprev-cache");
        MixRenderDialog d(mix, w.root, nullptr, &cache);
        d.folderEdit()->setText(QString::fromStdString(out.string()));
        d.startRender();
        d.cancelRender();
        d.waitForFinish();
        QVERIFY(!d.isRendering());
        QVERIFY(d.outputFile().isEmpty());
        QVERIFY(d.resultLabel()->text().contains(QStringLiteral("Cancelled")) || d.resultLabel()->text().startsWith(QStringLiteral("Saved")));
        if (d.outputFile().isEmpty()) for (const auto& e : fs::directory_iterator(out)) QFAIL(e.path().c_str());
        std::error_code ec;
        fs::remove_all(out, ec);
    }

    void windowKeepsNameAuthorAndImageFromTheRenderDialog()
    {
        World w(2);
        StudioMixWindow win([&]() -> const Library* { return &w.lib; }, nullptr);
        NewMixDialog dlg(&w.lib);
        dlg.selectPlaylist("Set");
        win.showPlaylistMix("Set", dlg.tracks());
        win.renderHook = [](MixRenderDialog* d) {
            d->nameEdit()->setText(QStringLiteral("Friday"));
            d->authorEdit()->setText(QStringLiteral("Me"));
        };
        win.renderMp3();
        QCOMPARE(win.mix().name, std::string("Friday"));
        QCOMPARE(win.mix().author, std::string("Me"));
        QVERIFY(win.isModified());
    }

    void windowSpaceTogglesAndEditsStop()
    {
        World w(3);
        StudioMixWindow win([&]() -> const Library* { return &w.lib; }, nullptr);
        Capture c;
        win.previewer()->setSinkFactory(factoryFor(&c));
        NewMixDialog dlg(&w.lib);
        dlg.selectPlaylist("Set");
        win.showPlaylistMix("Set", dlg.tracks());
        win.resize(1000, 600);
        win.show();
        QVERIFY(QTest::qWaitForWindowActive(&win));
        win.canvas()->setFocus();
        QVERIFY(win.isActiveWindow());
        QTest::keyClick(win.canvas(), Qt::Key_Space);
        QVERIFY(win.previewer()->isActive());
        QTRY_COMPARE_WITH_TIMEOUT(int(win.previewer()->state()), int(MixPreviewer::Playing), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(win.canvas()->playPosition() > 0, 5000);
        win.setMasterBpm(124);                                                   // any edit
        QCOMPARE(int(win.previewer()->state()), int(MixPreviewer::Stopped));
        QTest::keyClick(win.canvas(), Qt::Key_Space);
        QVERIFY(win.previewer()->isActive());
        QTest::keyClick(win.canvas(), Qt::Key_Space);
        QCOMPARE(int(win.previewer()->state()), int(MixPreviewer::Stopped));
    }
};

QTEST_MAIN(TestMixPreview)
#include "test_trakcrate_mix_preview.moc"
