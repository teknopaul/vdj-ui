#include "track_browser.h"
#include "nfs/id3_tags.h"
#include "waveform_delegate.h"
#include "nfs/remote_deck.h"
#include <QInputDialog>
#include <QTreeWidget>
#include <QDir>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QSplitter>
#include <QDateTime>
#include <QMenu>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QApplication>
#include <QStyle>
#include <QHash>
#include <QMap>
#include <QList>
#include <algorithm>
#include <functional>
#include <cstdlib>
#include <cdj/pdb.h>

namespace vdj {

static bool isAudioFile(const QString& name)
{
    static const char* kExts[] = { "mp3", "m4a", "aiff", "aif", "wav", "flac" };
    QString lower = name.toLower();
    for (const char* e : kExts) {
        if (lower.endsWith(QLatin1Char('.') + QLatin1String(e))) return true;
    }
    return false;
}

TrackBrowser::TrackBrowser(QWidget* parent)
    : QWidget(parent)
{
    setMinimumWidth(280);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* split = new QSplitter(Qt::Vertical, this);
    layout->addWidget(split);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({ QStringLiteral("Track"), QStringLiteral("Artist"), QStringLiteral("BPM"), QStringLiteral("Waveform") });
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c)
        tree_->header()->setSectionResizeMode(c, QHeaderView::Interactive);
    tree_->setColumnWidth(1, 90);
    tree_->setColumnWidth(2, 50);
    tree_->setColumnWidth(kWaveformColumn, 110);
    miniDelegate_ = new MiniWaveformDelegate(tree_);
    tree_->setItemDelegateForColumn(kWaveformColumn, miniDelegate_);
    split->addWidget(tree_);

    details_ = new TrackDetails(this);
    split->addWidget(details_);
    details_->setVisible(false);                  // only a loaded rekordbox database has track details; without one the tree gets the whole pane
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QWidget::customContextMenuRequested, this, &TrackBrowser::showContextMenu);

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* cur, QTreeWidgetItem*) {
        bool ok = false;
        const int idx = cur ? cur->data(0, Qt::UserRole + 2).toInt(&ok) : -1;
        if (ok && idx >= 0 && idx < dbTracks_.size())
            details_->setTrack(dbTracks_[idx]);
        else
            details_->clear();
    });

    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        const QStringList dbMeta = item->data(0, Qt::UserRole + 1).toStringList();
        if (dbMeta.size() == 4) {
            emit trackSelected(playerId_, dbMeta[0], dbMeta[1], dbMeta[3], dbMeta[2]);
            bool ok = false;
            const int idx = item->data(0, Qt::UserRole + 2).toInt(&ok);
            if (ok && idx >= 0 && idx < dbTracks_.size())
                emit previewRequested(dbTracks_[idx]);
            return;
        }
        QString path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty() || !isAudioFile(path))
            return;
        TrackMeta meta = readId3Tags(path);
        emit trackSelected(playerId_, meta.title, meta.artist, meta.bpm, meta.key);
        TrackInfo info;
        info.title = meta.title.isEmpty() ? QFileInfo(path).completeBaseName() : meta.title;
        info.audioPath = path;
        if (!meta.bpm.isEmpty())
            info.fields.append({ QStringLiteral("BPM"), meta.bpm });
        emit previewRequested(info);
    });
}

void TrackBrowser::setMount(const QString& mountPoint)
{
    mountPoint_ = mountPoint;
    refresh();
}

void TrackBrowser::setPlayerId(int playerId)
{
    playerId_ = playerId;
}

void TrackBrowser::refresh()
{
    miniDelegate_->setRoot(QString());
    tree_->clear();
    details_->setVisible(false);
    if (mountPoint_.isEmpty())
        return;

    QDir root(mountPoint_);
    if (!root.exists())
        return;

    for (const QString& child : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QTreeWidgetItem* rootItem = new QTreeWidgetItem(tree_);
        rootItem->setText(0, child);
        rootItem->setData(0, Qt::UserRole, root.filePath(child));
        rootItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        populateItem(rootItem, root.filePath(child), 0);
    }
}

QList<ExportTrack> TrackBrowser::playlistTracks(const QTreeWidgetItem* item) const
{
    bool ok = false;
    const int idx = item ? item->data(0, Qt::UserRole + 3).toInt(&ok) : -1;
    return (ok && idx >= 0 && idx < playlists_.size()) ? playlists_[idx].tracks : QList<ExportTrack>();
}

void TrackBrowser::showContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = tree_->itemAt(pos);
    bool ok = false;
    const int idx = item ? item->data(0, Qt::UserRole + 3).toInt(&ok) : -1;
    if (!ok || idx < 0 || idx >= playlists_.size())
        return;

    QMenu menu(this);
    QAction* exportAct = menu.addAction(QStringLiteral("Export playlist to folder..."));
    if (menu.exec(tree_->viewport()->mapToGlobal(pos)) == exportAct)
        exportPlaylistInteractive(playlists_[idx].name, playlists_[idx].tracks);
}

void TrackBrowser::exportPlaylistInteractive(const QString& name, const QList<ExportTrack>& tracks)
{
    const QString title = QStringLiteral("Export playlist");
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select an empty folder to export \"%1\" to").arg(name));
    if (dir.isEmpty())
        return;

    QProgressDialog progress(QStringLiteral("Copying %1...").arg(name), QStringLiteral("Cancel"),
                             0, tracks.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    const ExportResult r = exportPlaylist(dir, name, tracks, [&](int done, int) {
        progress.setValue(done);
        QApplication::processEvents();
        return !progress.wasCanceled();
    });
    progress.close();

    if (!r.ok) {
        QMessageBox::warning(this, title, r.error);
        return;
    }
    QString msg = QStringLiteral("Copied %1 files to %2\nOrder written to %3")
                      .arg(r.copied).arg(dir, QFileInfo(r.listFile).fileName());
    if (!r.missing.isEmpty())
        msg += QStringLiteral("\n\n%1 files were not found and were skipped:\n%2")
                   .arg(r.missing.size()).arg(r.missing.mid(0, 10).join('\n'));
    if (!r.failed.isEmpty())
        msg += QStringLiteral("\n\n%1 files could not be copied:\n%2")
                   .arg(r.failed.size()).arg(r.failed.mid(0, 10).join('\n'));
    if (r.missing.isEmpty() && r.failed.isEmpty())
        QMessageBox::information(this, title, msg);
    else
        QMessageBox::warning(this, title, msg);
}

bool TrackBrowser::openRekordboxFolder(const QString& folder, QString* error)
{
    remote_.reset();
    return openFolder(folder, error);
}

bool TrackBrowser::openRemoteDeck(int playerId, const QString& ip, QString* error)
{
    auto deck = std::make_shared<RemoteDeck>(playerId, ip);
    QString err;
    if (!deck->connect(&err)) {
        if (error) *error = err;
        return false;
    }
    int which = 0;
    const QStringList exports = deck->exports();
    if (exports.size() > 1) {
        bool ok = false;
        const QString picked = QInputDialog::getItem(this, QStringLiteral("Deck %1").arg(playerId),
            QStringLiteral("Which media on deck %1?").arg(playerId), exports, 0, false, &ok);
        if (!ok) {
            if (error) error->clear();
            return false;
        }
        which = exports.indexOf(picked);
    }
    if (!deck->open(which, &err)) {
        if (error) *error = err == QLatin1String("cancelled") ? QString() : err;
        return false;
    }
    remote_ = deck;
    playerId_ = playerId;
    if (!openFolder(deck->root(), error)) {
        remote_.reset();
        return false;
    }
    return true;
}

bool TrackBrowser::openFolder(const QString& folder, QString* error)
{
    QDir root(folder);
    if (!root.exists() || !root.exists(QStringLiteral("Contents"))) {
        if (error) *error = QStringLiteral("%1 has no Contents folder").arg(folder);
        return false;
    }
    hasPdb_ = QFileInfo::exists(root.filePath(QStringLiteral("PIONEER/rekordbox/export.pdb")));
    mountPoint_.clear();

    if (hasPdb_) {
        QString perr;
        if (loadPdb(root.filePath(QStringLiteral("PIONEER/rekordbox/export.pdb")), root.absolutePath(), &perr)) {
            exportRoot_ = root.absolutePath();
            emit databaseLoaded(exportRoot_);
            return true;
        }
        hasPdb_ = false;   // unreadable db: fall back to the folder view
        if (error) *error = perr;
    }

    miniDelegate_->setRoot(QString());
    tree_->clear();
    details_->setVisible(false);
    auto* top = new QTreeWidgetItem(tree_);
    top->setText(0, root.dirName());
    top->setIcon(0, style()->standardIcon(QStyle::SP_DriveHDIcon));
    top->setData(0, Qt::UserRole, root.absolutePath());
    populateContents(top, root.filePath(QStringLiteral("Contents")), 0);
    top->setExpanded(true);
    return true;
}

namespace {

struct DbTrack {
    uint32_t id = 0, artistId = 0, albumId = 0, genreId = 0, keyId = 0, bpmX100 = 0, duration = 0;
    uint32_t labelId = 0, remixerId = 0, artworkId = 0, colorId = 0, sampleRate = 0, bitrate = 0;
    uint32_t fileSize = 0, trackNumber = 0, discNumber = 0, year = 0, sampleDepth = 0;
    uint32_t rating = 0, playCount = 0;
    QString title, path, filename, comment, dateAdded, releaseDate, mixName, analyzePath;
};

struct Db {
    QHash<uint32_t, QString> artists, albums, genres, keys, labels, colors, artwork;
    QList<DbTrack> tracks;
    QHash<uint32_t, int> trackIndex;
    struct Node { uint32_t id, parent, sort; bool folder; QString name; };
    QList<Node> playlists;
    struct Entry { uint32_t index, track; };
    QHash<uint32_t, QList<Entry>> entries;
};

int nameCb(const pdb_named_t* r, void* ud)
{
    static_cast<QHash<uint32_t, QString>*>(ud)->insert(r->id, QString::fromUtf8(r->name ? r->name : ""));
    return 0;
}

int albumCb(const pdb_album_t* r, void* ud)
{
    static_cast<Db*>(ud)->albums.insert(r->id, QString::fromUtf8(r->name ? r->name : ""));
    return 0;
}

int trackCb(const pdb_track_t* t, void* ud)
{
    auto* db = static_cast<Db*>(ud);
    DbTrack d;
    d.id = t->id; d.artistId = t->artist_id; d.albumId = t->album_id; d.genreId = t->genre_id;
    d.keyId = t->key_id; d.bpmX100 = t->bpm_x100; d.duration = t->duration_secs;
    d.title = QString::fromUtf8(t->title ? t->title : (t->filename ? t->filename : ""));
    d.path = QString::fromUtf8(t->file_path ? t->file_path : "");
    auto str = [](const char* c) { return QString::fromUtf8(c ? c : ""); };
    d.labelId = t->label_id; d.remixerId = t->remixer_id; d.artworkId = t->artwork_id;
    d.colorId = t->color_id; d.sampleRate = t->sample_rate; d.bitrate = t->bitrate;
    d.fileSize = t->file_size; d.trackNumber = t->track_number; d.discNumber = t->disc_number;
    d.year = t->year; d.sampleDepth = t->sample_depth; d.rating = t->rating; d.playCount = t->play_count;
    d.filename = str(t->filename); d.comment = str(t->comment); d.dateAdded = str(t->date_added);
    d.releaseDate = str(t->release_date); d.mixName = str(t->mix_name); d.analyzePath = str(t->analyze_path);
    db->trackIndex.insert(d.id, db->tracks.size());
    db->tracks.append(d);
    return 0;
}

int playlistCb(const pdb_playlist_t* p, void* ud)
{
    static_cast<Db*>(ud)->playlists.append({ p->id, p->parent_id, p->sort_order, p->is_folder != 0,
                                             QString::fromUtf8(p->name ? p->name : "") });
    return 0;
}

int entryCb(const pdb_playlist_entry_t* e, void* ud)
{
    static_cast<Db*>(ud)->entries[e->playlist_id].append({ e->entry_index, e->track_id });
    return 0;
}

} // namespace

bool TrackBrowser::loadPdb(const QString& pdbPath, const QString& root, QString* error)
{
    uint32_t len = 0;
    uint8_t* buf = pdb_read_file(pdbPath.toLocal8Bit().constData(), &len);
    if (!buf) {
        if (error) *error = QStringLiteral("Cannot read %1").arg(pdbPath);
        return false;
    }

    Db db;
    int rc = pdb_parse(buf, len, trackCb, &db);
    if (rc == 0) {
        pdb_parse_artists(buf, len, nameCb, &db.artists);
        pdb_parse_genres(buf, len, nameCb, &db.genres);
        pdb_parse_keys(buf, len, nameCb, &db.keys);
        pdb_parse_labels(buf, len, nameCb, &db.labels);
        pdb_parse_colors(buf, len, nameCb, &db.colors);
        pdb_parse_artwork(buf, len, nameCb, &db.artwork);
        pdb_parse_albums(buf, len, albumCb, &db);
        pdb_parse_playlists(buf, len, playlistCb, &db);
        pdb_parse_playlist_entries(buf, len, entryCb, &db);
    }
    free(buf);
    if (rc != 0) {
        if (error) *error = QStringLiteral("%1 is not a valid rekordbox database").arg(pdbPath);
        return false;
    }

    dbTracks_.clear();
    playlists_.clear();
    // Artwork: track.artwork_id -> artwork table row -> path like /PIONEER/Artwork/00001/a1.jpg.
    // Newer exports also carry a larger a1_m.jpg next to it; prefer that when present.
    auto artworkPath = [&](uint32_t id) {
        const QString rel = db.artwork.value(id);
        if (id == 0 || rel.isEmpty()) return QString();
        QString medium = rel;
        const int dot = medium.lastIndexOf('.');
        if (dot > 0) medium.insert(dot, QStringLiteral("_m"));
        if (QFileInfo::exists(root + medium)) return root + medium;
        return QFileInfo::exists(root + rel) ? root + rel : QString();
    };
    auto buildInfo = [&](const DbTrack& t) {
        TrackInfo ti;
        ti.title = t.title;
        ti.artworkPath = artworkPath(t.artworkId);
        ti.root = root;
        ti.audioPath = root + t.path;
        if (remote_) {
            std::weak_ptr<RemoteDeck> weak = remote_;
            ti.ensureFile = [weak](const QString& rel) { auto d = weak.lock(); return d && d->ensure(rel); };
            ti.ensureAnalysis = [weak, this](const QString& rel) {
                auto d = weak.lock();
                const bool ok = d && d->ensureAnalysis(rel);
                if (ok) {                    // the row's mini waveform can be drawn now
                    miniDelegate_->forget(rel);
                    tree_->viewport()->update();
                }
                return ok;
            };
        }
        ti.analyzePath = t.analyzePath;
        auto add = [&](const char* label, const QString& v) { ti.fields.append({ QString::fromLatin1(label), v }); };
        auto num = [](uint32_t v) { return v ? QString::number(v) : QString(); };
        add("Artist", db.artists.value(t.artistId));
        add("Album", db.albums.value(t.albumId));
        add("Mix", t.mixName);
        add("Remixer", db.artists.value(t.remixerId));
        add("Genre", db.genres.value(t.genreId));
        add("Label", db.labels.value(t.labelId));
        add("Key", db.keys.value(t.keyId));
        add("BPM", t.bpmX100 ? QString::number(t.bpmX100 / 100.0, 'f', 2) : QString());
        add("Duration", t.duration ? QStringLiteral("%1:%2").arg(t.duration / 60).arg(t.duration % 60, 2, 10, QChar('0')) : QString());
        add("Year", num(t.year));
        add("Track number", num(t.trackNumber));
        add("Disc", num(t.discNumber));
        add("Rating", t.rating ? QString(t.rating > 5 ? 5 : t.rating, QChar(0x2605)) : QString());
        add("Colour", db.colors.value(t.colorId));
        add("Plays", num(t.playCount));
        add("Added", t.dateAdded);
        add("Released", t.releaseDate);
        add("Comment", t.comment);
        add("Bitrate", t.bitrate ? QStringLiteral("%1 kbps").arg(t.bitrate) : QString());
        add("Sample rate", t.sampleRate ? QStringLiteral("%1 Hz").arg(t.sampleRate) : QString());
        add("Bit depth", num(t.sampleDepth));
        add("File size", t.fileSize ? QStringLiteral("%1 MB").arg(t.fileSize / 1048576.0, 0, 'f', 1) : QString());
        add("File", t.path);
        add("Analysis", t.analyzePath);
        add("Track ID", num(t.id));
        return ti;
    };
    QHash<int, int> infoIndex;   // db.tracks index -> dbTracks_ index (each track built once)
    auto trackItem = [&](QTreeWidgetItem* parent, const DbTrack& t) {
        auto* item = new QTreeWidgetItem(parent);
        const QString artist = db.artists.value(t.artistId);
        const QString key = db.keys.value(t.keyId);
        const QString bpm = t.bpmX100 ? QString::number(t.bpmX100 / 100.0, 'f', 1) : QString();
        item->setText(0, t.title);
        item->setText(1, artist);
        item->setText(2, bpm);
        item->setData(0, MiniWaveformDelegate::kAnalyzePathRole, t.analyzePath);
        item->setToolTip(0, t.path);
        item->setData(0, Qt::UserRole, root + t.path);
        item->setData(0, Qt::UserRole + 1, QStringList{ t.title, artist, key, bpm });
        const int ti = db.trackIndex.value(t.id, -1);
        auto it = infoIndex.find(ti);
        if (it == infoIndex.end()) {
            dbTracks_.append(buildInfo(t));
            it = infoIndex.insert(ti, dbTracks_.size() - 1);
        }
        item->setData(0, Qt::UserRole + 2, it.value());
        return item;
    };
    auto folderItem = [&](QTreeWidgetItem* parent, const QString& text, QStyle::StandardPixmap icon) {
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
        item->setText(0, text);
        item->setIcon(0, style()->standardIcon(icon));
        return item;
    };

    miniDelegate_->setRoot(root);
    tree_->clear();
    auto* top = folderItem(nullptr, QDir(root).dirName(), QStyle::SP_DriveHDIcon);
    top->setText(1, QStringLiteral("%1 tracks").arg(db.tracks.size()));

    // Playlists: tree via parent_id (0 = root), tracks in entry order.
    auto* plRoot = folderItem(top, QStringLiteral("Playlists"), QStyle::SP_DirIcon);
    QHash<uint32_t, QList<int>> children;
    for (int i = 0; i < db.playlists.size(); ++i)
        children[db.playlists[i].parent].append(i);
    std::function<void(QTreeWidgetItem*, uint32_t, int)> addPlaylists =
        [&](QTreeWidgetItem* parent, uint32_t parentId, int depth) {
        QList<int> kids = children.value(parentId);
        std::sort(kids.begin(), kids.end(), [&](int a, int b) {
            return db.playlists[a].sort < db.playlists[b].sort;
        });
        for (int i : kids) {
            const auto& n = db.playlists[i];
            auto* item = folderItem(parent, n.name, QStyle::SP_DirIcon);
            if (depth < 16 && n.id != parentId)
                addPlaylists(item, n.id, depth + 1);
            QList<Db::Entry> es = db.entries.value(n.id);
            std::sort(es.begin(), es.end(), [](const Db::Entry& a, const Db::Entry& b) { return a.index < b.index; });
            for (const auto& e : es) {
                int ti = db.trackIndex.value(e.track, -1);
                if (ti >= 0) trackItem(item, db.tracks[ti]);
            }
            item->setText(1, QStringLiteral("%1").arg(es.size()));

            // Tracks in playlist order, for export.
            PlaylistData pd;
            pd.name = n.name;
            for (const auto& e : es) {
                int ti = db.trackIndex.value(e.track, -1);
                if (ti < 0) continue;
                const DbTrack& t = db.tracks[ti];
                pd.tracks.append({ root + t.path, t.title, db.artists.value(t.artistId) });
            }
            playlists_.append(pd);
            item->setData(0, Qt::UserRole + 3, playlists_.size() - 1);
        }
    };
    addPlaylists(plRoot, 0, 0);

    // Artists / Albums / Genres: group tracks by id, sorted by name.
    auto addGrouped = [&](const QString& title, const QHash<uint32_t, QString>& names,
                          uint32_t DbTrack::*field) {
        auto* groupRoot = folderItem(top, title, QStyle::SP_DirIcon);
        QMap<QString, QList<int>> groups;   // name -> track indexes (sorted by name, case-insensitively below)
        for (int i = 0; i < db.tracks.size(); ++i) {
            const QString n = names.value(db.tracks[i].*field);
            groups[n.isEmpty() ? QStringLiteral("(unknown)") : n].append(i);
        }
        QStringList keys = groups.keys();
        std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
        for (const QString& k : keys) {
            auto* g = folderItem(groupRoot, k, QStyle::SP_DirIcon);
            g->setText(1, QString::number(groups[k].size()));
            for (int i : groups[k]) trackItem(g, db.tracks[i]);
        }
    };
    addGrouped(QStringLiteral("Artists"), db.artists, &DbTrack::artistId);
    addGrouped(QStringLiteral("Albums"), db.albums, &DbTrack::albumId);
    addGrouped(QStringLiteral("Genres"), db.genres, &DbTrack::genreId);

    auto* all = folderItem(top, QStringLiteral("All Tracks"), QStyle::SP_DirIcon);
    for (const auto& t : db.tracks) trackItem(all, t);

    top->setExpanded(true);
    plRoot->setExpanded(true);
    details_->setVisible(true);
    return true;
}

// Cheap folder walk (no tag reads: rekordbox libraries are large). Contents is laid out
// as Artist/Album/track files; titles come from the file names until export.pdb is parsed.
void TrackBrowser::populateContents(QTreeWidgetItem* parent, const QString& path, int depth)
{
    if (depth >= 6)
        return;
    QDir dir(path);
    for (const QString& d : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, d);
        item->setData(0, Qt::UserRole, dir.filePath(d));
        item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        populateContents(item, dir.filePath(d), depth + 1);
    }
    for (const QString& f : dir.entryList(QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        if (!isAudioFile(f))
            continue;
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, QFileInfo(f).completeBaseName());
        item->setData(0, Qt::UserRole, dir.filePath(f));
    }
}

void TrackBrowser::populateItem(QTreeWidgetItem* parent, const QString& path, int depth)
{
    if (depth >= 4)
        return;
    QDir dir(path);
    const auto dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const auto files = dir.entryList(QDir::Files, QDir::Name);

    for (const QString& f : files) {
        if (!isAudioFile(f))
            continue;
        QString full = dir.filePath(f);
        TrackMeta meta = readId3Tags(full);
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, meta.title.isEmpty() ? f : meta.title);
        item->setText(1, meta.artist);
        item->setText(2, meta.bpm);
        item->setData(0, Qt::UserRole, full);
    }
    if (depth >= 3 && files.isEmpty())
        return;
    for (const QString& d : dirs) {
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, d);
        item->setData(0, Qt::UserRole, dir.filePath(d));
        item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        populateItem(item, dir.filePath(d), depth + 1);
    }
}

} // namespace vdj