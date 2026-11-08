#include <QtTest/QtTest>
#include "ui/track_browser.h"
#include "ui/waveform_delegate.h"
#include "ui/waveform_widget.h"
#include "ui/waveform_window.h"
#include <QLabel>
#include <QScrollBar>
#include <QWheelEvent>
#include <QHeaderView>
#include <QImage>
#include <QDir>
#include <QFileInfo>
#include <QTreeWidget>
#include <QTemporaryDir>
#include "export/playlist_export.h"
#include <QFile>
#include <cdj/pdb.h>

// Folder holding a real rekordbox export (Contents + PIONEER/rekordbox/export.pdb).
// Defaults to the CMake option VDJ_TEST_REKORDBOX_DIR; override at run time with the
// VDJ_TEST_REKORDBOX_DIR environment variable. Tests that need it skip when it is absent.
#ifndef VDJ_TEST_REKORDBOX_DIR
#define VDJ_TEST_REKORDBOX_DIR "/var/tmp/rekordbox-example"
#endif

static QString testDir()
{
    const QByteArray env = qgetenv("VDJ_TEST_REKORDBOX_DIR");
    return env.isEmpty() ? QString::fromLatin1(VDJ_TEST_REKORDBOX_DIR) : QString::fromLocal8Bit(env);
}

static QTreeWidgetItem* childNamed(QTreeWidgetItem* parent, const QString& name)
{
    for (int i = 0; i < parent->childCount(); ++i)
        if (parent->child(i)->text(0) == name) return parent->child(i);
    return nullptr;
}

static int countCb(const pdb_track_t*, void* ud) { ++*static_cast<int*>(ud); return 0; }

class TestRekordbox : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void rejectsFolderWithoutContents();
    void contentsOnlyFolder();
    void pdbTrackCountMatchesTree();
    void topLevelGroups();
    void playlistsHaveTracks();
    void trackRowsAreFilled();
    void trackPathsExistOnDisk();
    void doubleClickEmitsDbMetadata();
    void selectingTrackShowsDetails();
    void artworkResolvesFromDb();
    void noSelectionClearsDetails();
    void exportCopiesFilesAndWritesOrder();
    void exportHandlesDuplicatesClashesAndMissing();
    void exportRefusesNonEmptyDir();
    void exportCancel();
    void realPlaylistExport();
    void columnsAreTrackArtistBpmWaveform();
    void miniWaveformDelegateLoadsLazily();
    void miniWaveformIsPainted();
    void detailsShowAnalysis();
    void detailsShowCuesAndLoops();
    void waveformWidgetPaints();
    void noAnalysisForContentsOnlyFolder();
    void analysisHasTheBeatGrid();
    void beatLinesAreWhiteAndBarsRedInTheBottomHalf();
    void denseBeatsAreThinned();
    void pickupBeatsBelongToBarZero();
    void viewZoomAndScroll();
    void waveformWindowZoomsPansAndScrolls();
    void waveformWindowOpensFromDetails();

private:
    bool open(vdj::TrackBrowser& b) { QString e; return b.openRekordboxFolder(dir_, &e); }

    QString dir_;
    bool haveDb_ = false;
};

void TestRekordbox::initTestCase()
{
    dir_ = testDir();
    haveDb_ = QFileInfo::exists(dir_ + "/PIONEER/rekordbox/export.pdb")
              && QDir(dir_ + "/Contents").exists();
    qInfo() << "rekordbox test dir:" << dir_ << (haveDb_ ? "(found)" : "(missing, db tests skipped)");
}

void TestRekordbox::rejectsFolderWithoutContents()
{
    QTemporaryDir tmp;
    vdj::TrackBrowser b;
    QString err;
    QVERIFY(!b.openRekordboxFolder(tmp.path(), &err));
    QVERIFY(!err.isEmpty());
}

void TestRekordbox::contentsOnlyFolder()
{
    QTemporaryDir tmp;
    QVERIFY(QDir(tmp.path()).mkpath("Contents/Artist/Album"));
    QFile f(tmp.path() + "/Contents/Artist/Album/one.mp3");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();

    vdj::TrackBrowser b;
    QVERIFY(b.openRekordboxFolder(tmp.path()));
    QVERIFY(!b.hasPdb());
    QCOMPARE(b.treeWidget()->topLevelItemCount(), 1);
}

void TestRekordbox::pdbTrackCountMatchesTree()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    QVERIFY(b.hasPdb());

    int expected = 0;
    QCOMPARE(pdb_parse_file((dir_ + "/PIONEER/rekordbox/export.pdb").toLocal8Bit().constData(),
                            countCb, &expected), 0);
    QVERIFY(expected > 0);

    auto* all = childNamed(b.treeWidget()->topLevelItem(0), "All Tracks");
    QVERIFY(all);
    QCOMPARE(all->childCount(), expected);
}

void TestRekordbox::topLevelGroups()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* top = b.treeWidget()->topLevelItem(0);
    for (const char* name : { "Playlists", "Artists", "Albums", "Genres", "All Tracks" })
        QVERIFY2(childNamed(top, name), name);
    QVERIFY(childNamed(top, "Artists")->childCount() > 0);
}

void TestRekordbox::playlistsHaveTracks()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* playlists = childNamed(b.treeWidget()->topLevelItem(0), "Playlists");
    QVERIFY(playlists && playlists->childCount() > 0);
    int withTracks = 0;
    for (int i = 0; i < playlists->childCount(); ++i)
        if (playlists->child(i)->childCount() > 0) ++withTracks;
    QVERIFY(withTracks > 0);
}

void TestRekordbox::trackRowsAreFilled()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* all = childNamed(b.treeWidget()->topLevelItem(0), "All Tracks");
    QVERIFY(all);
    int titles = 0, artists = 0, bpms = 0, keys = 0;
    for (int i = 0; i < all->childCount(); ++i) {
        auto* t = all->child(i);
        titles += !t->text(0).isEmpty();
        artists += !t->text(1).isEmpty();
        keys += !t->data(0, Qt::UserRole + 1).toStringList().value(2).isEmpty();
        bpms += !t->text(2).isEmpty();
    }
    const int n = all->childCount();
    QCOMPARE(titles, n);
    // Most tracks should resolve their artist, key and BPM; allow a few untagged ones.
    QVERIFY2(artists > n * 8 / 10, qPrintable(QString("artists %1/%2").arg(artists).arg(n)));
    QVERIFY2(keys > n * 8 / 10, qPrintable(QString("keys %1/%2").arg(keys).arg(n)));
    QVERIFY2(bpms > n * 8 / 10, qPrintable(QString("bpms %1/%2").arg(bpms).arg(n)));
}

void TestRekordbox::trackPathsExistOnDisk()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* all = childNamed(b.treeWidget()->topLevelItem(0), "All Tracks");
    QVERIFY(all);
    int missing = 0;
    for (int i = 0; i < all->childCount(); ++i)
        if (!QFileInfo::exists(all->child(i)->data(0, Qt::UserRole).toString())) ++missing;
    QVERIFY2(missing == 0, qPrintable(QString("%1 of %2 audio files missing").arg(missing).arg(all->childCount())));
}

void TestRekordbox::doubleClickEmitsDbMetadata()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    b.setPlayerId(3);
    auto* all = childNamed(b.treeWidget()->topLevelItem(0), "All Tracks");
    QVERIFY(all && all->childCount() > 0);

    QSignalSpy spy(&b, &vdj::TrackBrowser::trackSelected);
    auto* t = all->child(0);
    emit b.treeWidget()->itemDoubleClicked(t, 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 3);
    QCOMPARE(spy.at(0).at(1).toString(), t->text(0));
    QCOMPARE(spy.at(0).at(2).toString(), t->text(1));
}

static QString field(const vdj::TrackInfo& ti, const QString& label)
{
    for (const auto& kv : ti.fields) if (kv.first == label) return kv.second;
    return QString();
}

void TestRekordbox::selectingTrackShowsDetails()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* all = childNamed(b.treeWidget()->topLevelItem(0), "All Tracks");
    QVERIFY(all && all->childCount() > 0);

    auto* t = all->child(0);
    b.treeWidget()->setCurrentItem(t);
    const vdj::TrackInfo& ti = b.detailsWidget()->info();
    QCOMPARE(ti.title, t->text(0));
    QCOMPARE(field(ti, "Artist"), t->text(1));
    QCOMPARE(field(ti, "Key"), t->data(0, Qt::UserRole + 1).toStringList().value(2));
    QVERIFY(!field(ti, "File").isEmpty());
    QVERIFY(!field(ti, "Track ID").isEmpty());
    QVERIFY(!field(ti, "Duration").isEmpty());
}

void TestRekordbox::artworkResolvesFromDb()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* all = childNamed(b.treeWidget()->topLevelItem(0), "All Tracks");
    QVERIFY(all);

    // artwork_id -> artwork table path (/PIONEER/Artwork/NNNNN/aN.jpg) -> file on disk
    int withArt = 0, loaded = 0;
    const int n = qMin(all->childCount(), 200);
    for (int i = 0; i < n; ++i) {
        b.treeWidget()->setCurrentItem(all->child(i));
        const auto& ti = b.detailsWidget()->info();
        if (ti.artworkPath.isEmpty()) continue;
        ++withArt;
        QVERIFY2(ti.artworkPath.contains("/PIONEER/Artwork/"), qPrintable(ti.artworkPath));
        QVERIFY(QFileInfo::exists(ti.artworkPath));
        if (b.detailsWidget()->hasArtwork()) ++loaded;
    }
    QVERIFY2(withArt > n / 2, qPrintable(QString("only %1/%2 tracks have artwork").arg(withArt).arg(n)));
    QCOMPARE(loaded, withArt);
}

void TestRekordbox::noSelectionClearsDetails()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* all = childNamed(b.treeWidget()->topLevelItem(0), "All Tracks");
    b.treeWidget()->setCurrentItem(all->child(0));
    QVERIFY(!b.detailsWidget()->info().title.isEmpty());
    b.treeWidget()->setCurrentItem(all);   // a group row, not a track
    QVERIFY(b.detailsWidget()->info().title.isEmpty());
    QVERIFY(!b.detailsWidget()->hasArtwork());
}

static QString makeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(data);
    return path;
}

static QStringList readLines(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList l = QString::fromUtf8(f.readAll()).split('\n');
    if (!l.isEmpty() && l.last().isEmpty()) l.removeLast();
    return l;
}

void TestRekordbox::exportCopiesFilesAndWritesOrder()
{
    QTemporaryDir src, dst;
    const QString a = makeFile(src.path() + "/A/one.mp3", "1");
    const QString b = makeFile(src.path() + "/B/two.mp3", "22");
    const QString c = makeFile(src.path() + "/C/Ünï.mp3", "333");
    // playlist order differs from creation/alphabetical order
    const auto r = vdj::exportPlaylist(dst.path(), "My Set", { { c, "", "" }, { a, "", "" }, { b, "", "" } });
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.copied, 3);
    QCOMPARE(r.listFile, dst.path() + "/My Set.txt");
    QCOMPARE(readLines(r.listFile), QStringList({ "Ünï.mp3", "one.mp3", "two.mp3" }));
    QCOMPARE(QFileInfo(dst.path() + "/two.mp3").size(), qint64(2));
}

void TestRekordbox::exportHandlesDuplicatesClashesAndMissing()
{
    QTemporaryDir src, dst;
    const QString a = makeFile(src.path() + "/X/song.mp3", "x");
    const QString b = makeFile(src.path() + "/Y/song.mp3", "yy");   // same file name, different file
    const auto r = vdj::exportPlaylist(dst.path(), "a/b:c", { { a, "", "" }, { b, "", "" }, { a, "", "" },
                                                              { src.path() + "/gone.mp3", "", "" } });
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.copied, 2);                                   // repeated track copied once
    QCOMPARE(r.missing.size(), 1);
    QCOMPARE(r.files, QStringList({ "song.mp3", "song (2).mp3", "song.mp3" }));
    QCOMPARE(QFileInfo(dst.path() + "/song (2).mp3").size(), qint64(2));
    QCOMPARE(readLines(r.listFile), r.files);                // missing file is not listed
    QCOMPARE(QFileInfo(r.listFile).fileName(), QString("a_b_c.txt"));
}

void TestRekordbox::exportRefusesNonEmptyDir()
{
    QTemporaryDir src, dst;
    const QString a = makeFile(src.path() + "/one.mp3", "1");
    makeFile(dst.path() + "/existing.txt", "keep");
    const auto r = vdj::exportPlaylist(dst.path(), "x", { { a, "", "" } });
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
    QCOMPARE(QDir(dst.path()).entryList(QDir::Files).size(), 1);   // nothing added
    QVERIFY(!vdj::exportPlaylist(dst.path() + "/nope", "x", {}).ok);
}

void TestRekordbox::exportCancel()
{
    QTemporaryDir src, dst;
    const QString a = makeFile(src.path() + "/one.mp3", "1");
    const QString b = makeFile(src.path() + "/two.mp3", "2");
    const auto r = vdj::exportPlaylist(dst.path(), "x", { { a, "", "" }, { b, "", "" } },
                                       [](int done, int) { return done < 1; });
    QVERIFY(!r.ok);
    QCOMPARE(r.error, QString("Cancelled"));
}

void TestRekordbox::realPlaylistExport()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* playlists = childNamed(b.treeWidget()->topLevelItem(0), "Playlists");
    QVERIFY(playlists);

    // The "Playlists" group itself is not exportable, a playlist is.
    QVERIFY(b.playlistTracks(playlists).isEmpty());
    auto* pl = playlists->child(0);
    QVERIFY(pl);
    const auto tracks = b.playlistTracks(pl);
    QCOMPARE(tracks.size(), pl->childCount());
    QVERIFY(!tracks.isEmpty());
    for (int i = 0; i < pl->childCount(); ++i)   // same order as shown in the tree
        QCOMPARE(tracks[i].title, pl->child(i)->text(0));

    QTemporaryDir dst;
    const auto r = vdj::exportPlaylist(dst.path(), pl->text(0), tracks);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.files.size(), tracks.size() - r.missing.size());
    QVERIFY(r.copied > 0);
    QCOMPARE(readLines(r.listFile), r.files);
    for (const QString& f : r.files)
        QVERIFY2(QFileInfo::exists(dst.path() + "/" + f), qPrintable(f));
}

void TestRekordbox::columnsAreTrackArtistBpmWaveform()
{
    vdj::TrackBrowser b;
    const auto* h = b.treeWidget()->headerItem();
    QCOMPARE(h->text(0), QString("Track"));
    QCOMPARE(h->text(1), QString("Artist"));
    QCOMPARE(h->text(2), QString("BPM"));
    QCOMPARE(h->text(vdj::TrackBrowser::kWaveformColumn), QString("Waveform"));
    for (int c = 0; c < b.treeWidget()->columnCount(); ++c)
        QVERIFY2(h->text(c) != "Key", "the Key column was replaced by the waveform");
    QVERIFY(b.treeWidget()->itemDelegateForColumn(vdj::TrackBrowser::kWaveformColumn) == b.miniWaveformDelegate());
}

void TestRekordbox::miniWaveformDelegateLoadsLazily()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* d = b.miniWaveformDelegate();
    QCOMPARE(d->cachedCount(), 0);                       // nothing is read when the database is opened

    auto* all = childNamed(b.treeWidget()->topLevelItem(0), "All Tracks");
    QVERIFY(all && all->childCount() > 10);
    int withWave = 0;
    for (int i = 0; i < 20; ++i) {
        const QString path = all->child(i)->data(0, vdj::MiniWaveformDelegate::kAnalyzePathRole).toString();
        QVERIFY2(path.startsWith("/PIONEER/USBANLZ/"), qPrintable(path));
        const auto& cols = d->columnsFor(path);
        if (cols.isEmpty()) continue;
        ++withWave;
        QCOMPARE(cols.size(), 100);                      // PWV2 has 100 columns
        for (quint8 h : cols) QVERIFY(h <= 15);
    }
    QVERIFY(withWave >= 15);
    QCOMPARE(d->cachedCount(), 20);
    d->columnsFor(all->child(0)->data(0, vdj::MiniWaveformDelegate::kAnalyzePathRole).toString());
    QCOMPARE(d->cachedCount(), 20);                      // second ask is served from the cache

    QVERIFY(d->columnsFor("/PIONEER/USBANLZ/PFFF/00000000/ANLZ0000.DAT").isEmpty());   // unknown track
    QVERIFY(d->columnsFor("/../etc/passwd.DAT").isEmpty());                              // rejected path
    QVERIFY(open(b));
    QCOMPARE(d->cachedCount(), 0);                       // reopening clears the cache
}

void TestRekordbox::miniWaveformIsPainted()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    b.resize(520, 700);
    auto* tree = b.treeWidget();
    auto* all = childNamed(tree->topLevelItem(0), "All Tracks");
    QVERIFY(all);
    all->setExpanded(true);
    b.show();
    tree->scrollToItem(all->child(5), QAbstractItemView::PositionAtTop);

    const QImage img = tree->viewport()->grab().toImage();
    const QRect col(tree->header()->sectionViewportPosition(vdj::TrackBrowser::kWaveformColumn), 0,
                    tree->header()->sectionSize(vdj::TrackBrowser::kWaveformColumn), img.height());
    QVERIFY(col.right() < img.width());
    int blue = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = col.left(); x <= col.right(); ++x)
            if (img.pixelColor(x, y) == QColor(70, 160, 255)) ++blue;
    QVERIFY2(blue > 200, qPrintable(QString("only %1 waveform pixels painted").arg(blue)));

    // the other columns carry no waveform pixels
    int blueElsewhere = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < col.left() - 1; ++x)
            if (img.pixelColor(x, y) == QColor(70, 160, 255)) ++blueElsewhere;
    QCOMPARE(blueElsewhere, 0);
    QVERIFY(b.miniWaveformDelegate()->cachedCount() > 0);
    QVERIFY(b.miniWaveformDelegate()->cachedCount() < 100);   // only the visible rows were read
}

void TestRekordbox::detailsShowAnalysis()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* all = childNamed(b.treeWidget()->topLevelItem(0), "All Tracks");
    QVERIFY(all);

    int analysed = 0;
    for (int i = 0; i < 40; ++i) {
        b.treeWidget()->setCurrentItem(all->child(i));
        const auto* d = b.detailsWidget();
        QVERIFY2(d->analysis().valid, qPrintable(all->child(i)->text(0)));
        QVERIFY(d->waveformWidget()->hasWaveform());
        QCOMPARE(d->analysis().mini.size(), 100);
        QVERIFY(d->analysis().color.size() > 100);
        if (d->analysis().beatCount == 0) continue;     // a track rekordbox has not analysed
        ++analysed;
        // BPM column and the beat grid agree to a BPM
        QVERIFY(qAbs(d->analysis().avgBpm - all->child(i)->text(2).toDouble()) < 1.0);
        // detail waveform length (150/s) is the track length
        const double secs = d->analysis().lengthMs() / 1000.0;
        QVERIFY(secs > 5 && secs < 3600);
        QVERIFY(d->analysis().lastBeatMs <= d->analysis().lengthMs() + 1500);
    }
    QVERIFY(analysed >= 30);

    b.treeWidget()->setCurrentItem(all);                // a group row: analysis cleared
    QVERIFY(!b.detailsWidget()->analysis().valid);
    QVERIFY(!b.detailsWidget()->waveformWidget()->hasWaveform());
}

void TestRekordbox::detailsShowCuesAndLoops()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* all = childNamed(b.treeWidget()->topLevelItem(0), "All Tracks");
    QVERIFY(all);

    int withCues = 0, withLoop = 0;
    for (int i = 0; i < all->childCount() && (withCues < 5 || !withLoop); ++i) {
        b.treeWidget()->setCurrentItem(all->child(i));
        const auto& cues = b.detailsWidget()->analysis().cues;
        if (cues.isEmpty()) continue;
        ++withCues;
        for (const auto& c : cues) {
            QVERIFY(c.hotCue >= 0 && c.hotCue <= 8);
            QVERIFY(c.timeMs <= b.detailsWidget()->analysis().lengthMs() + 1500);
            if (c.hotCue) QCOMPARE(c.letter(), QString(QChar('A' + c.hotCue - 1)));
            if (c.loop) {
                ++withLoop;
                QVERIFY(c.loopEndMs > c.timeMs);
                QVERIFY(c.loopBeats() > 0);
            }
        }
    }
    QVERIFY2(withCues >= 5, "expected tracks with cue points in the test data");
    // loops are rarer; only insist when the export has one (the first export has 147)
    if (!withLoop) qInfo() << "no loop cue found in the test data";
}

void TestRekordbox::waveformWidgetPaints()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* all = childNamed(b.treeWidget()->topLevelItem(0), "All Tracks");
    // pick a track with a hot cue
    for (int i = 0; i < all->childCount(); ++i) {
        b.treeWidget()->setCurrentItem(all->child(i));
        if (!b.detailsWidget()->analysis().cues.isEmpty()) break;
    }
    auto* w = b.detailsWidget()->waveformWidget();
    QVERIFY(w->hasWaveform());
    b.resize(600, 700);
    b.show();
    const QImage img = w->grab().toImage();
    QVERIFY(img.width() >= 200 && img.height() >= 60);
    int lit = 0;
    for (int y = 0; y < img.height(); y += 2)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, y) != QColor(16, 16, 20)) ++lit;
    QVERIFY2(lit > 2000, qPrintable(QString("waveform drew only %1 pixels").arg(lit)));

    // every cue is a yellow vertical line, with a yellow arrow at the top pointing down
    const auto& cues = b.detailsWidget()->analysis().cues;
    QVERIFY(!cues.isEmpty());
    for (const auto& c : cues) {
        const int x = w->cueX(c.timeMs);
        QVERIFY(x >= 0 && x < img.width());
        int yellow = 0;
        for (int y = 0; y < img.height(); ++y)
            if (img.pixelColor(x, y) == vdj::WaveformWidget::kCueColor) ++yellow;
        QVERIFY2(yellow >= img.height() - 2, qPrintable(QString("cue at %1 ms: line has %2/%3 yellow pixels").arg(c.timeMs).arg(yellow).arg(img.height())));
        // arrow: wide at the top row, narrowing to a point a few rows down
        auto yellowRun = [&](int y) {
            int n = 0;
            for (int xx = qMax(0, x - 8); xx <= qMin(img.width() - 1, x + 8); ++xx)
                if (img.pixelColor(xx, y).red() > 240 && img.pixelColor(xx, y).green() > 200 && img.pixelColor(xx, y).blue() < 80) ++n;
            return n;
        };
        QVERIFY2(yellowRun(0) >= 7, qPrintable(QString("arrow top row has %1 yellow pixels").arg(yellowRun(0))));
        QVERIFY2(yellowRun(4) < yellowRun(0), "the arrow should narrow downwards");
        QVERIFY(yellowRun(4) >= 1);
    }
    // no yellow line where there is no cue
    int lonely = 0;
    for (int x = 0; x < img.width(); ++x) {
        bool isCue = false;
        for (const auto& c : cues) if (qAbs(w->cueX(c.timeMs) - x) <= 8) isCue = true;
        if (isCue) continue;
        int yellow = 0;
        for (int y = 0; y < img.height(); ++y)
            if (img.pixelColor(x, y) == vdj::WaveformWidget::kCueColor) ++yellow;
        if (yellow >= img.height() - 2) ++lonely;
    }
    QCOMPARE(lonely, 0);

    w->clear();
    QVERIFY(!w->hasWaveform());
    QVERIFY(!w->grab().toImage().isNull());
}

void TestRekordbox::noAnalysisForContentsOnlyFolder()
{
    QTemporaryDir tmp;
    QVERIFY(QDir(tmp.path()).mkpath("Contents/Artist/Album"));
    QFile f(tmp.path() + "/Contents/Artist/Album/one.mp3");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    vdj::TrackBrowser b;
    QVERIFY(b.openRekordboxFolder(tmp.path()));
    auto* item = b.treeWidget()->topLevelItem(0);
    b.treeWidget()->setCurrentItem(item);
    QVERIFY(!b.detailsWidget()->analysis().valid);
    QCOMPARE(b.miniWaveformDelegate()->columnsFor("/PIONEER/USBANLZ/P000/00000000/ANLZ0000.DAT").size(), 0);
}

// A synthetic silent track of 20 s at 120 BPM (a beat every 500 ms, a bar every 2000 ms), no cues.
static vdj::TrackAnalysis syntheticTrack(double bpm = 120.0, int seconds = 20)
{
    vdj::TrackAnalysis a;
    a.valid = true;
    a.color = QVector<quint16>(seconds * 150, 0);          // height 0: nothing drawn but the background
    const double beatMs = 60000.0 / bpm;
    int n = 0;
    for (double t = 100.0; t < seconds * 1000.0 - beatMs; t += beatMs, ++n)
        a.beats.append({ t, quint8(n % 4 + 1) });
    a.beatCount = a.beats.size();
    a.avgBpm = bpm;
    a.firstBeatMs = quint32(a.beats.first().ms);
    a.lastBeatMs = quint32(a.beats.last().ms);
    return a;
}

void TestRekordbox::analysisHasTheBeatGrid()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* all = childNamed(b.treeWidget()->topLevelItem(0), "All Tracks");
    QVERIFY(all);
    int checked = 0;
    for (int i = 0; i < 30; ++i) {
        b.treeWidget()->setCurrentItem(all->child(i));
        const auto& a = b.detailsWidget()->analysis();
        QCOMPARE(a.beats.size(), a.beatCount);
        if (a.beats.isEmpty()) continue;
        ++checked;
        QVERIFY(a.beats.first().number >= 1 && a.beats.first().number <= 4);   // some tracks start with a pickup (beat 4)
        for (int k = 1; k < a.beats.size(); ++k) {
            QVERIFY(a.beats[k].ms > a.beats[k - 1].ms);                          // strictly increasing
            QCOMPARE(int(a.beats[k].number), (int(a.beats[k - 1].number) % 4) + 1); // 1 2 3 4 1 2 ...
        }
        QCOMPARE(quint32(qMax(0.0, a.beats.first().ms)), a.firstBeatMs);          // a moved grid can start before 0: firstBeatMs is clamped
        QCOMPARE(quint32(qMax(0.0, a.beats.last().ms)), a.lastBeatMs);
        // the grid's spacing is the tempo
        const double beatMs = (a.beats.last().ms - a.beats.first().ms) / (a.beats.size() - 1);
        QVERIFY(qAbs(60000.0 / beatMs - a.avgBpm) < 0.01);
    }
    QVERIFY(checked >= 25);
}

void TestRekordbox::beatLinesAreWhiteAndBarsRedInTheBottomHalf()
{
    vdj::WaveformWidget w;
    w.resize(1000, 100);
    w.setAnalysis(syntheticTrack());
    w.show();
    w.setView(0, 4000);                                   // 4 s: 8 beats, 2 bars, 250 px per second
    const QImage img = w.grab().toImage();
    const int H = img.height(), mid = H / 2;

    int whites = 0, reds = 0;
    for (const auto& beat : w.analysis().beats) {
        const int x = w.timeX(beat.ms);
        if (x < 2 || x >= img.width() - 3) continue;
        const bool bar = beat.number == 1;
        const QColor want = bar ? vdj::WaveformWidget::kBarColor : vdj::WaveformWidget::kBeatColor;
        QVERIFY2(img.pixelColor(x, H - 1) == want, qPrintable(QString("beat at %1 ms: bottom pixel is %2").arg(beat.ms).arg(img.pixelColor(x, H - 1).name())));
        // the line runs from the bottom up to the middle ...
        for (int y = mid; y < H; ++y)
            QVERIFY2(img.pixelColor(x, y) == want, qPrintable(QString("beat at %1 ms, y=%2: %3").arg(beat.ms).arg(y).arg(img.pixelColor(x, y).name())));
        // ... and no further: the top half is untouched background
        for (int y = 0; y < mid; ++y)
            QVERIFY2(img.pixelColor(x, y) == QColor(16, 16, 20), qPrintable(QString("beat at %1 ms drew in the top half at y=%2").arg(beat.ms).arg(y)));
        (bar ? reds : whites)++;
    }
    QVERIFY2(reds >= 2, qPrintable(QString("%1 bars seen").arg(reds)));
    QVERIFY2(whites >= 6, qPrintable(QString("%1 beats seen").arg(whites)));
    // bars come every 4th beat: 2 s apart = 500 px
    int lastRed = -1;
    for (const auto& beat : w.analysis().beats) {
        if (beat.number != 1) continue;
        if (lastRed >= 0) QCOMPARE(w.timeX(beat.ms) - lastRed, 500);
        lastRed = w.timeX(beat.ms);
    }
    // between the lines the bottom row is background
    QCOMPARE(img.pixelColor(w.timeX(100.0 + 250.0), H - 1), QColor(16, 16, 20));
}

void TestRekordbox::denseBeatsAreThinned()
{
    vdj::WaveformWidget w;
    w.resize(500, 100);
    w.setAnalysis(syntheticTrack(120.0, 120));            // 2 minutes in 500 px: a beat is 2.1 px, a bar 8.3 px
    w.show();
    QImage img = w.grab().toImage();
    int white = 0, red = 0;
    for (int x = 0; x < img.width(); ++x) {
        const QColor c = img.pixelColor(x, img.height() - 1);
        white += c == vdj::WaveformWidget::kBeatColor;
        red += c == vdj::WaveformWidget::kBarColor;
    }
    QCOMPARE(white, 0);                                   // beat lines closer than 3 px are left out
    QVERIFY(red > 0);                                     // bars (4 px apart here) are drawn

    w.resize(1500, 100);                                  // a beat is 6.3 px: beats appear
    img = w.grab().toImage();
    white = 0;
    for (int x = 0; x < img.width(); ++x)
        white += img.pixelColor(x, img.height() - 1) == vdj::WaveformWidget::kBeatColor;
    QVERIFY(white > 100);

    w.resize(150, 100);                                   // a bar would be 2.5 px: nothing drawn
    img = w.grab().toImage();
    int any = 0;
    for (int x = 0; x < img.width(); ++x)
        any += img.pixelColor(x, img.height() - 1) != QColor(16, 16, 20);
    QCOMPARE(any, 0);
}

void TestRekordbox::pickupBeatsBelongToBarZero()
{
    vdj::WaveformWidget w;
    w.resize(1000, 100);
    vdj::TrackAnalysis a = syntheticTrack();
    // start the grid on beat 3: beats 3, 4 are a pickup, then bar 1 starts
    for (int i = 0; i < a.beats.size(); ++i)
        a.beats[i].number = quint8((i + 2) % 4 + 1);
    w.setAnalysis(a);
    QVERIFY2(w.describePosition(w.timeX(a.beats[0].ms)).contains("beat 1 (bar 0, beat 3)"), qPrintable(w.describePosition(w.timeX(a.beats[0].ms))));
    QVERIFY2(w.describePosition(w.timeX(a.beats[2].ms)).contains("beat 3 (bar 1, beat 1)"), qPrintable(w.describePosition(w.timeX(a.beats[2].ms))));
}

void TestRekordbox::viewZoomAndScroll()
{
    vdj::WaveformWidget w;
    w.resize(1000, 100);
    w.setAnalysis(syntheticTrack());                      // 20 000 ms
    const double len = w.analysis().lengthMs();
    QCOMPARE(len, 20000.0);
    QCOMPARE(w.zoom(), 1.0);
    QCOMPARE(w.timeX(0), 0);
    QCOMPARE(w.timeX(len), 1000);
    QCOMPARE(w.timeAt(500), 10000.0);

    w.zoomAt(500, 2.0);                                   // zoom in around the middle: the time under x stays put
    QCOMPARE(w.viewSpan(), 10000.0);
    QCOMPARE(w.viewStart(), 5000.0);
    QCOMPARE(w.timeAt(500), 10000.0);
    QCOMPARE(w.zoom(), 2.0);

    w.zoomAt(0, 2.0);                                     // anchored at the left edge
    QCOMPARE(w.viewStart(), 5000.0);
    QCOMPARE(w.viewSpan(), 5000.0);

    w.panBy(-99999);                                      // clamps at the start
    QCOMPARE(w.viewStart(), 0.0);
    w.panBy(99999);                                       // clamps at the end
    QCOMPARE(w.viewStart(), len - w.viewSpan());

    for (int i = 0; i < 30; ++i) w.zoomAt(500, 2.0);      // zoom limit
    QCOMPARE(w.viewSpan(), vdj::WaveformWidget::kMinSpanMs);
    for (int i = 0; i < 30; ++i) w.zoomAt(500, 0.5);      // and out again: never beyond the track
    QCOMPARE(w.viewSpan(), len);
    QCOMPARE(w.viewStart(), 0.0);

    QSignalSpy spy(&w, &vdj::WaveformWidget::viewChanged);
    w.setView(3000, 2000);
    QCOMPARE(spy.count(), 1);
    w.setView(3000, 2000);                                // no change: no signal
    QCOMPARE(spy.count(), 1);
    w.fit();
    QCOMPARE(w.zoom(), 1.0);

    // cue and beat positions follow the view
    w.setView(5000, 5000);
    QCOMPARE(w.cueX(5000), 0);
    QCOMPARE(w.cueX(7500), 500);
    QCOMPARE(w.timeX(10000.0), 1000);

    // describePosition names the nearest beat
    w.fit();
    const QString text = w.describePosition(w.timeX(2100.0));
    QVERIFY2(text.contains("nearest beat 5 (bar 2, beat 1) at 0:02.100"), qPrintable(text));
    QVERIFY2(text.contains("+0.0 ms"), qPrintable(text));
    QVERIFY2(w.describePosition(w.timeX(2600.0)).contains("nearest beat 6 (bar 2, beat 2)"), qPrintable(w.describePosition(w.timeX(2600.0))));

    // a track with no analysis does nothing silly
    vdj::WaveformWidget empty;
    empty.zoomAt(10, 2.0);
    empty.panBy(100);
    empty.setView(5, 5);
    QCOMPARE(empty.viewSpan(), 0.0);
    QVERIFY(empty.describePosition(10).isEmpty() == false);
}

void TestRekordbox::waveformWindowZoomsPansAndScrolls()
{
    vdj::WaveformWindow win;
    win.setTrack("Synthetic", syntheticTrack());
    win.resize(1000, 400);
    win.show();
    QVERIFY(win.windowTitle().contains("Synthetic"));
    auto* w = win.waveform();
    QVERIFY(w->isInteractive());
    QVERIFY(w->width() > 500);
    QCOMPARE(w->zoom(), 1.0);
    QVERIFY(!win.scrollBar()->isEnabled());               // the whole track fits: nothing to scroll

    // mouse wheel zooms around the pointer
    const int x = w->width() / 2;
    const double timeUnderPointer = w->timeAt(x);
    QWheelEvent wheel(QPointF(x, 50), w->mapToGlobal(QPointF(x, 50)), QPoint(0, 0), QPoint(0, 240),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(w, &wheel);
    QVERIFY2(w->zoom() > 1.5, qPrintable(QString::number(w->zoom())));
    QVERIFY(qAbs(w->timeAt(x) - timeUnderPointer) < 40);   // the time under the pointer stays under the pointer
    QVERIFY(win.scrollBar()->isEnabled());
    QCOMPARE(win.scrollBar()->pageStep(), int(w->viewSpan()));
    QCOMPARE(win.scrollBar()->value(), int(std::lround(w->viewStart())));
    QVERIFY(win.viewLabel()->text().contains("zoom"));

    // the scroll bar scrolls
    win.scrollBar()->setValue(0);
    QCOMPARE(w->viewStart(), 0.0);
    win.scrollBar()->setValue(win.scrollBar()->maximum());
    QVERIFY(qAbs(w->viewStart() + w->viewSpan() - 20000.0) < 1.5);

    // dragging pans: drag right by 100 px moves the view earlier
    w->setView(8000, 4000);
    const double before = w->viewStart();
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, QPoint(300, 50));
    QTest::mouseMove(w, QPoint(400, 50));
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, QPoint(400, 50));
    const double expected = 100.0 * 4000.0 / w->width();
    QVERIFY2(qAbs((before - w->viewStart()) - expected) < 5.0, qPrintable(QString("moved %1, expected %2").arg(before - w->viewStart()).arg(expected)));
    QCOMPARE(win.scrollBar()->value(), int(std::lround(w->viewStart())));

    // hovering reports the position and the nearest beat
    QSignalSpy hover(w, &vdj::WaveformWidget::hovered);
    QTest::mouseMove(w, QPoint(200, 50));
    QVERIFY(hover.count() >= 1);
    QVERIFY2(win.positionLabel()->text().contains("nearest beat"), qPrintable(win.positionLabel()->text()));

    // keyboard: + zooms in, Home fits
    w->setFocus();
    const double span = w->viewSpan();
    QTest::keyClick(w, Qt::Key_Plus);
    QVERIFY(w->viewSpan() < span);
    QTest::keyClick(w, Qt::Key_Right);
    QTest::keyClick(w, Qt::Key_Home);
    QCOMPARE(w->zoom(), 1.0);

    // double click fits
    w->setView(1000, 2000);
    QTest::mouseDClick(w, Qt::LeftButton, Qt::NoModifier, QPoint(100, 50));
    QCOMPARE(w->zoom(), 1.0);

    // a new track resets the view
    w->setView(1000, 2000);
    win.setTrack("Other", syntheticTrack(140.0, 30));
    QCOMPARE(w->zoom(), 1.0);
    QCOMPARE(w->analysis().lengthMs(), 30000.0);
    QVERIFY(win.windowTitle().contains("Other"));
}

void TestRekordbox::waveformWindowOpensFromDetails()
{
    if (!haveDb_) QSKIP("no rekordbox test data");
    vdj::TrackBrowser b;
    QVERIFY(open(b));
    auto* all = childNamed(b.treeWidget()->topLevelItem(0), "All Tracks");
    QVERIFY(all);
    auto* d = b.detailsWidget();
    QVERIFY(d->waveformWindow() == nullptr);               // nothing is created until it is asked for

    b.treeWidget()->setCurrentItem(all->child(1));
    d->openWaveformWindow();
    auto* win = d->waveformWindow();
    QVERIFY(win && win->isVisible());
    QCOMPARE(win->waveform()->analysis().beatCount, d->analysis().beatCount);
    QVERIFY(win->waveform()->hasWaveform());
    QVERIFY(win->windowTitle().contains(all->child(1)->text(0)));

    // the open window follows the selection
    b.treeWidget()->setCurrentItem(all->child(2));
    QVERIFY(win->windowTitle().contains(all->child(2)->text(0)));
    QCOMPARE(win->waveform()->analysis().lengthMs(), d->analysis().lengthMs());

    // double clicking the embedded waveform opens it too
    win->hide();
    QTest::mouseDClick(d->waveformWidget(), Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
    QVERIFY(win->isVisible());
    win->hide();
}

QTEST_MAIN(TestRekordbox)
#include "test_rekordbox.moc"
