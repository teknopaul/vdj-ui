#include <QtTest/QtTest>

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QRadioButton>
#include <QCheckBox>
#include <QProgressBar>
#include "ui/trakcrate_jobs_strip.h"
#include <QSignalSpy>
#include <QTreeView>
#include <QTreeWidget>

#include "ui/trakcrate_job.h"
#include "ui/trakcrate_panel.h"
#include "ui/trakcrate_window.h"
#include <QMenuBar>
#include <QSplitter>
#include "ui/trakcrate_settings_viewer.h"
#include "ui/trakcrate_export_dialog.h"
#include "ui/dir_usage.h"
#include "ui/trakcrate_preview_player.h"
#include "audio/audio_player.h"
#include "trakcrate/exporter.h"
#include "trakcrate/ext_pdb.h"
#include "trakcrate/pdb_writer.h"
#include "trakcrate/settings_files.h"
#include "trakcrate/settings_sets.h"
#include "trakcrate/usb_speed.h"
#include <QFile>
#include "ui/trakcrate_track_model.h"
#include "ui/mounted_sticks.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <atomic>
#include <chrono>
#include <thread>
#include <unistd.h>

using namespace vdj;
namespace tc = trakcrate;

static void put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xFF); }
static void put16(std::string& s, uint16_t v) { s += char(v & 0xFF); s += char(v >> 8); }

static void writeWav(const tc::fs::path& p, double freq, const std::string& title = std::string())
{
    const int rate = 8000, n = 1600;
    std::string data;
    for (int i = 0; i < n; ++i) put16(data, uint16_t(int16_t(8000 * std::sin(2 * M_PI * freq * i / rate))));
    std::string list;
    if (!title.empty()) {
        std::string z = title + '\0';
        if (z.size() % 2) z += '\0';
        std::string info = "INAM";
        put32(info, uint32_t(title.size() + 1));
        info += z;
        list = "LIST";
        put32(list, uint32_t(info.size() + 4));
        list += "INFO" + info;
    }
    std::string fmt;
    put16(fmt, 1); put16(fmt, 1); put32(fmt, rate); put32(fmt, rate * 2); put16(fmt, 2); put16(fmt, 16);
    std::string out = "RIFF";
    put32(out, uint32_t(4 + 8 + fmt.size() + list.size() + 8 + data.size()));
    out += "WAVEfmt ";
    put32(out, uint32_t(fmt.size()));
    out += fmt + list + "data";
    put32(out, uint32_t(data.size()));
    out += data;
    tc::fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << out;
}

class TestTrakcratePanel : public QObject
{
    Q_OBJECT

private:
    tc::fs::path tmp_;

private slots:
    void init() { tmp_ = tc::makeTempDir("trakcrate-panel"); QVERIFY(!tmp_.empty()); }
    void cleanup() { std::error_code ec; tc::fs::remove_all(tmp_, ec); }

    void jobRunnerRunsAndReports();
    void panelHandMadePlaylists();
    void panelEnterLoadsAndPlays();
    void panelForwardsPlayerKeys();
    void panelSearch();
    void jobRunnerOneAtATimeAndCancel();
    void modelShowsFoldersAndPlaylists();
    void modelLazyAndSortRoles();
    void panelWithoutLibrary();
    void panelImportsAndShowsTracks();
    void panelFilterAndDetails();
    void panelRefusesSecondJob();
    void panelPicksUpChangesOnDisk();
    void panelAnalysesSelectedTracks();
    void panelExportsAPlaylist();
    void rekordboxSticksAreListedFromMountedVolumes();
    void panelAddsAPlaylistToARekordboxStick();
    void panelImportsARekordboxDrive();
    void panelPendrives();
    void panelSettingsSets();
    void settingsViewer();
    void trakcrateOnlyWindow();
    void pendriveSettingsLinkAndExportDialog();
    void panelPreviewPlayer();
    void panelReordersPlaylists();
    void usbLabels();
    void bpmHelpers();
    void panelAppliesBpm();
    void panelDetectsBpmAndAsks();
};

void TestTrakcratePanel::jobRunnerRunsAndReports()
{
    TrakcrateJobRunner r;
    QSignalSpy started(&r, &TrakcrateJobRunner::started), finished(&r, &TrakcrateJobRunner::finished), progress(&r, &TrakcrateJobRunner::progress);
    int result = 0;
    QThread* main = QThread::currentThread();
    bool onWorker = false;
    QVERIFY(r.start("work", [&](const std::atomic<bool>&, const TrakcrateJobRunner::Progress& p) {
        onWorker = QThread::currentThread() != main;
        p(1, 2, "a");
        p(2, 2, "b");
        result = 42;
    }));
    QVERIFY(r.busy());
    QVERIFY(finished.wait(3000));
    QVERIFY(!r.busy());
    QCOMPARE(result, 42);
    QVERIFY(onWorker);
    QCOMPARE(started.count(), 1);
    QCOMPARE(finished.first().at(0).toString(), QStringLiteral("work"));
    QCOMPARE(finished.first().at(1).toBool(), false);
    QCOMPARE(progress.count(), 2);                                   // delivered on this thread
    QCOMPARE(progress.last().at(2).toString(), QStringLiteral("b"));
}

void TestTrakcratePanel::jobRunnerOneAtATimeAndCancel()
{
    TrakcrateJobRunner r;
    QSignalSpy finished(&r, &TrakcrateJobRunner::finished);
    QVERIFY(r.start("long", [](const std::atomic<bool>& cancel, const TrakcrateJobRunner::Progress&) {
        while (!cancel.load()) QThread::msleep(2);
    }));
    QVERIFY(!r.start("second", [](const std::atomic<bool>&, const TrakcrateJobRunner::Progress&) {}));
    r.cancel();
    QVERIFY(finished.wait(3000));
    QCOMPARE(finished.first().at(1).toBool(), true);                 // cancelled
    QVERIFY(r.start("again", [](const std::atomic<bool>&, const TrakcrateJobRunner::Progress&) {}));
    QVERIFY(finished.wait(3000));
    {   // destroying a runner with a job still going cancels it and waits
        TrakcrateJobRunner r2;
        r2.start("x", [](const std::atomic<bool>& cancel, const TrakcrateJobRunner::Progress&) {
            while (!cancel.load()) QThread::msleep(2);
        });
    }
}

void TestTrakcratePanel::modelShowsFoldersAndPlaylists()
{
    tc::Library lib;
    QVERIFY(tc::Library::create(tmp_ / "lib") && tc::Library::open(tmp_ / "lib", &lib));
    writeWav(tmp_ / "src" / "b.wav", 300, "Bravo");
    writeWav(tmp_ / "src" / "a.wav", 310);
    tc::ImportOptions o;
    QVERIFY(tc::importFolder(lib, tmp_ / "src", o).ok);

    TrakcrateTrackModel m;
    QCOMPARE(m.rowCount(), 0);
    m.showFolder(lib, "src");
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.columnCount(), int(TrakcrateTrackModel::ColumnCount));
    QCOMPARE(m.data(m.index(0, TrakcrateTrackModel::Title)).toString(), QStringLiteral("a"));        // falls back to the file name
    QCOMPARE(m.data(m.index(1, TrakcrateTrackModel::Title)).toString(), QStringLiteral("Bravo"));
    QCOMPARE(m.data(m.index(0, TrakcrateTrackModel::Origin)).toString(), QStringLiteral("generated"));
    QVERIFY(m.data(m.index(0, TrakcrateTrackModel::Flags)).toString().contains("no beat grid"));
    QVERIFY(!m.data(m.index(0, TrakcrateTrackModel::Flags)).toString().contains("no audio"));
    QCOMPARE(m.data(m.index(0, TrakcrateTrackModel::Time)).toString(), QStringLiteral("0:00"));      // 0.2 s
    QVERIFY(m.trackDir(1).endsWith("Bravo") || m.trackDir(1).endsWith("b"));
    // the rating column: always five stars, and editing it rewrites meta.txt
    QCOMPARE(m.data(m.index(0, TrakcrateTrackModel::Rating)).toString(), QString::fromUtf8("\u2606\u2606\u2606\u2606\u2606"));
    QSignalSpy written(&m, &TrakcrateTrackModel::ratingWritten);
    QVERIFY(m.setData(m.index(0, 0), 3, TrakcrateTrackModel::RatingRole));
    QCOMPARE(written.count(), 1);
    QCOMPARE(m.data(m.index(0, TrakcrateTrackModel::Rating)).toString(), QString::fromUtf8("\u2605\u2605\u2605\u2606\u2606"));
    QCOMPARE(m.data(m.index(0, TrakcrateTrackModel::Rating), TrakcrateTrackModel::SortRole).toInt(), 3);
    {
        tc::Track r;
        QVERIFY(tc::loadTrack(m.trackDir(0).toStdString(), &r));
        QCOMPARE(r.rating(), 3);
        QCOMPARE(QString::fromStdString(tc::origin(r.source, "meta")), QStringLiteral("edited"));
    }
    // the comment column: editable text that is written to meta.txt
    QVERIFY(m.flags(m.index(0, TrakcrateTrackModel::Comment)) & Qt::ItemIsEditable);
    QVERIFY(!(m.flags(m.index(0, TrakcrateTrackModel::Title)) & Qt::ItemIsEditable));
    QVERIFY(m.setData(m.index(0, TrakcrateTrackModel::Comment), QStringLiteral("  warm up  "), Qt::EditRole));
    QCOMPARE(m.data(m.index(0, TrakcrateTrackModel::Comment)).toString(), QStringLiteral("warm up"));
    {
        tc::Track r;
        QVERIFY(tc::loadTrack(m.trackDir(0).toStdString(), &r));
        QCOMPARE(QString::fromStdString(r.comment()), QStringLiteral("warm up"));
        QCOMPARE(r.rating(), 3);                                                                  // the rest of meta.txt is kept
    }
    QVERIFY(m.setData(m.index(0, 0), 9, TrakcrateTrackModel::RatingRole));                                 // clamped
    QCOMPARE(m.data(m.index(0, 0), TrakcrateTrackModel::RatingRole).toInt(), 5);
    tc::Track t;
    QVERIFY(m.track(1, &t));
    QCOMPARE(QString::fromStdString(t.meta.get("title")), QStringLiteral("Bravo"));
    QVERIFY(!m.track(5, &t));
    QVERIFY(m.trackDir(-1).isEmpty());

    m.showPlaylist(lib, "src");
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.data(m.index(0, TrakcrateTrackModel::Title)).toString(), QStringLiteral("a"));        // playlist order is alphabetical
    m.showPlaylist(lib, "no such playlist");
    QCOMPARE(m.rowCount(), 0);
    m.clear();
    QCOMPARE(m.rowCount(), 0);
}

void TestTrakcratePanel::modelLazyAndSortRoles()
{
    tc::Library lib;
    QVERIFY(tc::Library::create(tmp_ / "lib") && tc::Library::open(tmp_ / "lib", &lib));
    for (int i = 0; i < 300; ++i) {                                  // a big folder opens without reading every track
        tc::fs::path d = lib.createTrackDir("Big", "t" + std::to_string(i));
        tc::Track t;
        t.dir = d;
        t.meta.set("title", "Song " + std::to_string(i));
        t.meta.setInt("duration_ms", 60000 + i * 1000);
        t.hasGrid = true;
        tc::setBeatGrid(t.grid, tc::BeatGrid{100.0 + i, 0});
        QVERIFY(tc::saveMeta(t));
        QVERIFY(tc::saveBeatGrid(t));
    }
    TrakcrateTrackModel m;
    m.showFolder(lib, "Big");
    QCOMPARE(m.rowCount(), 300);
    QCOMPARE(m.data(m.index(299, TrakcrateTrackModel::Bpm), TrakcrateTrackModel::SortRole).toDouble(), 100.0 + 299);     // numeric sort key
    QCOMPARE(m.data(m.index(299, TrakcrateTrackModel::Bpm)).toString(), QStringLiteral("399.00"));
    QCOMPARE(m.data(m.index(1, TrakcrateTrackModel::Time)).toString(), QStringLiteral("1:01"));
    QVERIFY(!m.data(m.index(1, TrakcrateTrackModel::Flags)).toString().contains("no beat grid"));
    QVERIFY(m.data(m.index(1, TrakcrateTrackModel::Flags)).toString().contains("no audio"));
}

void TestTrakcratePanel::panelWithoutLibrary()
{
    TrakcratePanel p;
    QVERIFY(!p.hasLibrary());
    QCOMPARE(p.visibleTrackCount(), 0);
    QVERIFY(!p.importFolder("/tmp", tc::ImportOptions()));           // refused: no library
    QString err;
    QVERIFY(!p.openLibrary((tmp_ / "nothing").string().c_str(), &err));
    QVERIFY(!err.isEmpty());
    p.refresh();                                                     // harmless
}

void TestTrakcratePanel::panelImportsAndShowsTracks()
{
    writeWav(tmp_ / "Acid" / "two.wav", 300);
    writeWav(tmp_ / "Acid" / "one.wav", 310, "First One");
    TrakcratePanel p;
    QString err;
    QVERIFY2(p.createLibrary((tmp_ / "lib").string().c_str(), &err), qPrintable(err));
    QVERIFY(p.hasLibrary());
    QSignalSpy done(&p, &TrakcratePanel::importFinished);
    QVERIFY(p.importFolder((tmp_ / "Acid").string().c_str(), tc::ImportOptions()));
    QVERIFY(p.busy());
    QVERIFY(!p.importFolder((tmp_ / "Acid").string().c_str(), tc::ImportOptions()));      // one job at a time
    QVERIFY(p.waitForJob());
    QCOMPARE(done.count(), 1);
    QVERIFY2(done.first().at(1).toBool(), qPrintable(done.first().at(0).toString()));
    QVERIFY(done.first().at(0).toString().contains("Imported 2"));
    QVERIFY(done.first().at(0).toString().contains("playlist \"Acid\""));
    QVERIFY(p.library().hasPlaylist("Acid"));

    p.selectFolder("Acid");
    QCOMPARE(p.visibleTrackCount(), 2);
    p.selectPlaylist("Acid");
    QCOMPARE(p.visibleTrackCount(), 2);
    QCOMPARE(p.trackModel()->data(p.trackModel()->index(0, TrakcrateTrackModel::Title)).toString(), QStringLiteral("First One"));   // alphabetical: "one" < "two"
}

void TestTrakcratePanel::panelFilterAndDetails()
{
    writeWav(tmp_ / "Mix" / "alpha.wav", 300, "Alpha Song");
    writeWav(tmp_ / "Mix" / "beta.wav", 320, "Beta Tune");
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    QVERIFY(p.importFolder((tmp_ / "Mix").string().c_str(), tc::ImportOptions()));
    QVERIFY(p.waitForJob());
    p.selectFolder("Mix");
    QCOMPARE(p.visibleTrackCount(), 2);

    // selecting a row fills the details pane (the labels are children of the panel)
    p.show();
    QList<QLabel*> labels = p.findChildren<QLabel*>();
    QTreeView* table = nullptr;                                      // QTreeWidget is a QTreeView too: take the one that is not
    for (QTreeView* v : p.findChildren<QTreeView*>())
        if (!qobject_cast<QTreeWidget*>(v)) table = v;
    QVERIFY(table);
    table->setCurrentIndex(table->model()->index(0, 0));
    QString all;
    for (QLabel* l : labels) all += l->text();
    QVERIFY2(all.contains("Alpha Song"), qPrintable(all));
}

void TestTrakcratePanel::panelRefusesSecondJob()
{
    writeWav(tmp_ / "Gig" / "a.wav", 300);
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    tc::ImportOptions o;
    QVERIFY(p.importFolder((tmp_ / "Gig").string().c_str(), o));
    QVERIFY(p.waitForJob());
    QSignalSpy done(&p, &TrakcratePanel::importFinished);
    QVERIFY(p.importFolder((tmp_ / "Gig").string().c_str(), o));      // the playlist exists now: the report says so, nothing breaks
    QVERIFY(p.waitForJob());
    QCOMPARE(done.count(), 1);
    QVERIFY(!done.first().at(1).toBool());
    QVERIFY(done.first().at(0).toString().contains("exists"));
}

void TestTrakcratePanel::panelPicksUpChangesOnDisk()
{
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    QVERIFY(p.library().listFolders().empty());
    tc::fs::path d = p.library().createTrackDir("Added", "outside");   // another program adds a track
    tc::Track t;
    t.dir = d;
    t.meta.set("title", "Outside");
    QVERIFY(tc::saveMeta(t));
    p.refresh();
    p.selectFolder("Added");
    QCOMPARE(p.visibleTrackCount(), 1);
    // the file system watcher notices a new folder by itself (debounced)
    p.library().createTrackDir("Second", "x");
    QTRY_VERIFY_WITH_TIMEOUT(p.findChild<QTreeWidget*>()->topLevelItem(0)->childCount() == 2, 5000);
}

void TestTrakcratePanel::panelAnalysesSelectedTracks()
{
    writeWav(tmp_ / "Set" / "a.wav", 300);
    writeWav(tmp_ / "Set" / "b.wav", 320);
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    QVERIFY(p.importFolder((tmp_ / "Set").string().c_str(), tc::ImportOptions()));      // no analysis yet
    QVERIFY(p.waitForJob());
    tc::fs::path a = p.library().trackDir("Set", "a"), b = p.library().trackDir("Set", "b");
    QVERIFY(!tc::fs::exists(a / "waveform.bin"));
    QSignalSpy done(&p, &TrakcratePanel::analysisFinished);
    QVERIFY(!p.analyseTracks(QStringList()));                                           // nothing to do
    QVERIFY(p.analyseTracks(QStringList() << QString::fromStdString(a.string()) << QString::fromStdString(b.string())));
    QVERIFY(p.busy());
    QVERIFY(!p.analyseTracks(QStringList() << QString::fromStdString(a.string())));     // one job at a time
    QVERIFY(p.waitForJob());
    QCOMPARE(done.count(), 1);
    QVERIFY2(done.first().at(1).toBool(), qPrintable(done.first().at(0).toString()));
    QVERIFY(done.first().at(0).toString().contains("Analysed 2"));
    QVERIFY(tc::fs::exists(a / "waveform.bin") && tc::fs::exists(b / "beatgrid.txt"));

    // run again: the same result, the origins stay "generated" so nothing is protected; then protect one and see it left alone
    tc::Track t;
    QVERIFY(tc::loadTrack(b, &t));
    tc::setOrigin(t.source, "beatgrid", "rekordbox");
    tc::setOrigin(t.source, "waveform", "rekordbox");
    QVERIFY(tc::saveSource(t));
    QVERIFY(p.analyseTracks(QStringList() << QString::fromStdString(b.string())));
    QVERIFY(p.waitForJob());
    QVERIFY(done.last().at(0).toString().contains("1 left as they were"));
    QVERIFY(p.analyseTracks(QStringList() << QString::fromStdString(b.string()), true));  // forced
    QVERIFY(p.waitForJob());
    QVERIFY(done.last().at(0).toString().contains("Analysed 1"));

    // the import dialog's option reaches the importer
    writeWav(tmp_ / "Auto" / "c.wav", 330);
    tc::ImportOptions o;
    o.analyse = true;
    QVERIFY(p.importFolder((tmp_ / "Auto").string().c_str(), o));
    QVERIFY(p.waitForJob());
    QVERIFY(tc::fs::exists(p.library().trackDir("Auto", "c") / "waveform.bin"));
}

void TestTrakcratePanel::rekordboxSticksAreListedFromMountedVolumes()
{
    auto makeStick = [&](const char* name, bool database) {
        const tc::fs::path root = tmp_ / name;
        tc::fs::create_directories(root / "PIONEER/rekordbox");
        if (database) std::ofstream(root / "PIONEER/rekordbox/export.pdb") << "x";
        return QString::fromStdString(root.string());
    };
    MountedVolume ok, noDb, readOnly, notReady, unnamed;
    ok.root = makeStick("ok", true);            ok.name = "KIOXIA";        ok.freeBytes = 5ll << 30;
    noDb.root = makeStick("nodb", false);       noDb.name = "PLAIN";
    readOnly.root = makeStick("ro", true);      readOnly.name = "LOCKED";  readOnly.readOnly = true;
    notReady.root = makeStick("nr", true);      notReady.name = "GONE";    notReady.ready = false;
    unnamed.root = makeStick("unnamed", true);
    const QList<RekordboxStick> sticks = listRekordboxSticks([&]() { return QList<MountedVolume>{noDb, ok, readOnly, notReady, unnamed}; });
    QCOMPARE(sticks.size(), 2);                                                          // only ready, writable volumes with PIONEER/rekordbox/export.pdb, in order
    QCOMPARE(sticks[0].root, ok.root);
    QVERIFY(sticks[0].label.startsWith("KIOXIA ("));
    QVERIFY(sticks[0].label.contains("free"));
    QCOMPARE(sticks[1].root, unnamed.root);
    QVERIFY(sticks[1].label.startsWith("unnamed ("));                                    // no volume name: the folder name
    QVERIFY(listRekordboxSticks([]() { return QList<MountedVolume>(); }).isEmpty());
}

void TestTrakcratePanel::panelAddsAPlaylistToARekordboxStick()
{
    auto makeSet = [&](const char* folder, std::initializer_list<const char*> names, double base) {
        double f = base;
        for (const char* n : names) writeWav(tmp_ / folder / (std::string(n) + ".wav"), f += 40);
    };
    makeSet("Gig", {"g1", "g2"}, 300);
    makeSet("More", {"m1", "m2", "m3"}, 700);
    makeSet("Raw", {"r1"}, 900);
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    tc::ImportOptions io;
    io.analyse = true;
    QVERIFY(p.importFolder((tmp_ / "Gig").string().c_str(), io));
    QVERIFY(p.waitForJob());
    QVERIFY(p.importFolder((tmp_ / "More").string().c_str(), io));
    QVERIFY(p.waitForJob());
    tc::ImportOptions raw;
    raw.analyse = false;
    QVERIFY(p.importFolder((tmp_ / "Raw").string().c_str(), raw));
    QVERIFY(p.waitForJob());

    tc::ExportOptions o;
    o.playlists = {"Gig"};
    o.dest = tmp_ / "stick";
    o.stagingBase = tmp_;
    QSignalSpy done(&p, &TrakcratePanel::exportFinished);
    QVERIFY(p.exportDrive(o));
    QVERIFY(p.waitForJob(20000));
    QVERIFY2(done.last().at(1).toBool(), qPrintable(done.last().at(0).toString()));

    // the library as it is now: adding to the stick must not touch it
    auto snapshot = [&]() {
        std::map<std::string, std::uintmax_t> m;
        std::error_code ec;
        for (const auto& e : tc::fs::recursive_directory_iterator(tmp_ / "lib", ec))
            if (e.is_regular_file(ec)) m[e.path().lexically_relative(tmp_ / "lib").generic_string()] = tc::fs::file_size(e.path(), ec);
        return m;
    };
    const auto libBefore = snapshot();
    QCOMPARE(p.unanalysedInPlaylist("More"), 0);
    QCOMPARE(p.unanalysedInPlaylist("Raw"), 1);

    const QString stick = QString::fromStdString((tmp_ / "stick").string());
    QString err;
    QVERIFY2(p.addToRekordbox("More", stick, false, &err), qPrintable(err));
    QVERIFY(p.busy());
    QString again;
    QVERIFY(!p.addToRekordbox("Raw", stick, true, &again));                                 // one job on a pendrive at a time (and exclusive jobs are not running)
    QVERIFY(p.waitForJob(30000));
    QCOMPARE(done.count(), 2);
    QVERIFY2(done.last().at(1).toBool(), qPrintable(done.last().at(0).toString()));
    QVERIFY(done.last().at(0).toString().contains("Added playlist \"More\" to"));
    QVERIFY(done.last().at(0).toString().contains("3 new track(s)"));
    QCOMPARE(done.last().at(2).toString(), stick);
    QVERIFY(tc::fs::exists(tmp_ / "stick/PIONEER/rekordbox/export.pdb.bak"));
    QVERIFY(libBefore == snapshot());                                                       // no change to the trakcrate library

    // a playlist with the name already on the stick fails, and says so
    QVERIFY(p.addToRekordbox("More", stick, false, &err));
    QVERIFY(p.waitForJob(30000));
    QCOMPARE(done.count(), 3);
    QVERIFY(!done.last().at(1).toBool());
    QVERIFY2(done.last().at(0).toString().contains("already has a playlist"), qPrintable(done.last().at(0).toString()));

    // an unanalysed playlist is refused by the engine unless the question was answered yes
    QVERIFY(p.addToRekordbox("Raw", stick, false, &err));
    QVERIFY(p.waitForJob(30000));
    QVERIFY(!done.last().at(1).toBool());
    QVERIFY(done.last().at(0).toString().contains("not analysed"));
    QVERIFY(p.addToRekordbox("Raw", stick, true, &err));
    QVERIFY(p.waitForJob(30000));
    QVERIFY2(done.last().at(1).toBool(), qPrintable(done.last().at(0).toString()));
    QVERIFY(libBefore == snapshot());
}

void TestTrakcratePanel::panelExportsAPlaylist()
{
    // click tracks so the analysis finds a beat grid; the panel adds the template database and the artwork encoder itself
    for (const char* n : {"p", "q"}) {
        const int rate = 44100;
        std::vector<float> m(size_t(rate) * 8, 0.0f);
        for (int beat = 0; beat < 16; ++beat)
            for (int i = 0; i < 80; ++i) m[size_t(beat * rate / 2 + rate / 10 + i)] = 0.7f * float(std::sin(i * 0.4)) * (n[0] == 'p' ? 1.0f : 0.8f);
        std::string wav = "RIFF";
        put32(wav, uint32_t(36 + m.size() * 2));
        wav += "WAVEfmt ";
        put32(wav, 16); put16(wav, 1); put16(wav, 1); put32(wav, rate); put32(wav, rate * 2); put16(wav, 2); put16(wav, 16);
        wav += "data";
        put32(wav, uint32_t(m.size() * 2));
        for (float v : m) put16(wav, uint16_t(int16_t(v * 30000)));
        tc::fs::create_directories(tmp_ / "Gig");
        std::ofstream(tmp_ / "Gig" / (std::string(n) + ".wav"), std::ios::binary) << wav;
    }
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    tc::ImportOptions io;
    io.analyse = true;
    QVERIFY(p.importFolder((tmp_ / "Gig").string().c_str(), io));
    QVERIFY(p.waitForJob());
    QVERIFY(p.library().hasPlaylist("Gig"));

    tc::ExportOptions o;
    o.playlists = {"Gig"};
    o.dest = tmp_ / "stick";
    o.stagingBase = tmp_;
    o.verify = true;
    o.writeManifest = true;
    QSignalSpy done(&p, &TrakcratePanel::exportFinished);
    QSignalSpy reg(&p, &TrakcratePanel::registerPendriveRequested);
    QVERIFY(p.exportDrive(o, "Gig on stick"));
    QVERIFY(p.busy());
    QVERIFY(!p.exportDrive(o));                                                          // one job at a time
    QVERIFY(p.waitForJob(20000));
    QCOMPARE(done.count(), 1);
    QVERIFY2(done.first().at(1).toBool(), qPrintable(done.first().at(0).toString()));
    QVERIFY(done.first().at(0).toString().contains("Exported 2 track(s)"));
    QVERIFY(done.first().at(0).toString().contains("verified"));
    QCOMPARE(reg.count(), 1);
    QCOMPARE(reg.first().at(0).toString(), QStringLiteral("Gig on stick"));
    QCOMPARE(reg.first().at(1).toString(), QString::fromStdString((tmp_ / "stick").string()));
    QVERIFY(tc::fs::exists(tmp_ / "stick/PIONEER/rekordbox/export.pdb"));
    QVERIFY(tc::fs::exists(tmp_ / "stick/SHA256SUMS"));

    // a second export to the same (now full) folder fails and says so; nothing is registered
    QVERIFY(p.exportDrive(o, "again"));
    QVERIFY(p.waitForJob(20000));
    QCOMPARE(done.count(), 2);
    QVERIFY(!done.last().at(1).toBool());
    QVERIFY(done.last().at(0).toString().contains("already has"));
    QCOMPARE(reg.count(), 1);
    QVERIFY(tc::fs::exists(tmp_ / "stick/PIONEER/rekordbox/export.pdb"));                // the earlier export is untouched

    // update: the same folder is refreshed, the old database kept as a backup
    o.update = true;
    QVERIFY(p.exportDrive(o));
    QVERIFY(p.waitForJob(20000));
    QCOMPARE(done.count(), 3);
    QVERIFY2(done.last().at(1).toBool(), qPrintable(done.last().at(0).toString()));
    QVERIFY(done.last().at(0).toString().contains("export.pdb.bak"));
    QVERIFY(tc::fs::exists(tmp_ / "stick/PIONEER/rekordbox/export.pdb.bak"));

    // Cancel button: the job ends, the UI carries on, an update leaves the drive's database whole
    p.show();
    QVERIFY(p.exportDrive(o));
    QPushButton* cancelButton = nullptr;
    for (QPushButton* b : p.findChildren<QPushButton*>()) if (b->text() == "Cancel") cancelButton = b;
    QVERIFY(cancelButton);
    cancelButton->click();
    QVERIFY(p.waitForJob(20000));
    QCOMPARE(done.count(), 4);
    QVERIFY(!p.busy());
    QVERIFY(tc::fs::file_size(tmp_ / "stick/PIONEER/rekordbox/export.pdb") > 1000);
    QVERIFY(p.exportDrive(o));                                                           // and it can export again
    QVERIFY(p.waitForJob(20000));
    QCOMPARE(done.count(), 5);

    // several exports at once: different folders run side by side, each reports on its own
    tc::ExportOptions a = o, b = o;
    a.update = b.update = false;
    a.dest = tmp_ / "stickA";
    b.dest = tmp_ / "stickB";
    QVERIFY(p.exportDrive(a, "A"));
    QVERIFY(p.exportDrive(b, "B"));
    QVERIFY(p.busy());
    int rows = 0;
    for (QProgressBar* bar : p.findChildren<QProgressBar*>()) if (bar->isVisibleTo(&p)) ++rows;
    QVERIFY2(rows >= 2, "a progress bar for each running export");
    tc::ExportOptions nested = o;
    nested.update = false;
    nested.dest = tmp_ / "stickA" / "inside";
    QVERIFY(!p.exportDrive(nested));                                                     // a folder inside a running export's is refused
    QVERIFY(!p.exportDrive(a));                                                          // and so is the same one
    QVERIFY(!p.importFolder((tmp_ / "Gig").string().c_str(), tc::ImportOptions()));      // the library is not changed under running exports
    QVERIFY(p.waitForJob(30000));
    QCOMPARE(done.count(), 7);
    QVERIFY2(done.at(5).at(1).toBool(), qPrintable(done.at(5).at(0).toString()));
    QVERIFY2(done.at(6).at(1).toBool(), qPrintable(done.at(6).at(0).toString()));
    QVERIFY(tc::fs::exists(tmp_ / "stickA/PIONEER/rekordbox/export.pdb"));
    QVERIFY(tc::fs::exists(tmp_ / "stickB/PIONEER/rekordbox/export.pdb"));
    QCOMPARE(reg.count(), 3);                                                            // "Gig on stick", "A" and "B"
    QVERIFY(!p.busy());

    // Cancel stops every running export; the panel is idle again
    tc::fs::remove_all(tmp_ / "stickA");
    tc::fs::remove_all(tmp_ / "stickB");
    QVERIFY(p.exportDrive(a));
    QVERIFY(p.exportDrive(b));
    int cancelButtons = 0;                                                               // one Cancel per running export, in the jobs strip
    for (QPushButton* bt : p.findChildren<QPushButton*>()) if (bt->text() == "Cancel" && bt->isEnabled()) { bt->click(); ++cancelButtons; }
    QVERIFY(cancelButtons >= 2);
    QVERIFY(p.waitForJob(30000));
    QCOMPARE(done.count(), 9);
    QVERIFY(!p.busy());
}

void TestTrakcratePanel::panelImportsARekordboxDrive()
{
    // a drive written by our own exporter
    tc::Library src;
    QVERIFY(tc::Library::create(tmp_ / "srclib") && tc::Library::open(tmp_ / "srclib", &src));
    writeWav(tmp_ / "House" / "a.wav", 300);
    writeWav(tmp_ / "House" / "b.wav", 330);
    tc::ImportOptions io;
    io.analyse = true;
    QVERIFY(tc::importFolder(src, tmp_ / "House", io).ok);
    QFile tpl(QStringLiteral(":/trakcrate/template-export.pdb"));
    QVERIFY(tpl.open(QIODevice::ReadOnly));
    tc::ExportOptions eo;
    eo.playlists = {"House"};
    eo.dest = tmp_ / "usb";
    eo.templatePdb.assign(tpl.readAll().constData(), size_t(tpl.size()));
    eo.stagingBase = tmp_;
    QVERIFY(tc::exportDrive(src, eo).ok);

    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    QVERIFY(!p.importRekordbox(QString::fromStdString((tmp_ / "House").string()), tc::RbImportOptions()) || p.waitForJob());   // not a drive: fails inside the job
    QSignalSpy done(&p, &TrakcratePanel::rekordboxImportFinished);
    QVERIFY(p.waitForJob());
    done.clear();
    QVERIFY(p.importRekordbox(QString::fromStdString((tmp_ / "usb").string()), tc::RbImportOptions()));
    QVERIFY(!p.importRekordbox(QString::fromStdString((tmp_ / "usb").string()), tc::RbImportOptions()));     // one job at a time
    QVERIFY(p.waitForJob());
    QTRY_COMPARE(done.count(), 1);
    QVERIFY2(done.at(0).at(1).toBool(), qPrintable(done.at(0).at(0).toString()));
    QCOMPARE(p.library().listFolders().size(), size_t(1));
    QCOMPARE(p.library().listPlaylists().size(), size_t(1));
}

void TestTrakcratePanel::panelReordersPlaylists()
{
    for (const char* n : {"a", "b", "c", "d"}) writeWav(tmp_ / "Set" / (std::string(n) + ".wav"), 300, std::string("Track ") + n);
    writeWav(tmp_ / "Other" / "z.wav", 330, "Z");
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    for (const char* f : {"Set", "Other"}) {
        QVERIFY(p.importFolder((tmp_ / f).string().c_str(), tc::ImportOptions()));
        QVERIFY(p.waitForJob());
    }
    QTreeView* table = p.findChild<QTreeView*>();
    QTreeWidget* tree = p.findChild<QTreeWidget*>();
    auto titles = [&]() {
        QStringList l;
        for (int r = 0; r < p.visibleTrackCount(); ++r) l << table->model()->index(r, 0).data().toString();
        return l;
    };
    // folders can be sorted, playlists cannot: their order is the DJ's work
    p.selectFolder("Set");
    QVERIFY(table->isSortingEnabled());
    p.selectPlaylist("Set");
    QVERIFY(!table->isSortingEnabled());
    QCOMPARE(table->dragDropMode(), QAbstractItemView::InternalMove);
    QCOMPARE(titles(), QStringList({"Track a", "Track b", "Track c", "Track d"}));
    QString err;
    QVERIFY(p.moveRows({3}, 0, &err));                                                  // Move to start
    QCOMPARE(titles(), QStringList({"Track d", "Track a", "Track b", "Track c"}));
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QCOMPARE(table->selectionModel()->selectedRows().first().row(), 0);
    QVERIFY(p.moveRows({0, 1}, 4, &err));                                               // two rows to the end (drag below the last)
    QCOMPARE(titles(), QStringList({"Track b", "Track c", "Track d", "Track a"}));
    QCOMPARE(table->selectionModel()->selectedRows().size(), 2);
    QVERIFY(!p.moveRows({}, 1, &err));
    // the order is on disk: a fresh panel on the same library sees it
    {
        TrakcratePanel q;
        QVERIFY(q.openLibrary((tmp_ / "lib").string().c_str()));
        q.selectPlaylist("Set");
        QTreeView* t2 = q.findChild<QTreeView*>();
        QCOMPARE(t2->model()->index(0, 0).data().toString(), QString("Track b"));
        QCOMPARE(t2->model()->index(3, 0).data().toString(), QString("Track a"));
    }
    p.selectFolder("Set");
    QVERIFY(!p.moveRows({0}, 2, &err));                                                 // a folder has no order to change

    // Ctrl+Up / Ctrl+Down move the selected tracks one place
    p.selectPlaylist("Set");
    table->setFocus();
    table->selectionModel()->select(table->model()->index(1, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QTest::keyClick(table, Qt::Key_Up, Qt::ControlModifier);
    QCOMPARE(titles(), QStringList({"Track c", "Track b", "Track d", "Track a"}));
    QTest::keyClick(table, Qt::Key_Down, Qt::ControlModifier);
    QTest::keyClick(table, Qt::Key_Down, Qt::ControlModifier);
    QCOMPARE(titles(), QStringList({"Track b", "Track d", "Track c", "Track a"}));
    QTest::keyClick(table, Qt::Key_Down, Qt::ControlModifier);
    QTest::keyClick(table, Qt::Key_Down, Qt::ControlModifier);                           // already last: stays there
    QCOMPARE(titles(), QStringList({"Track b", "Track d", "Track a", "Track c"}));
    QVERIFY(p.moveSelected(-2));                                                         // Move to start
    QCOMPARE(titles().first(), QString("Track c"));
    QVERIFY(p.moveSelected(2));                                                          // Move to end
    QCOMPARE(titles().last(), QString("Track c"));

    // the lists of a pendrive are dragged into order in the tree
    QVERIFY(p.createPendrive(QStringLiteral("Fri"), {QStringLiteral("Set")}, {QStringLiteral("Other")}, &err));
    QCOMPARE(p.library().driveEntries("Fri")[0].name, std::string("Set"));
    QVERIFY(p.moveEntry("Fri", 1, 0, &err));                                            // Other dropped before Set
    QCOMPARE(p.library().driveEntries("Fri")[0].name, std::string("Other"));
    QVERIFY(tree->currentItem() && tree->currentItem()->text(0).startsWith("Other"));    // the moved list stays chosen
    QVERIFY(p.moveEntry("Fri", 0, 2, &err));                                            // dropped after the last one
    QCOMPARE(p.library().driveEntries("Fri")[1].name, std::string("Other"));
    QVERIFY(p.moveEntry("Fri", 1, 1, &err) && p.moveEntry("Fri", 1, 2, &err));          // dropped where it is
    QCOMPARE(p.library().driveEntries("Fri")[1].name, std::string("Other"));
    tree->setFocus();
    QTest::keyClick(tree, Qt::Key_Up, Qt::ControlModifier);                              // Ctrl+Up on the chosen list
    QCOMPARE(p.library().driveEntries("Fri")[0].name, std::string("Other"));
    QTest::keyClick(tree, Qt::Key_Up, Qt::ControlModifier);                              // already first
    QCOMPARE(p.library().driveEntries("Fri")[0].name, std::string("Other"));
    QTest::keyClick(tree, Qt::Key_Down, Qt::ControlModifier);
    QCOMPARE(p.library().driveEntries("Fri")[1].name, std::string("Other"));
    QTest::keyClick(tree, Qt::Key_Down, Qt::ControlModifier);                            // already last
    QCOMPARE(p.library().driveEntries("Fri")[1].name, std::string("Other"));
}

void TestTrakcratePanel::panelHandMadePlaylists()
{
    writeWav(tmp_ / "House" / "a.wav", 300, "A one");
    writeWav(tmp_ / "House" / "b.wav", 330, "B two");
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    QVERIFY(p.importFolder((tmp_ / "House").string().c_str(), tc::ImportOptions()));
    QVERIFY(p.waitForJob());
    QString err;
    QVERIFY(p.createPlaylist(QStringLiteral("Mine"), &err));
    QVERIFY(!p.createPlaylist(QStringLiteral("Mine"), &err));              // a second one of that name is refused
    QCOMPARE(p.currentPlaylist(), QStringLiteral("Mine"));
    QVERIFY(p.library().hasPlaylist("Mine"));
    QVERIFY(p.library().playlistTracks("Mine").empty());

    p.selectFolder("House");
    QTreeView* table = p.findChild<QTreeView*>();
    table->selectAll();
    const QStringList dirs = p.selectedTrackDirs();
    QCOMPARE(dirs.size(), 2);
    QVERIFY(p.addToPlaylist(p.currentPlaylist(), dirs, &err));
    QCOMPARE(int(p.library().playlistTracks("Mine").size()), 2);

    // remove the first row of the playlist: the track stays in the folder
    p.selectPlaylist("Mine");
    table->clearSelection();
    table->selectionModel()->select(table->model()->index(0, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QVERIFY(p.removeSelectedFromPlaylist(&err));
    QCOMPARE(int(p.library().playlistTracks("Mine").size()), 1);
    QCOMPARE(int(p.library().listTracks("House").size()), 2);
}

void TestTrakcratePanel::panelEnterLoadsAndPlays()
{
    writeWav(tmp_ / "House" / "a.wav", 300, "A one");
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    QVERIFY(p.importFolder((tmp_ / "House").string().c_str(), tc::ImportOptions()));
    QVERIFY(p.waitForJob());
    p.selectFolder("House");
    QTreeView* table = p.findChild<QTreeView*>();
    table->setCurrentIndex(table->model()->index(0, 0));
    QSignalSpy spy(&p, &TrakcratePanel::loadAndPlayRequested);
    // Up and Down are application shortcuts (pitch nudge): the list claims them while it has the focus
    QKeyEvent over(QEvent::ShortcutOverride, Qt::Key_Down, Qt::NoModifier);
    over.setAccepted(false);
    QCoreApplication::sendEvent(table, &over);
    QVERIFY(over.isAccepted());
    QTest::keyClick(table, Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
}

void TestTrakcratePanel::panelForwardsPlayerKeys()
{
    writeWav(tmp_ / "House" / "a.wav", 300, "A one");
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    QVERIFY(p.importFolder((tmp_ / "House").string().c_str(), tc::ImportOptions()));
    QVERIFY(p.waitForJob());
    p.selectFolder("House");
    QTreeView* table = p.findChild<QTreeView*>();
    table->setCurrentIndex(table->model()->index(0, 0));
    bool loaded = false;
    int handled = 0;
    p.setPlayerKeys([&](const QKeyEvent* e) { return loaded && e->key() == Qt::Key_Space; },
                    [&](QKeyEvent* e) { if (!loaded || e->key() != Qt::Key_Space) return false; ++handled; e->accept(); return true; });
    QKeyEvent over(QEvent::ShortcutOverride, Qt::Key_Space, Qt::NoModifier);
    over.setAccepted(false);
    QCoreApplication::sendEvent(table, &over);
    QVERIFY(!over.isAccepted());                     // nothing loaded: Space stays the VDJ's play / pause
    QTest::keyClick(table, Qt::Key_Space);
    QCOMPARE(handled, 0);
    loaded = true;
    over.setAccepted(false);
    QCoreApplication::sendEvent(table, &over);
    QVERIFY(over.isAccepted());
    QTest::keyClick(table, Qt::Key_Space);
    QCOMPARE(handled, 1);
}

void TestTrakcratePanel::panelSearch()
{
    writeWav(tmp_ / "House" / "a.wav", 300, "Alpha tune");
    writeWav(tmp_ / "Dub" / "b.wav", 330, "Beta tune");
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    for (const char* f : {"House", "Dub"}) {
        QVERIFY(p.importFolder((tmp_ / f).string().c_str(), tc::ImportOptions()));
        QVERIFY(p.waitForJob());
    }
    QCOMPARE(p.searchTracks(false, QStringLiteral("zzz")), 0);
    QVERIFY(!p.searchNext());
    QCOMPARE(p.searchTracks(false, QStringLiteral("TUNE")), 2);              // case does not matter
    QCOMPARE(p.selectedTrackDirs().size(), 1);
    const QString first = p.selectedTrackDirs().first();
    QVERIFY(p.searchNext());
    QVERIFY(p.selectedTrackDirs().first() != first);                         // the other folder was opened
    QVERIFY(p.searchNext());
    QCOMPARE(p.selectedTrackDirs().first(), first);                          // wraps round
    QCOMPARE(p.searchTracks(false, QStringLiteral("beta")), 1);
}

void TestTrakcratePanel::panelPreviewPlayer()
{
    writeWav(tmp_ / "House" / "a.wav", 300, "A one");
    writeWav(tmp_ / "House" / "b.wav", 330, "B two");
    writeWav(tmp_ / "Dub" / "c.wav", 350, "C three");
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    for (const char* f : {"House", "Dub"}) {
        QVERIFY(p.importFolder((tmp_ / f).string().c_str(), tc::ImportOptions()));
        QVERIFY(p.waitForJob());
    }
    QString err;
    QVERIFY(p.createPendrive(QStringLiteral("Fri"), {QStringLiteral("House")}, {QStringLiteral("Dub")}, &err));
    TrakcratePreviewPlayer* pv = p.preview();
    QTreeWidget* tree = p.findChild<QTreeWidget*>();
    QSignalSpy started(pv, &TrakcratePreviewPlayer::trackStarted);
    QSignalSpy stopped(pv, &TrakcratePreviewPlayer::stopped);

    // the tempo arithmetic: natural, or all at one BPM within what the stretcher can do
    QCOMPARE(TrakcratePreviewPlayer::tempoFor(120, 132), 1.1);
    QCOMPARE(TrakcratePreviewPlayer::tempoFor(0, 128), 1.0);
    QCOMPARE(TrakcratePreviewPlayer::tempoFor(60, 200), 2.0);
    QCOMPARE(TrakcratePreviewPlayer::tempoFor(200, 60), 0.5);

    // a folder is not played; the buttons say so
    p.selectFolder("House");
    QVERIFY(!pv->playButton()->isEnabled());
    QVERIFY(!p.startPreview(-1, &err));

    // a playlist plays one track after the other and the playing row follows
    p.selectPlaylist("House");
    QVERIFY(pv->playButton()->isEnabled());
    QTreeView* table = p.findChild<QTreeView*>();
    QVERIFY(p.startPreview(-1, &err));
    QVERIFY(pv->isActive());
    QCOMPARE(started.count(), 1);
    QCOMPARE(pv->currentIndex(), 0);
    const QString first = pv->currentDir();
    QCOMPARE(table->currentIndex().row(), 0);
    QVERIFY(pv->label()->text().contains("1/2"));
    QVERIFY(pv->stopButton()->isEnabled());
    emit pv->player()->finished();                                                      // the first track ended
    QCOMPARE(pv->currentIndex(), 1);
    QCOMPARE(table->currentIndex().row(), 1);                                           // the highlight and the details moved on
    QVERIFY(pv->currentDir() != first);
    emit pv->player()->finished();                                                      // the last one: the list is over
    QVERIFY(!pv->isActive());
    QCOMPARE(stopped.count(), 1);

    // "next playlist" is off by default; when ticked the next playlist follows and the list after the last one ends the player
    QVERIFY(!pv->continueBox()->isChecked());
    p.selectPlaylist("Dub");                                                            // playlists are in alphabetical order: Dub, House
    QVERIFY(p.startPreview(0, &err));
    emit pv->player()->finished();
    QVERIFY(!pv->isActive());                                                           // unticked: the end of Dub is the end
    pv->continueBox()->setChecked(true);
    QVERIFY(p.startPreview(0, &err));
    emit pv->player()->finished();
    QVERIFY(pv->isActive());                                                            // ticked: on to House
    QVERIFY(tree->currentItem()->data(0, Qt::UserRole).toString() == QLatin1String("playlist"));
    QCOMPARE(tree->currentItem()->text(0), QStringLiteral("House"));
    QCOMPARE(pv->currentIndex(), 0);
    emit pv->player()->finished();
    emit pv->player()->finished();                                                      // the end of the last playlist: nothing follows
    QVERIFY(!pv->isActive());
    pv->continueBox()->setChecked(false);
    p.selectPlaylist("House");

    // all at one BPM: a track with a grid is stretched, the audio on disk is not touched
    tc::Track t;
    QVERIFY(tc::loadTrack(first.toStdString(), &t));
    tc::BeatGrid g;
    g.bpm = 120;
    tc::setBeatGrid(t.grid, g);
    t.hasGrid = true;
    QVERIFY(tc::saveBeatGrid(t));
    std::string before, after;
    tc::readFile(tc::audioFileOf(first.toStdString()), &before);
    pv->setFixedBpm(true, 132);
    QVERIFY(pv->fixedBpm());
    QVERIFY(p.startPreview(0, &err));
    QCOMPARE(pv->currentTempo(), 1.1);
    pv->setFixedBpm(false);
    QCOMPARE(pv->currentTempo(), 1.0);                                                  // switching mode applies at once
    pv->stop();
    tc::readFile(tc::audioFileOf(first.toStdString()), &after);
    QVERIFY(before == after);
    QCOMPARE(tc::audioFileOf(first.toStdString()).filename().string(), tc::audioFileOf(first.toStdString()).filename().string());

    // a pendrive plays all its lists in order and the tree follows from list to list
    QTreeWidgetItem* drive = nullptr;
    for (int g2 = 0; g2 < tree->topLevelItemCount(); ++g2)
        for (int d = 0; d < tree->topLevelItem(g2)->childCount(); ++d)
            if (tree->topLevelItem(g2)->child(d)->data(0, Qt::UserRole).toString() == QLatin1String("drive")) drive = tree->topLevelItem(g2)->child(d);
    QVERIFY(drive);
    tree->setCurrentItem(drive);
    QVERIFY(pv->playButton()->isEnabled());
    started.clear();
    QVERIFY(p.startPreview(-1, &err));
    QVERIFY(tree->currentItem() != drive);                                              // the pendrive lists no tracks: the view moves to the list playing
    QVERIFY(tree->currentItem()->text(0).startsWith("House"));
    emit pv->player()->finished();
    emit pv->player()->finished();                                                      // into the second list (Dub)
    QCOMPARE(pv->currentIndex(), 2);
    QVERIFY(tree->currentItem() != drive);
    QVERIFY(tree->currentItem()->text(0).startsWith("Dub"));
    QCOMPARE(table->currentIndex().row(), 0);
    pv->stop();

    // not while on the Pro DJ Link network
    bool onNetwork = true;
    p.setPlaybackBlocker([&]() { return onNetwork ? QStringLiteral("connected") : QString(); });
    QVERIFY(!pv->playButton()->isEnabled());
    QVERIFY(!p.startPreview(-1, &err));
    QVERIFY(err.contains("connected"));
    QVERIFY(!pv->isActive());
    onNetwork = false;
    pv->refreshAllowed();
    QVERIFY(pv->playButton()->isEnabled());
    QVERIFY(p.startPreview(-1, &err));
    onNetwork = true;                                                                    // connecting stops it
    p.stopPreview();
    QVERIFY(!pv->isActive());
    QVERIFY(!pv->nextButton()->isEnabled());
}

void TestTrakcratePanel::panelSettingsSets()
{
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    QTreeWidget* tree = p.findChild<QTreeWidget*>();
    QVERIFY(tree);
    auto group = [&]() -> QTreeWidgetItem* {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) if (tree->topLevelItem(i)->text(0) == QLatin1String("User Settings")) return tree->topLevelItem(i);
        return nullptr;
    };
    QVERIFY(group());
    // no sets yet: a hint, not an error
    QCOMPARE(group()->childCount(), 1);
    QCOMPARE(group()->child(0)->data(0, Qt::UserRole).toString(), QStringLiteral("settings-hint"));

    QString err;
    QVERIFY2(p.createSettingsSet(QStringLiteral("Home"), QStringLiteral("Ana"), &err), qPrintable(err));
    QVERIFY(p.createSettingsSet(QStringLiteral("Club"), QStringLiteral("Bo"), &err));
    QVERIFY(!p.createSettingsSet(QStringLiteral("Club"), QStringLiteral("Bo"), &err));           // exists
    QVERIFY(!err.isEmpty());
    QVERIFY(!p.createSettingsSet(QStringLiteral("a/b"), QStringLiteral("Bo"), &err));
    QCOMPARE(group()->childCount(), 2);
    // sets in name order, the default (the first created) marked, the five files below each (set.txt is not shown)
    QCOMPARE(group()->child(0)->data(0, Qt::UserRole + 1).toString(), QStringLiteral("Club"));
    QCOMPARE(group()->child(0)->text(0), QStringLiteral("Club"));
    QCOMPARE(group()->child(1)->text(0), QStringLiteral("Home (default)"));
    QCOMPARE(group()->child(1)->data(0, Qt::UserRole).toString(), QStringLiteral("settings-set"));
    QCOMPARE(group()->child(1)->childCount(), 5);
    QCOMPARE(group()->child(1)->child(0)->data(0, Qt::UserRole).toString(), QStringLiteral("setting"));
    QCOMPARE(group()->child(1)->child(0)->data(0, Qt::UserRole + 3).toString(), QStringLiteral("Home"));

    // exportExt.pdb is not a leaf: its categories, each with its tags, are below it (My Tags)
    auto extItem = [&](const char* set) -> QTreeWidgetItem* {
        for (int i = 0; i < group()->childCount(); ++i) {
            QTreeWidgetItem* si = group()->child(i);
            if (si->data(0, Qt::UserRole + 1).toString() != QLatin1String(set)) continue;
            for (int k = 0; k < si->childCount(); ++k) if (si->child(k)->text(0) == QLatin1String("exportExt.pdb")) return si->child(k);
        }
        return nullptr;
    };
    QTreeWidgetItem* ext = extItem("Home");
    QVERIFY(ext);
    QCOMPARE(ext->childCount(), 4);
    QCOMPARE(ext->child(0)->text(0), QStringLiteral("Genre"));
    QCOMPARE(ext->child(0)->data(0, Qt::UserRole).toString(), QStringLiteral("mytag-category"));
    QCOMPARE(ext->child(0)->childCount(), 7);
    QCOMPARE(ext->child(0)->child(0)->text(0), QStringLiteral("Acid House"));
    QCOMPARE(ext->child(0)->child(0)->data(0, Qt::UserRole).toString(), QStringLiteral("mytag"));
    QCOMPARE(ext->child(0)->child(0)->childCount(), 0);
    QCOMPARE(ext->child(2)->text(0), QStringLiteral("Situation"));
    QCOMPARE(ext->child(2)->childCount(), 8);
    QVERIFY(ext->toolTip(0).contains(QStringLiteral("0 track link")));
    {   // a copy with a deleted tag row: removing the track links also cleans it, and the tree follows
        const tc::fs::path file = tc::settingsSetDir(p.library(), "Home") / "exportExt.pdb";
        std::string pdb;
        QVERIFY(tc::readFile(file, &pdb));
        tc::PdbFile f;
        QVERIFY(tc::parsePdb(pdb, &f));
        f.pages[8].rows[3].present = false;
        f.pages[8].u20 = uint16_t(f.pages[8].presentRows());
        QVERIFY(tc::serialisePdb(f, &pdb));
        QVERIFY(tc::atomicWrite(file, pdb));
        p.refresh();
        QCOMPARE(extItem("Home")->child(0)->childCount(), 6);
        int links = -1;
        QVERIFY(p.removeTrackLinks(QStringLiteral("Home"), &links, &err));
        QCOMPARE(links, 0);
        tc::ExtPdbStats st;
        QVERIFY(tc::readFile(file, &pdb) && tc::extPdbStats(pdb, &st));
        QCOMPARE(st.deletedTags, size_t(0));
        QCOMPARE(st.tags, size_t(27));
        QCOMPARE(extItem("Home")->child(0)->childCount(), 6);
        QVERIFY(!p.removeTrackLinks(QStringLiteral("Nope"), &links, &err));
    }

    QVERIFY(p.makeDefaultSettingsSet(QStringLiteral("Club"), &err));
    QCOMPARE(group()->child(0)->text(0), QStringLiteral("Club (default)"));
    QVERIFY(p.renameSettingsSet(QStringLiteral("Club"), QStringLiteral("Gig"), &err));
    QCOMPARE(group()->child(0)->text(0), QStringLiteral("Gig (default)"));
    QVERIFY(p.duplicateSettingsSet(QStringLiteral("Gig"), QStringLiteral("Gig 2"), &err));
    QCOMPARE(group()->childCount(), 3);
    QVERIFY(p.setSettingsUserName(QStringLiteral("Gig 2"), QStringLiteral("Cy"), &err));
    tc::DjProfile prof;
    std::string bytes;
    QVERIFY(tc::readFile(tc::settingsSetDir(p.library(), "Gig 2") / "djprofile.nxs", &bytes) && tc::parseDjprofile(bytes, &prof));
    QCOMPARE(prof.userName, std::string("Cy"));
    QVERIFY(!p.setSettingsUserName(QStringLiteral("Gig 2"), QString(), &err));
    QVERIFY(p.deleteSettingsSet(QStringLiteral("Gig"), &err));
    QVERIFY(!p.deleteSettingsSet(QStringLiteral("Gig"), &err));
    QCOMPARE(group()->childCount(), 2);
    // the default moved to a remaining set
    bool one = false;
    for (int i = 0; i < group()->childCount(); ++i) one = one || group()->child(i)->text(0).endsWith(QLatin1String("(default)"));
    QVERIFY(one);

    // copy from a rekordbox pendrive
    const tc::fs::path drive = tmp_ / "usb";
    tc::fs::create_directories(drive / "PIONEER");
    QVERIFY(tc::atomicWrite(drive / "PIONEER/DEVSETTING.DAT", tc::defaultDevSetting()));
    QVERIFY(!p.copySettingsSet(QStringLiteral("FromDeck"), QString::fromStdString((tmp_ / "empty").string()), &err));
    QVERIFY(p.copySettingsSet(QStringLiteral("FromDeck"), QString::fromStdString(drive.string()), &err));
    QCOMPARE(group()->childCount(), 3);
}

void TestTrakcratePanel::trakcrateOnlyWindow()
{
    TrakcrateWindow w;
    QCOMPARE(w.windowTitle(), QString::fromUtf8("\xF0\x9F\x93\xA6 trakcrate"));
    // only File and Config, with only what the trakcrate view offers
    QStringList menus, file, config;
    for (QAction* a : w.menuBar()->actions()) menus << a->text();
    QCOMPARE(menus, (QStringList{QStringLiteral("&File"), QStringLiteral("&Config")}));
    for (QAction* a : w.menuBar()->actions()) {
        QStringList& into = a->text() == QLatin1String("&File") ? file : config;
        for (QAction* x : a->menu()->actions()) if (!x->isSeparator()) into << x->text();
    }
    QCOMPARE(file, (QStringList{QStringLiteral("&Clone Pendrive..."), QStringLiteral("&Quit")}));
    QCOMPARE(config, (QStringList{QStringLiteral("&Quantize"), QStringLiteral("&Quit")}));
    QVERIFY(w.panel());
    QCOMPARE(w.centralWidget(), static_cast<QWidget*>(w.panel()));
    // the tree starts at twice the width of the dock in vdj-ui (170)
    w.show();
    QSplitter* sp = w.panel()->findChild<QSplitter*>();
    QVERIFY(sp);
    QTRY_VERIFY(sp->sizes().first() >= 300);
}

void TestTrakcratePanel::settingsViewer()
{
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    QVERIFY(!p.showSettingsViewer(QStringLiteral("Nope")));
    QVERIFY(p.createSettingsSet(QStringLiteral("Home"), QStringLiteral("Ana")));
    QVERIFY(p.createSettingsSet(QStringLiteral("Club"), QStringLiteral("Bo")));
    TrakcrateSettingsViewer* v = p.showSettingsViewer(QStringLiteral("Home"), QStringLiteral("djprofile.nxs"));
    QVERIFY(v);
    QCOMPARE(v->set(), QStringLiteral("Home"));
    QVERIFY(v->rowCount() > 3);
    QCOMPARE(v->differingRows(), 0);                                                    // no comparison
    v->setCompareSet(QStringLiteral("Club"));
    QCOMPARE(v->compareSet(), QStringLiteral("Club"));
    QCOMPARE(v->differingRows(), 1);                                                    // only the name differs
    v->setFile(QStringLiteral("MYSETTING.DAT"));
    QCOMPARE(v->differingRows(), 0);                                                    // both sets hold the built in file
    // the viewer changes nothing but the name
    v->setFile(QStringLiteral("djprofile.nxs"));
    QString err;
    QVERIFY2(v->saveUserName(&err), qPrintable(err));
    v->setCompareSet(QString());
    v->setSet(QStringLiteral("Club"));
    v->setCompareSet(QStringLiteral("Home"));
    QCOMPARE(v->differingRows(), 1);
    delete v;
}

void TestTrakcratePanel::pendriveSettingsLinkAndExportDialog()
{
    writeWav(tmp_ / "House" / "a.wav", 300);
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    QVERIFY(p.importFolder((tmp_ / "House").string().c_str(), tc::ImportOptions()));
    QVERIFY(p.waitForJob());
    QString err;
    QVERIFY(p.createSettingsSet(QStringLiteral("Home"), QStringLiteral("Ana")));
    QVERIFY(p.createSettingsSet(QStringLiteral("Club"), QStringLiteral("Bo")));
    QVERIFY(p.createPendrive(QStringLiteral("Fri"), {QStringLiteral("House")}, {}, &err));
    QVERIFY(!p.setPendriveSettings(QStringLiteral("Fri"), QStringLiteral("Nope"), &err));
    QVERIFY(!p.setPendriveSettings(QStringLiteral("Nope"), QStringLiteral("Club"), &err));
    QVERIFY(p.setPendriveSettings(QStringLiteral("Fri"), QStringLiteral("Club"), &err));
    QCOMPARE(tc::driveSettingsSet(p.library(), "Fri"), std::string("Club"));
    QVERIFY(!p.deleteSettingsSet(QStringLiteral("Club"), &err));                        // in use by a pendrive
    QVERIFY(err.contains(QStringLiteral("Fri")));

    TrakcrateExportDialog dlg(p.library());
    dlg.setPendrive(QStringLiteral("Fri"), false);
    dlg.setDestination(QString::fromStdString((tmp_ / "out").string()));
    QCOMPARE(dlg.settingsSet(), QStringLiteral("Club"));                                // preselected from the link
    QVERIFY(!dlg.raiseLimitsVisible());                                                 // the modern limits are only offered above the lower ones
    QVERIFY(!dlg.options().raiseLimits);
    dlg.setRaiseLimits(true);
    QVERIFY(dlg.options().raiseLimits);
    dlg.setRaiseLimits(false);
    tc::ExportOptions o = dlg.options();
    QCOMPARE(o.settingsSet, std::string("Club"));
    QVERIFY(!o.settingsBuiltIn);
    QVERIFY(o.settingsPolicy == tc::ExportOptions::SettingsPolicy::AddMissing);
    dlg.setSettingsSet(TrakcrateExportDialog::builtInSettingsKey());
    dlg.setReplaceSettings(true);
    o = dlg.options();
    QVERIFY(o.settingsBuiltIn);
    QVERIFY(o.settingsPolicy == tc::ExportOptions::SettingsPolicy::Replace);
    dlg.setSettingsSet(QString());
    QVERIFY(dlg.options().settingsSet.empty());                                         // the default set

    // an async dialog opens at once with "Loading...", Export disabled, and fills in from a worker thread
    {
        TrakcrateExportDialog adlg(p.library(), nullptr, true);
        adlg.setPendrive(QStringLiteral("Fri"), false);
        adlg.setDestination(QString::fromStdString((tmp_ / "out_async").string()));
        QPushButton* exportButton = nullptr;
        for (QPushButton* b : adlg.findChildren<QPushButton*>()) if (b->text() == QLatin1String("Export")) exportButton = b;
        QVERIFY(exportButton);
        QVERIFY(!exportButton->isEnabled());
        auto summaryText = [&adlg] {
            QString all;
            for (QLabel* l : adlg.findChildren<QLabel*>()) all += l->text();
            return all;
        };
        QVERIFY(summaryText().contains(QStringLiteral("Loading...")));
        QTRY_VERIFY_WITH_TIMEOUT(summaryText().contains(QStringLiteral("track(s)")), 10000);       // entries read, summary made
        QVERIFY(!summaryText().contains(QStringLiteral("Loading...")));
        QCOMPARE(adlg.options().lists.size(), size_t(1));                                         // the entries are ticked
    }

    // several drives: the ticked ones get the same export, started together; a drive that holds an export is updated
    {
        tc::fs::create_directories(tmp_ / "usb1");
        tc::fs::create_directories(tmp_ / "usb2");
        tc::fs::create_directories(tmp_ / "usb3/PIONEER");                                // an earlier export
        tc::fs::create_directories(tmp_ / "usb3/Contents");
        tc::fs::create_directories(tmp_ / "usb3/Contents/a");
        std::ofstream(tmp_ / "usb3/Contents/a/big.mp3") << std::string(1 << 20, 'x');            // measured once per dialog: made before it
        TrakcrateExportDialog mdlg(p.library(), nullptr, false);
        mdlg.setPendrive(QStringLiteral("Fri"), true);
        mdlg.setDestination(QString::fromStdString((tmp_ / "usb1").string()));
        QCOMPARE(mdlg.allOptions().size(), size_t(1));                                    // nothing ticked: the one drive
        mdlg.addExtraDrive(QString::fromStdString((tmp_ / "usb2").string()), QStringLiteral("usb2"));
        mdlg.addExtraDrive(QString::fromStdString((tmp_ / "usb3").string()), QStringLiteral("usb3"));
        mdlg.addExtraDrive(QString::fromStdString((tmp_ / "usb1").string()), QStringLiteral("usb1 again"));
        mdlg.selectExtraDrive(QString::fromStdString((tmp_ / "usb2").string()));
        mdlg.selectExtraDrive(QString::fromStdString((tmp_ / "usb3").string()));
        mdlg.selectExtraDrive(QString::fromStdString((tmp_ / "usb1").string()));          // the chosen destination itself is not written twice
        const std::vector<tc::ExportOptions> all = mdlg.allOptions();
        QCOMPARE(all.size(), size_t(3));
        QCOMPARE(all[0].dest, tmp_ / "usb1");
        QCOMPARE(all[1].dest, tmp_ / "usb2");
        QVERIFY(!all[1].update);
        QCOMPARE(all[2].dest, tmp_ / "usb3");
        QVERIFY(all[2].update);                                                           // it already holds an export
        QCOMPARE(all[1].lists.size(), all[0].lists.size());
        QVERIFY(!all[1].lists.empty());
        // the chosen destination is not offered as an additional one; the drives are offered in physical mode only
        QListWidget* extras = mdlg.findChild<QWidget*>(QStringLiteral("extraBox"))->findChild<QListWidget*>();
        for (int i = 0; i < extras->count(); ++i) {                                       // (real mounted drives may be listed too)
            const QString mp = extras->item(i)->data(Qt::UserRole).toString();
            if (mp == QString::fromStdString((tmp_ / "usb1").string())) {
                QVERIFY(extras->item(i)->isHidden());
                QCOMPARE(extras->item(i)->checkState(), Qt::Unchecked);
            } else if (mp == QString::fromStdString((tmp_ / "usb2").string())) {
                QVERIFY(!extras->item(i)->isHidden());
            }
        }
        QVERIFY(!mdlg.findChild<QWidget*>(QStringLiteral("physicalBox"))->isHidden());
        // the space table: what is still to be written is the estimate minus what the drive holds already, never negative
        {
            const std::vector<TrakcrateExportDialog::LocationSpace>& sp = mdlg.spaces();
            QVERIFY(sp.size() >= 3);
            QVERIFY(sp[0].currentKnown);
            QVERIFY(sp[0].requiredBytes > 0);
            const qint64 est = qint64(sp[0].requiredBytes);                                 // usb1 is empty: required = the whole estimate
            mdlg.selectExtraDrive(QString::fromStdString((tmp_ / "usb3").string()), false);
            mdlg.selectExtraDrive(QString::fromStdString((tmp_ / "usb3").string()), true);
            for (const auto& l : mdlg.spaces()) {
                QVERIFY(l.requiredBytes >= 0);
                if (l.base == QString::fromStdString((tmp_ / "usb3").string())) {
                    QVERIFY(l.currentBytes >= (1 << 20));
                    QCOMPARE(l.currentTracks, 1);
                    QVERIFY(l.requiredBytes <= std::max<qint64>(0, est - (1 << 20)) + 1);
                }
            }
        }
        // the table: a block per location, with sizes in GiB form, never a negative Required; Empty trash only where a trash folder exists
        {
            const QString usb1 = QString::fromStdString((tmp_ / "usb1").string());
            auto blockLabels = [&](const char* name) { QStringList v; for (QLabel* l : mdlg.findChildren<QLabel*>(QLatin1String(name))) v << l->text(); return v; };
            QCOMPARE(blockLabels("requiredValue").size(), int(mdlg.spaces().size()));
            for (const QString& r : blockLabels("requiredValue")) QVERIFY(!r.startsWith(QLatin1Char('-')));
            const QList<QCheckBox*> after = mdlg.findChildren<QCheckBox*>(QStringLiteral("cleanUpAfter"));
            QCOMPARE(after.size(), int(mdlg.spaces().size()));
            QCOMPARE(after[0]->toolTip(), QStringLiteral("Clean up after export"));
            QVERIFY(!mdlg.cleanUpAfterExport(usb1));
            after[0]->setChecked(true);
            QVERIFY(mdlg.cleanUpAfterExport(usb1));
            tc::fs::create_directories(tmp_ / "usb1" / (".Trash-" + std::to_string(getuid())) / "files");
            std::ofstream(tmp_ / "usb1" / (".Trash-" + std::to_string(getuid())) / "files/old.mp3") << "x";
            mdlg.selectExtraDrive(QString::fromStdString((tmp_ / "usb2").string()), false);
            mdlg.selectExtraDrive(QString::fromStdString((tmp_ / "usb2").string()), true);
            QCOMPARE(mdlg.findChildren<QPushButton*>(QStringLiteral("emptyTrash")).first()->isEnabled(), true);
            QCOMPARE(mdlg.findChildren<QPushButton*>(QStringLiteral("emptyTrash")).last()->isEnabled(), false);
        }
        // Empty trash: the contents go (a link inside is removed, not followed), the folder stays, the button goes off, a link as the trash is refused
        {
            const QString usb1 = QString::fromStdString((tmp_ / "usb1").string());
            const tc::fs::path trash = tmp_ / "usb1" / (".Trash-" + std::to_string(getuid()));
            tc::fs::create_directories(tmp_ / "outside");
            std::ofstream(tmp_ / "outside/keep") << "k";
            tc::fs::create_symlink(tmp_ / "outside", trash / "link");
            mdlg.setConfirm([](const QString& t) { return t.contains(QStringLiteral(".Trash-")); });
            QPushButton* et = mdlg.findChildren<QPushButton*>(QStringLiteral("emptyTrash")).first();
            QVERIFY(et->isEnabled());
            et->click();
            QTRY_VERIFY(!mdlg.deleting());
            QTRY_VERIFY(!et->isEnabled());
            QVERIFY(tc::fs::is_directory(trash));
            QVERIFY(tc::fs::is_empty(trash));
            QVERIFY(tc::fs::exists(tmp_ / "outside/keep"));
            tc::fs::remove(trash);
            tc::fs::create_directory_symlink(tmp_ / "outside", trash);
            QVERIFY(!TrakcrateExportDialog::emptyTrash(usb1));
            QVERIFY(tc::fs::exists(tmp_ / "outside/keep"));
            tc::fs::remove(trash);
        }
        // Delete really removes the exported data (and only that), links are not followed
        {
            const tc::fs::path d = tmp_ / "wipe";
            for (const char* sub : {"PIONEER/USBANLZ/P1", "PIONEER/rekordbox", "PIONEER/Artwork", "Contents/Artist", "Other"}) tc::fs::create_directories(d / sub);
            for (const char* f : {"PIONEER/USBANLZ/P1/ANLZ0000.DAT", "PIONEER/rekordbox/export.pdb", "PIONEER/Artwork/a.jpg", "Contents/Artist/t.mp3", "Other/mine.txt", "PIONEER/DEVSETTING.DAT"})
                std::ofstream(d / f) << "x";
            tc::fs::create_directory_symlink(tmp_ / "outside", d / "Contents/link");
            std::atomic<bool> none{false};
            QVERIFY(TrakcrateExportDialog::deleteRegenerable(QString::fromStdString(d.string()), none));
            QVERIFY(tc::fs::is_empty(d / "Contents"));
            QVERIFY(tc::fs::is_empty(d / "PIONEER/rekordbox"));
            QVERIFY(tc::fs::is_empty(d / "PIONEER/USBANLZ"));
            QVERIFY(tc::fs::is_empty(d / "PIONEER/Artwork"));
            QVERIFY(tc::fs::exists(d / "Other/mine.txt"));
            QVERIFY(tc::fs::exists(d / "PIONEER/DEVSETTING.DAT"));
            QVERIFY(tc::fs::exists(tmp_ / "outside/keep"));
            std::ofstream(d / "Contents/t2.mp3") << "x";
            std::atomic<bool> stop{true};
            QVERIFY(!TrakcrateExportDialog::deleteRegenerable(QString::fromStdString(d.string()), stop));
            QVERIFY(tc::fs::exists(d / "Contents/t2.mp3"));                                 // cancelled before anything was removed
        }
        // Delete: the question names both folders (and warns above 1 GiB); Export and the busy location's buttons are off until the worker is done
        {
            const QString t = TrakcrateExportDialog::deleteConfirmText(QStringLiteral("/m/usb"), 100);
            QVERIFY(t.contains(QStringLiteral("/m/usb/PIONEER")) && t.contains(QStringLiteral("/m/usb/Contents")));
            QVERIFY(!t.contains(QStringLiteral("This may take a while")));
            QVERIFY(TrakcrateExportDialog::deleteConfirmText(QStringLiteral("/m/usb"), qint64(2) << 30).contains(QStringLiteral("This may take a while")));
            auto exportEnabled = [&] { for (QPushButton* b : mdlg.findChildren<QPushButton*>()) if (b->text() == QLatin1String("Export")) return b->isEnabled(); return false; };
            for (QCheckBox* c : mdlg.findChildren<QCheckBox*>()) if (c->text().startsWith(QLatin1String("Allow tracks that are not analysed"))) c->setChecked(true);
            QMetaObject::invokeMethod(&mdlg, "refreshSummary");
            QVERIFY(exportEnabled());
            std::atomic<bool> release{false}, sawCancel{false};
            std::atomic<int> started{0};
            mdlg.setConfirm([](const QString&) { return true; });
            mdlg.setDeleter([&](const QString&, const std::atomic<bool>& cancel) {
                ++started;
                while (!release.load() && !cancel.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                if (cancel.load()) sawCancel = true;
            });
            QList<QPushButton*> dels = mdlg.findChildren<QPushButton*>(QStringLiteral("deleteData"));
            QVERIFY(dels.size() >= 2);
            dels[0]->click();
            QVERIFY(mdlg.deleting());
            QVERIFY(!exportEnabled());
            QVERIFY(!dels[0]->isEnabled());
            QVERIFY(dels[1]->isEnabled());                                                // another location can still be deleted from
            dels[1]->click();
            QTRY_COMPARE(started.load(), 2);
            release = true;
            QTRY_VERIFY(!mdlg.deleting());
            QTRY_VERIFY(exportEnabled());
            for (QPushButton* b : mdlg.findChildren<QPushButton*>(QStringLiteral("deleteData"))) QVERIFY(b->isEnabled());
            // closing the dialog stops a delete that is still going
            release = false;
            mdlg.findChildren<QPushButton*>(QStringLiteral("deleteData"))[0]->click();
            QTRY_COMPARE(started.load(), 3);
            mdlg.reject();
            QTRY_VERIFY(sawCancel.load());
            QTRY_VERIFY(!mdlg.deleting());
            mdlg.setDeleter([](const QString&, const std::atomic<bool>&) {});
        }
        TrakcrateExportDialog fdlg(p.library(), nullptr, false);
        fdlg.setPendrive(QStringLiteral("Fri"), false);
        QVERIFY(fdlg.findChild<QWidget*>(QStringLiteral("physicalBox"))->isHidden());
        bool titled = false;
        for (QRadioButton* r : fdlg.findChildren<QRadioButton*>()) titled |= r->text() == QLatin1String("Overwrite All User Settings");
        QVERIFY(titled);
    }

    {
        tc::fs::create_directories(tmp_ / "du/PIONEER");
        tc::fs::create_directories(tmp_ / "du/Contents/x");
        std::ofstream(tmp_ / "du/PIONEER/f") << std::string(1000, 'a');
        std::ofstream(tmp_ / "du/Contents/x/t.mp3") << std::string(2000, 'b');
        const DirUsage u = measureRekordbox(QString::fromStdString((tmp_ / "du").string()));
        QVERIFY(u.ok);
        QCOMPARE(u.bytes, qint64(3000));
        QCOMPARE(u.audioFiles, 1);
        tc::fs::create_directories(tmp_ / "du_empty");
        const DirUsage e = measureRekordbox(QString::fromStdString((tmp_ / "du_empty").string()));
        QVERIFY(!e.ok);
        QCOMPARE(e.bytes, qint64(0));
        std::atomic<bool> stop{true};
        QVERIFY(measureRekordbox(QString::fromStdString((tmp_ / "du").string()), &stop).bytes <= 3000);
    }

    // exporting the linked pendrive puts that set's profile on the drive
    dlg.setSettingsSet(QStringLiteral("Club"));
    dlg.setReplaceSettings(false);
    o = dlg.options();
    o.allowUnanalysed = true;
    QVERIFY(p.exportDrive(o));
    QVERIFY(p.waitForJob(30000));
    std::string bytes;
    tc::DjProfile prof;
    QVERIFY(tc::readFile(tmp_ / "out/PIONEER/djprofile.nxs", &bytes) && tc::parseDjprofile(bytes, &prof));
    QCOMPARE(prof.userName, std::string("Bo"));
    QVERIFY(p.setPendriveSettings(QStringLiteral("Fri"), QString(), &err));             // unlink, now the set can go
    QVERIFY(p.deleteSettingsSet(QStringLiteral("Club"), &err));
}

void TestTrakcratePanel::panelPendrives()
{
    writeWav(tmp_ / "House" / "a.wav", 300);
    writeWav(tmp_ / "House" / "b.wav", 330);
    writeWav(tmp_ / "Dub" / "c.wav", 350);
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    for (const char* f : {"House", "Dub"}) {
        QVERIFY(p.importFolder((tmp_ / f).string().c_str(), tc::ImportOptions()));
        QVERIFY(p.waitForJob());
    }
    QString err;
    QVERIFY(!p.createPendrive(QStringLiteral("a/b"), {}, {}, &err));
    QVERIFY(!p.createPendrive(QStringLiteral("Fri"), {QStringLiteral("Nothing")}, {}, &err));
    QVERIFY(p.library().listDrives().empty());                                          // a failed create leaves no half made pendrive
    QVERIFY(p.createPendrive(QStringLiteral("Fri"), {QStringLiteral("House")}, {QStringLiteral("Dub")}, &err));
    QVERIFY(!p.createPendrive(QStringLiteral("Fri"), {}, {}, &err));                    // exists
    QVERIFY(p.addToPendrive(QStringLiteral("Fri"), QStringLiteral("folder"), QStringLiteral("Dub"), &err));
    QVERIFY(!p.addToPendrive(QStringLiteral("Nope"), QStringLiteral("folder"), QStringLiteral("Dub"), &err));
    const auto es = p.library().driveEntries("Fri");
    QCOMPARE(es.size(), size_t(3));
    QCOMPARE(es[0].name, std::string("House"));

    // the tree has the pendrive with its entries, and an entry shows its tracks
    QTreeWidget* tree = p.findChild<QTreeWidget*>();
    QVERIFY(tree);
    QTreeWidgetItem* group = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) if (tree->topLevelItem(i)->text(0) == QLatin1String("Virtual Pendrives")) group = tree->topLevelItem(i);
    QVERIFY(group);
    QCOMPARE(group->childCount(), 1);
    QCOMPARE(group->child(0)->childCount(), 3);
    tree->setCurrentItem(group->child(0)->child(0));
    QCOMPARE(p.visibleTrackCount(), 2);                                                 // House has two tracks
    tree->setCurrentItem(group->child(0));
    QCOMPARE(p.visibleTrackCount(), 0);                                                 // a pendrive itself lists no tracks

    // deleting a folder would break the pendrive: the warning data says so
    const auto im = p.folderImpact(QStringLiteral("Dub"));
    QVERIFY(im.any());
    QCOMPARE(im.driveEntries.size(), size_t(2));
    QVERIFY(!p.folderImpact(QStringLiteral("Nothing")).any());
    QString e2;
    QVERIFY(!p.deleteFolder(QStringLiteral("../x"), &e2));
}

void TestTrakcratePanel::usbLabels()
{
    QCOMPARE(tc::usbLabel(0), std::string());
    QCOMPARE(tc::usbLabel(1.5), std::string("USB 1.1"));
    QCOMPARE(tc::usbLabel(12), std::string("USB 1.1"));
    QCOMPARE(tc::usbLabel(480), std::string("USB 2.0"));
    QCOMPARE(tc::usbLabel(5000), std::string("USB 3.0"));
    QCOMPARE(tc::usbLabel(10000), std::string("USB 3.2"));
    QCOMPARE(tc::usbLabel(20000), std::string("USB 3.2"));
    // a fake sysfs: partition below disk below the USB device (speed + idVendor), with an interface directory in between
    const tc::fs::path dev = tmp_ / "usb1" / "1-1.4" / "1-1.4.1";
    tc::fs::create_directories(dev / "1-1.4.1:1.0" / "host6" / "block" / "sda" / "sda1");
    std::ofstream(dev / "speed") << "480\n";
    std::ofstream(dev / "idVendor") << "0951\n";
    std::ofstream(dev / "product") << "DataTraveler 3.0\n";
    tc::UsbLink l = tc::usbLinkFromSysfs(dev / "1-1.4.1:1.0" / "host6" / "block" / "sda" / "sda1");
    QVERIFY(l.found);
    QCOMPARE(l.label, std::string("USB 2.0"));
    QCOMPARE(l.product, std::string("DataTraveler 3.0"));
    QVERIFY(!l.superSpeedCapable);
    QVERIFY(!l.downgraded());
    // the BOS descriptor of the real stick (USB 2.0 extension + SuperSpeed capability) and of a plain USB 2 device (extension only)
    const std::string ss("\x05\x0f\x16\x00\x02\x07\x10\x02\x06\x00\x00\x00\x0a\x10\x03\x00\x0e\x00\x02\x0a\xff\x07", 22);
    const std::string hs("\x05\x0f\x0c\x00\x01\x07\x10\x02\x06\x00\x00\x00", 12);
    QVERIFY(tc::bosHasSuperSpeed(ss));
    QVERIFY(!tc::bosHasSuperSpeed(hs));
    QVERIFY(!tc::bosHasSuperSpeed(std::string()));
    QVERIFY(!tc::bosHasSuperSpeed(std::string("\x05\x0f\xff\xff\x01\x00\x10\x03", 8)));     // damaged length: no crash, no match
    std::ofstream(dev / "bos_descriptors", std::ios::binary).write(ss.data(), long(ss.size()));
    l = tc::usbLinkFromSysfs(dev / "1-1.4.1:1.0");
    QVERIFY(l.superSpeedCapable);
    QVERIFY(l.downgraded());                                                             // USB 3 device on a 480 Mbit/s link
    QVERIFY(!tc::usbLinkFromSysfs(tmp_ / "nothing" / "here").found);
    QVERIFY(!tc::usbLinkFromSysfs(tmp_).found);                                          // no USB device above it
    QVERIFY(!tc::usbLinkOfPath(tmp_ / "does-not-exist").found);
}

void TestTrakcratePanel::bpmHelpers()
{
    std::vector<double> beats;
    for (int i = 0; i < 40; ++i) beats.push_back(0.13 + i * 60.0 / 128.0);
    QVERIFY(std::fabs(tc::bpmFromBeats(beats) - 128.0) < 0.01);
    QVERIFY(std::llabs(tc::firstBeatFromBeats(beats, 128.0) - 130000) < 2000);
    QCOMPARE(tc::bpmFromBeats(std::vector<double>{0.1, 0.6}), 0.0);
}

void TestTrakcratePanel::panelAppliesBpm()
{
    writeWav(tmp_ / "Bp" / "a.wav", 300);
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    QVERIFY(p.importFolder((tmp_ / "Bp").string().c_str(), tc::ImportOptions()));
    QVERIFY(p.waitForJob());
    const QString dir = QString::fromStdString(p.library().trackDir("Bp", "a").string());
    QVERIFY(!p.applyBpm(dir, 0, 0, false));
    QVERIFY(p.applyBpm(dir, 127.5, 250000, false));                  // no grid yet: created
    tc::Track t;
    QVERIFY(tc::loadTrack(dir.toStdString(), &t));
    QVERIFY(t.hasGrid);
    QCOMPARE(tc::beatGridFrom(t.grid).bpm, 127.5);
    QCOMPARE(tc::beatGridFrom(t.grid).firstBeatUs, 250000LL);
    QCOMPARE(tc::origin(t.source, "beatgrid"), std::string("edited"));
    QVERIFY(p.applyBpm(dir, 90, 0, false));                          // a grid exists: left alone
    QVERIFY(tc::loadTrack(dir.toStdString(), &t));
    QCOMPARE(tc::beatGridFrom(t.grid).bpm, 127.5);
    QVERIFY(p.applyBpm(dir, 90, 0, true));                           // unless the user asks
    QVERIFY(tc::loadTrack(dir.toStdString(), &t));
    QCOMPARE(tc::beatGridFrom(t.grid).bpm, 90.0);
}

void TestTrakcratePanel::panelDetectsBpmAndAsks()
{
    // a 120 BPM click track so aubio has something to find
    const int rate = 44100, n = rate * 20;
    std::string pcm(size_t(n) * 2, '\0');
    for (int beat = 0; beat * rate / 2 < n; ++beat)
        for (int i = 0; i < 400 && beat * rate / 2 + i < n; ++i) {
            const int16_t s = int16_t(28000.0 * std::exp(-i / 60.0) * std::sin(i * 0.5));
            std::memcpy(&pcm[size_t(beat * rate / 2 + i) * 2], &s, 2);
        }
    tc::fs::create_directories(tmp_ / "Click");
    {
        std::ofstream f(tmp_ / "Click" / "click.wav", std::ios::binary);
        std::string h = "RIFF";
        put32(h, uint32_t(36 + pcm.size())); h += "WAVEfmt ";
        put32(h, 16); put16(h, 1); put16(h, 1); put32(h, rate); put32(h, rate * 2); put16(h, 2); put16(h, 16);
        h += "data"; put32(h, uint32_t(pcm.size()));
        f << h << pcm;
    }
    TrakcratePanel p;
    QVERIFY(p.createLibrary((tmp_ / "lib").string().c_str()));
    QVERIFY(p.importFolder((tmp_ / "Click").string().c_str(), tc::ImportOptions()));
    QVERIFY(p.waitForJob());
    const QString dir = QString::fromStdString(p.library().trackDir("Click", "click").string());
    double offered = 0;
    p.setBpmPrompt([&](const QString&, const tc::BpmDetection& f, double* bpm, bool*) { offered = f.bpm; *bpm = f.bpm * 2; return true; });
    QSignalSpy done(&p, &TrakcratePanel::bpmFinished);
    QVERIFY(p.detectBpm(dir));
    QVERIFY(!p.detectBpm(dir));                                      // one job at a time
    QVERIFY(p.waitForJob());
    QTRY_COMPARE(done.count(), 1);
    if (!tc::bpmDetectAvailable()) {
        QVERIFY(!done.at(0).at(1).toBool());                         // failed cleanly, nothing written
        return;
    }
    QVERIFY(done.at(0).at(1).toBool());
    QVERIFY(offered > 55 && offered < 130);                          // 120, or 60 if aubio is an octave out
    tc::Track t;
    QVERIFY(tc::loadTrack(dir.toStdString(), &t));
    QVERIFY(t.hasGrid);
    QVERIFY(std::fabs(tc::beatGridFrom(t.grid).bpm - offered * 2) < 0.01);
}

QTEST_MAIN(TestTrakcratePanel)
#include "test_trakcrate_panel.moc"
