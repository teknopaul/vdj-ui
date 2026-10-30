#include <QtTest/QtTest>

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSignalSpy>
#include <QTreeView>
#include <QTreeWidget>

#include "ui/trakcrate_job.h"
#include "ui/trakcrate_panel.h"
#include "ui/trakcrate_preview_player.h"
#include "audio/audio_player.h"
#include "trakcrate/exporter.h"
#include "trakcrate/usb_speed.h"
#include <QFile>
#include "ui/trakcrate_track_model.h"

#include <cmath>
#include <cstring>
#include <fstream>

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
    void panelImportsARekordboxDrive();
    void panelPendrives();
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
