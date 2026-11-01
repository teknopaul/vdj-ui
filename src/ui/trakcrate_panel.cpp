#include "trakcrate_panel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <QIcon>

#include <functional>
#include <map>
#include <unordered_set>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QStyle>
#include <QMouseEvent>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "config/config_manager.h"
#include "trakcrate/settings_files.h"
#include "trakcrate/settings_sets.h"
#include "trakcrate_settings_dialogs.h"
#include "trakcrate_settings_viewer.h"
#include "trakcrate_artwork.h"
#include "trakcrate_bpm_dialog.h"
#include "trakcrate_export_dialog.h"
#include "trakcrate_import_dialog.h"
#include "trakcrate_pendrive_dialog.h"
#include "trakcrate_rbimport_dialog.h"
#include "trakcrate_preview_player.h"
#include "trakcrate_scripts.h"
#include "trakcrate_table_view.h"
#include "trakcrate_tree_widget.h"
#include "trakcrate_job.h"
#include "trakcrate_track_model.h"

namespace vdj {

namespace {

// The normal cell with a 1 px dark yellow line under it: together the cells of a row draw a line under the row (on top of the row banding).
class RowLineDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        painter->save();
        painter->setPen(QColor(0x6d, 0x59, 0x00));
        painter->drawLine(option.rect.left(), option.rect.bottom(), option.rect.right(), option.rect.bottom());
        painter->restore();
    }
};

// The Rating column: five stars, solid for the rating and hollow for the rest, a click sets that many (the same star again clears).
class RatingDelegate : public RowLineDelegate {
public:
    using RowLineDelegate::RowLineDelegate;
    static int pitch(const QFontMetrics& fm) { return fm.horizontalAdvance(QChar(0x2605)) + 3; }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override { return QSize(5 * pitch(option.fontMetrics) + 10, option.fontMetrics.height() + 4); }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem o = option;
        initStyleOption(&o, index);
        o.text.clear();
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &o, painter, o.widget);          // background and selection only
        const int stars = index.data(TrakcrateTrackModel::RatingRole).toInt();
        painter->save();
        const int w = pitch(option.fontMetrics);
        for (int i = 0; i < 5; ++i) {
            painter->setPen(i < stars ? QColor(240, 180, 20) : QColor(140, 140, 140));
            painter->drawText(QRect(option.rect.left() + 5 + i * w, option.rect.top(), w, option.rect.height()), Qt::AlignVCenter | Qt::AlignLeft,
                              QString(QChar(i < stars ? 0x2605 : 0x2606)));
        }
        painter->setPen(QColor(0x6d, 0x59, 0x00));
        painter->drawLine(option.rect.left(), option.rect.bottom(), option.rect.right(), option.rect.bottom());
        painter->restore();
    }
    bool editorEvent(QEvent* ev, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override
    {
        if (ev->type() != QEvent::MouseButtonRelease) return false;
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() != Qt::LeftButton) return false;
        const int n = (me->pos().x() - option.rect.left() - 5) / pitch(option.fontMetrics) + 1;
        if (me->pos().x() < option.rect.left() + 5 || n < 1 || n > 5) return false;
        const int now = index.data(TrakcrateTrackModel::RatingRole).toInt();
        model->setData(index, n == now ? 0 : n, TrakcrateTrackModel::RatingRole);
        return true;
    }
};

} // namespace

namespace {
constexpr int KindRole = Qt::UserRole;       // "folder", "playlist", "drive" or "entry"; empty for the group headings
constexpr int NameRole = Qt::UserRole + 1;
constexpr int DriveRole = Qt::UserRole + 3;   // entries: the pendrive they belong to (NameRole holds the entry's directory name)
constexpr int IndexRole = Qt::UserRole + 4;   // entries: position on the pendrive
}

TrakcratePanel::TrakcratePanel(ConfigManager* config, QWidget* parent) : QWidget(parent), config_(config)
{
    jobs_ = new TrakcrateJobRunner(this);
    model_ = new TrakcrateTrackModel(this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setSortRole(TrakcrateTrackModel::SortRole);
    proxy_->setSortCaseSensitivity(Qt::CaseInsensitive);

    libraryLabel_ = new QLabel(QStringLiteral("No library: open or create one"));
    libraryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* openButton = new QPushButton(QStringLiteral("Open..."));
    openButton->setToolTip(QStringLiteral("Open a trakcrate library folder"));
    auto* newButton = new QPushButton(QStringLiteral("New..."));
    newButton->setToolTip(QStringLiteral("Create a library in a folder"));
    importButton_ = new QPushButton(QStringLiteral("Import folder..."));
    importButton_->setToolTip(QStringLiteral("Import a folder of mp3/wav files and make a playlist of it"));
    newPendriveButton_ = new QPushButton(QStringLiteral("New pendrive..."));
    newPendriveButton_->setToolTip(QStringLiteral("Make a pendrive from folders and playlists of the library"));
    newPendriveButton_->setEnabled(false);
    rbImportButton_ = new QPushButton(QStringLiteral("Import drive..."));
    rbImportButton_->setToolTip(QStringLiteral("Import a real rekordbox pendrive: metadata, beat grids, cues, waveforms, artwork and playlists as they are"));
    rbImportButton_->setEnabled(false);
    refreshButton_ = new QPushButton(QStringLiteral("Refresh"));
    exportButton_ = new QPushButton(QStringLiteral("Export..."));
    exportButton_->setToolTip(QStringLiteral("Write playlists to an empty folder or stick in the rekordbox format"));
    exportButton_->setEnabled(false);
    importButton_->setEnabled(false);
    refreshButton_->setEnabled(false);

    auto* top = new QHBoxLayout;
    top->addWidget(libraryLabel_, 1);
    top->addWidget(openButton);
    top->addWidget(newButton);
    top->addWidget(importButton_);
    top->addWidget(rbImportButton_);
    top->addWidget(newPendriveButton_);
    top->addWidget(exportButton_);
    top->addWidget(refreshButton_);
    scripts_ = new ScriptManager(config_, this);
    scriptsButton_ = new QPushButton(QStringLiteral("Scripts"));
    scriptsButton_->setToolTip(QStringLiteral("Scripts for the whole library (scripts/crate-script-*). They only run when you start them."));
    scriptsButton_->setEnabled(false);
    auto* scriptsMenu = new QMenu(scriptsButton_);
    scriptsButton_->setMenu(scriptsMenu);
    connect(scriptsMenu, &QMenu::aboutToShow, this, [this, scriptsMenu] {
        populateScriptsMenu(scriptsMenu, scripts_, trakcrate::ScriptKind::Crate, this, [] { return QString(); });
    });
    connect(scripts_, &ScriptManager::ran, this, [this] { refresh(); });
    top->addWidget(scriptsButton_);

    tree_ = new TrakcrateTreeWidget;
    tree_->setHeaderHidden(true);
    tree_->setMinimumWidth(140);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setDragEnabled(true);                          // only the lists of a virtual pendrive can be dragged (see rebuildTree)
    tree_->setDragDropMode(QAbstractItemView::InternalMove);
    tree_->setDropIndicatorShown(true);
    tree_->onEntryDropped = [this](const QString& drive, int from, int before) { QString err; if (!moveEntry(drive, from, before, &err) && !err.isEmpty()) emit status(err); };   // the logo above it is 128 px wide

    table_ = new TrakcrateTableView;
    table_->setModel(proxy_);
    table_->setRootIsDecorated(false);
    table_->setAlternatingRowColors(true);
    table_->setDragDropMode(QAbstractItemView::NoDragDrop);      // on for playlists and pendrive lists, see onTreeSelection
    table_->setDropIndicatorShown(true);
    table_->onKeyMove = [this](int dir) { QString err; if (!moveSelected(dir, &err) && !err.isEmpty()) emit status(err); };
    tree_->onKeyMove = [this](int dir) { QString err; if (!moveChosenEntry(dir, &err) && !err.isEmpty()) emit status(err); };
    table_->onRowsDropped = [this](const std::vector<int>& rows, int before) { QString err; if (!moveRows(rows, before, &err) && !err.isEmpty()) emit status(err); };
    table_->setItemDelegate(new RowLineDelegate(table_));
    table_->setItemDelegateForColumn(TrakcrateTrackModel::Rating, new RatingDelegate(table_));
    connect(model_, &TrakcrateTrackModel::ratingFailed, this, &TrakcratePanel::status);
    connect(model_, &TrakcrateTrackModel::ratingWritten, this, [this](const QString& dir, int stars) {
        showDetails(table_->currentIndex());
        emit status(QStringLiteral("Rating set to %1").arg(stars));
        emit ratingEdited(dir);
    });
    table_->setSortingEnabled(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->setUniformRowHeights(true);
    table_->setIconSize(QSize(120, 20));
    table_->header()->setStretchLastSection(true);
    table_->sortByColumn(-1, Qt::AscendingOrder);      // keep the library's own order (playlist order) until the user sorts

    artwork_ = new QLabel;
    artwork_->setFixedSize(160, 160);
    artwork_->setAlignment(Qt::AlignCenter);
    artwork_->setFrameShape(QFrame::StyledPanel);
    artwork_->setText(QStringLiteral("no artwork"));
    details_ = new QLabel;
    details_->setWordWrap(true);
    details_->setTextFormat(Qt::RichText);
    details_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* detailBox = new QWidget;
    auto* detailLayout = new QVBoxLayout(detailBox);
    detailLayout->addWidget(artwork_, 0, Qt::AlignLeft);
    detailLayout->addWidget(details_, 1);
    detailBox->setMinimumWidth(190);

    auto* middle = new QWidget;
    auto* middleLayout = new QVBoxLayout(middle);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->addWidget(table_, 1);
    preview_ = new TrakcratePreviewPlayer(config_);
    middleLayout->addWidget(preview_);

    auto* splitter = new QSplitter(Qt::Horizontal);
    // left column: the trakcrate logo at the top left, the folder and playlist tree below it
    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto* logo = new QLabel;
    logo->setPixmap(QPixmap(QStringLiteral(":/img/trakcrate-128x128.png")));
    logo->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    logo->setToolTip(QStringLiteral("trakcrate"));
    leftLayout->addWidget(logo, 0, Qt::AlignLeft | Qt::AlignTop);
    leftLayout->addWidget(tree_, 1);
    splitter->addWidget(left);
    splitter->addWidget(middle);
    splitter->addWidget(detailBox);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({170, 560, 220});

    progressLabel_ = new QLabel;
    progress_ = new QProgressBar;
    progress_->setMaximumWidth(220);
    progress_->hide();
    cancelButton_ = new QPushButton(QStringLiteral("Cancel"));
    cancelButton_->hide();
    orphanButton_ = new QPushButton;
    orphanButton_->hide();
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(progressLabel_, 1);
    bottom->addWidget(orphanButton_);
    bottom->addWidget(progress_);
    bottom->addWidget(cancelButton_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(splitter, 1);
    layout->addLayout(bottom);

    watcher_ = new QFileSystemWatcher(this);
    watchTimer_ = new QTimer(this);
    watchTimer_->setSingleShot(true);
    watchTimer_->setInterval(500);

    connect(openButton, &QPushButton::clicked, this, &TrakcratePanel::chooseLibrary);
    connect(newButton, &QPushButton::clicked, this, &TrakcratePanel::newLibrary);
    connect(importButton_, &QPushButton::clicked, this, &TrakcratePanel::importDialog);
    connect(newPendriveButton_, &QPushButton::clicked, this, &TrakcratePanel::newPendriveDialog);
    connect(rbImportButton_, &QPushButton::clicked, this, &TrakcratePanel::rekordboxImportDialog);
    connect(refreshButton_, &QPushButton::clicked, this, &TrakcratePanel::refresh);
    connect(exportButton_, &QPushButton::clicked, this, &TrakcratePanel::exportDialog);
    connect(cancelButton_, &QPushButton::clicked, jobs_, &TrakcrateJobRunner::cancel);
    connect(orphanButton_, &QPushButton::clicked, this, &TrakcratePanel::deleteOrphansDialog);
    // Qt only toggles ascending/descending; a third click on the same header goes back to the library's own (playlist) order.
    connect(table_->header(), &QHeaderView::sectionClicked, this, [this, last = std::make_shared<std::pair<int, Qt::SortOrder>>(-1, Qt::AscendingOrder)](int col) {
        const Qt::SortOrder now = table_->header()->sortIndicatorOrder();
        if (last->first == col && last->second == Qt::DescendingOrder) {
            table_->sortByColumn(-1, Qt::AscendingOrder);
            *last = {-1, Qt::AscendingOrder};
        } else {
            *last = {col, now};
        }
    });
    table_->setEditTriggers(QAbstractItemView::EditKeyPressed);          // F2; a double click on the Comment column is handled below
    connect(model_, &TrakcrateTrackModel::commentWritten, this, [this](const QString& dir, const QString&) {
        showDetails(table_->currentIndex());
        emit status(QStringLiteral("Comment saved"));
        emit ratingEdited(dir);                                         // the track editor reloads if it shows this track
    });
    connect(table_, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (idx.column() == TrakcrateTrackModel::Comment) { table_->edit(idx); return; }      // edit the comment instead of loading the track
        const QString d = model_->trackDir(proxy_->mapToSource(idx).row());
        if (!d.isEmpty()) { preview_->stop(); emit loadRequested(d); }       // the deck needs the audio output
    });
    connect(table_, &QWidget::customContextMenuRequested, this, &TrakcratePanel::tableContextMenu);
    connect(preview_, &TrakcratePreviewPlayer::playRequested, this, [this] {
        const QModelIndex cur = table_->currentIndex();
        QString err;
        startPreview(cur.isValid() ? cur.row() : -1, &err);
    });
    connect(preview_, &TrakcratePreviewPlayer::endOfList, this, &TrakcratePanel::onPreviewEnd);
    connect(preview_, &TrakcratePreviewPlayer::trackStarted, this, &TrakcratePanel::onPreviewTrack);
    connect(preview_, &TrakcratePreviewPlayer::status, this, &TrakcratePanel::status);
    connect(tree_, &QWidget::customContextMenuRequested, this, &TrakcratePanel::treeContextMenu);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) { onTreeDoubleClick(it); });
    connect(tree_, &QTreeWidget::currentItemChanged, this, &TrakcratePanel::onTreeSelection);
    connect(table_->selectionModel(), &QItemSelectionModel::currentChanged, this, &TrakcratePanel::onTrackSelection);
    connect(jobs_, &TrakcrateJobRunner::progress, this, &TrakcratePanel::onJobProgress);
    connect(jobs_, &TrakcrateJobRunner::finished, this, &TrakcratePanel::onJobFinished);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, &TrakcratePanel::fileSystemChanged);
    connect(watchTimer_, &QTimer::timeout, this, &TrakcratePanel::refresh);

    if (config_) {                                      // reopen the library used last time, silently
        const QString last = config_->loadValue(QStringLiteral("trakcrate"), QStringLiteral("library"));
        if (!last.isEmpty() && QDir(last).exists()) openLibrary(last);
    }
}

TrakcratePanel::~TrakcratePanel() = default;

bool TrakcratePanel::busy() const { return jobs_->busy(); }

bool TrakcratePanel::waitForJob(int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (jobs_->busy()) {
        if (t.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return true;
}

// ---- library ------------------------------------------------------------------------------------------------------------------------

void TrakcratePanel::rememberLibrary(const QString& path)
{
    if (config_) config_->saveValue(QStringLiteral("trakcrate"), QStringLiteral("library"), path);
}

bool TrakcratePanel::openLibrary(const QString& path, QString* err)
{
    std::string e;
    trakcrate::Library lib;
    if (!trakcrate::Library::open(path.toStdString(), &lib, &e)) {
        if (err) *err = QString::fromStdString(e);
        return false;
    }
    lib_ = lib;
    sizeCache_.clear();
    libraryLabel_->setText(QString::fromStdString(lib_.root().string()));
    importButton_->setEnabled(true);
    rbImportButton_->setEnabled(true);
    newPendriveButton_->setEnabled(true);
    exportButton_->setEnabled(true);
    refreshButton_->setEnabled(true);
    scriptsButton_->setEnabled(true);
    scripts_->setLibrary(QString::fromStdString(lib_.root().string()));
    model_->clear();
    showDetails(QModelIndex());
    rebuildTree();
    updateWatches();
    rememberLibrary(QString::fromStdString(lib_.root().string()));
    emit libraryChanged(QString::fromStdString(lib_.root().string()));
    return true;
}

bool TrakcratePanel::createLibrary(const QString& path, QString* err)
{
    std::string e;
    if (!trakcrate::Library::create(path.toStdString(), &e)) {
        if (err) *err = QString::fromStdString(e);
        return false;
    }
    return openLibrary(path, err);
}

void TrakcratePanel::chooseLibrary()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Open a trakcrate library"), QDir::homePath());
    if (dir.isEmpty()) return;
    QString err;
    if (!openLibrary(dir, &err)) QMessageBox::warning(this, QStringLiteral("trakcrate"), err);
}

void TrakcratePanel::newLibrary()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Folder for the new trakcrate library"), QDir::homePath());
    if (dir.isEmpty()) return;
    QString err;
    if (!createLibrary(dir, &err)) QMessageBox::warning(this, QStringLiteral("trakcrate"), err);
}

// ---- the folder and playlist tree ---------------------------------------------------------------------------------------------------

uint64_t TrakcratePanel::estimateBytes(const std::vector<trakcrate::fs::path>& trackDirs)
{
    uint64_t n = 0;
    std::unordered_set<std::string> seen;
    for (const trakcrate::fs::path& d : trackDirs) {
        const std::string key = d.string();
        if (!seen.insert(key).second) continue;
        auto it = sizeCache_.find(key);
        if (it == sizeCache_.end()) {
            const uint64_t b = trakcrate::Library::estimateTrackBytes(d);
            if (b == 0) continue;                               // no audio (yet): do not remember, an import may be filling it
            it = sizeCache_.emplace(key, b).first;
        }
        n += it->second;
    }
    return n > 0 ? n + 300000 : 0;                              // the database and settings
}

void TrakcratePanel::rebuildTree(bool selectionSignals)
{
    QString keepKind, keepName, keepDrive;
    if (QTreeWidgetItem* cur = tree_->currentItem()) {
        keepKind = cur->data(0, KindRole).toString();
        keepName = cur->data(0, NameRole).toString();
        keepDrive = cur->data(0, DriveRole).toString();
    }
    // remember what the user had collapsed and where the view was scrolled, so a rebuild does not throw the view away
    auto keyOf = [](const QTreeWidgetItem* it) {
        const QString nm = it->data(0, NameRole).toString();
        return (nm.isEmpty() ? it->text(0) : nm) + QLatin1Char('\x1f') + it->data(0, KindRole).toString() + QLatin1Char('\x1f') + it->data(0, DriveRole).toString();
    };
    QSet<QString> collapsed;
    const bool hadItems = tree_->topLevelItemCount() > 0;
    std::function<void(QTreeWidgetItem*)> note = [&](QTreeWidgetItem* it) {
        if (it->childCount() > 0 && !it->isExpanded()) collapsed.insert(keyOf(it));
        for (int i = 0; i < it->childCount(); ++i) note(it->child(i));
    };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) note(tree_->topLevelItem(i));
    const int scrollPos = tree_->verticalScrollBar()->value();
    QSignalBlocker block(tree_);
    tree_->clear();
    QTreeWidgetItem* folders = new QTreeWidgetItem(tree_, QStringList(QStringLiteral("Folders")));
    QTreeWidgetItem* lists = new QTreeWidgetItem(tree_, QStringList(QStringLiteral("Playlists")));
    QTreeWidgetItem* drives = new QTreeWidgetItem(tree_, QStringList(QStringLiteral("Virtual Pendrives")));
    folders->setData(0, NameRole, QStringLiteral("Folders"));           // the label carries a size that changes; this keeps the collapsed state
    drives->setData(0, NameRole, QStringLiteral("Virtual Pendrives"));
    folders->setFlags(Qt::ItemIsEnabled);
    lists->setFlags(Qt::ItemIsEnabled);
    drives->setFlags(Qt::ItemIsEnabled);
    QTreeWidgetItem* settings = new QTreeWidgetItem(tree_, QStringList(QStringLiteral("User Settings")));
    settings->setFlags(Qt::ItemIsEnabled);
    drives->setData(0, KindRole, QStringLiteral("drives"));
    QTreeWidgetItem* restore = nullptr;
    const QIcon folderIcon(QStringLiteral(":/img/trakcrate-22x22.png"));
    const QIcon playlistIcon(QStringLiteral(":/img/playlist-22x22.png"));
    const QIcon pendriveIcon(QStringLiteral(":/img/pendrive-22x22.png"));
    auto add = [&](QTreeWidgetItem* parent, const char* kind, const std::string& name) {
        auto* it = new QTreeWidgetItem(parent, QStringList(QString::fromStdString(name)));
        it->setData(0, KindRole, QString::fromLatin1(kind));
        it->setData(0, NameRole, QString::fromStdString(name));
        it->setIcon(0, std::strcmp(kind, "folder") == 0 ? folderIcon : playlistIcon);
        if (keepKind == QLatin1String(kind) && keepName == QString::fromStdString(name)) restore = it;
    };
    if (lib_.isOpen()) {
        std::vector<trakcrate::fs::path> everything;
        for (const std::string& f : lib_.listFolders()) {
            add(folders, "folder", f);
            for (const std::string& t : lib_.listTracks(f)) everything.push_back(lib_.trackDir(f, t));
        }
        folders->setText(0, QStringLiteral("Folders (%1)").arg(QString::fromStdString(trakcrate::formatFolderSize(estimateBytes(everything)))));
        folders->setToolTip(0, QStringLiteral("Estimated disk space of all the folders: the audio plus an allowance for the analysis files"));
        for (const std::string& p : lib_.listPlaylists()) add(lists, "playlist", p);
        // user defined pendrives, each with its entries (the playlists that will be on it, in order)
        for (const std::string& dname : lib_.listDrives()) {
            auto* d = new QTreeWidgetItem(drives, QStringList(QString::fromStdString(dname)));
            d->setData(0, KindRole, QStringLiteral("drive"));
            d->setIcon(0, pendriveIcon);
            d->setData(0, NameRole, QString::fromStdString(dname));
            if (keepKind == QLatin1String("drive") && keepName == QString::fromStdString(dname)) restore = d;
            int index = 0;
            std::vector<trakcrate::fs::path> onDrive;
            const std::vector<trakcrate::Library::DriveEntry> entries = lib_.driveEntries(dname);
            const trakcrate::Library dl = lib_.driveLibrary(dname);
            for (const trakcrate::Library::DriveEntry& e : entries)
                for (const trakcrate::fs::path& t : dl.playlistTracks(e.dir)) onDrive.push_back(t);
            d->setText(0, QStringLiteral("%1 (%2)").arg(QString::fromStdString(dname), QString::fromStdString(trakcrate::formatPendriveSize(estimateBytes(onDrive)))));
            d->setToolTip(0, QStringLiteral("Smallest pendrive, in whole gigabytes (rounded up), that holds this export"));
            for (const trakcrate::Library::DriveEntry& e : entries) {
                auto* it = new QTreeWidgetItem(d, QStringList(QStringLiteral("%1 (%2)").arg(QString::fromStdString(e.name)).arg(e.tracks)));
                it->setData(0, KindRole, QStringLiteral("entry"));
                it->setData(0, NameRole, QString::fromStdString(e.dir));
                it->setData(0, DriveRole, QString::fromStdString(dname));
                it->setData(0, IndexRole, index++);
                it->setIcon(0, playlistIcon);
                it->setToolTip(0, QString::fromStdString(e.kind.empty() ? e.name : e.kind + ": " + e.source));
                if (keepKind == QLatin1String("entry") && keepName == QString::fromStdString(e.dir) && keepDrive == QString::fromStdString(dname)) restore = it;
            }
        }
    }
    // the settings sets: the player support files copied onto every pendrive, one set per child
    if (lib_.isOpen()) {
        const QIcon settingsIcon(QStringLiteral(":/img/user-settings-22x22.png"));
        const std::vector<trakcrate::SettingsSet> sets = trakcrate::listSettingsSets(lib_);
        settings->setData(0, KindRole, QStringLiteral("settings"));
        settings->setData(0, NameRole, QStringLiteral("User Settings"));
        for (const trakcrate::SettingsSet& set : sets) {
            const QString nm = QString::fromStdString(set.name);
            auto* si = new QTreeWidgetItem(settings, QStringList(set.isDefault ? nm + QStringLiteral(" (default)") : nm));
            si->setIcon(0, settingsIcon);
            si->setData(0, KindRole, QStringLiteral("settings-set"));
            si->setData(0, NameRole, nm);
            if (set.isDefault) { QFont f = si->font(0); f.setBold(true); si->setFont(0, f); }
            QStringList tip;
            if (!set.userName.empty()) tip << QStringLiteral("Name on the players: %1").arg(QString::fromStdString(set.userName));
            if (!set.source.empty()) tip << QStringLiteral("Source: %1").arg(QString::fromStdString(set.source));
            si->setToolTip(0, tip.join(QLatin1Char('\n')));
            std::vector<std::string> names;
            std::error_code ec;
            for (trakcrate::fs::recursive_directory_iterator it(trakcrate::settingsSetDir(lib_, set.name), ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec) && it->path().filename() != "set.txt")
                    names.push_back(it->path().lexically_relative(trakcrate::settingsSetDir(lib_, set.name)).generic_string());
            std::sort(names.begin(), names.end());
            for (const std::string& n : names) {
                auto* it = new QTreeWidgetItem(si, QStringList(QString::fromStdString(n)));
                it->setIcon(0, settingsIcon);
                it->setData(0, KindRole, QStringLiteral("setting"));
                it->setData(0, DriveRole, nm);                                  // the set it belongs to
            }
        }
        if (sets.empty()) {
            auto* hint = new QTreeWidgetItem(settings, QStringList(QStringLiteral("None yet: exports use the built in files. Double-click to create.")));
            hint->setData(0, KindRole, QStringLiteral("settings-hint"));
            hint->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        }
    }
    {   // only the lists of a pendrive can be dragged
        std::function<void(QTreeWidgetItem*)> flag = [&](QTreeWidgetItem* it) {
            if (it->data(0, KindRole).toString() != QLatin1String("entry")) it->setFlags(it->flags() & ~Qt::ItemIsDragEnabled);
            for (int i = 0; i < it->childCount(); ++i) flag(it->child(i));
        };
        for (int i = 0; i < tree_->topLevelItemCount(); ++i) flag(tree_->topLevelItem(i));
    }
    tree_->expandAll();
    if (!hadItems) {                                     // first time the view is shown: Playlists open, Virtual Pendrives one level, the rest closed
        folders->setExpanded(false);
        settings->setExpanded(false);
        for (int i = 0; i < settings->childCount(); ++i) settings->child(i)->setExpanded(false);
        for (int i = 0; i < drives->childCount(); ++i) drives->child(i)->setExpanded(false);
    } else {
        std::function<void(QTreeWidgetItem*)> fold = [&](QTreeWidgetItem* it) {
            if (collapsed.contains(keyOf(it))) it->setExpanded(false);
            for (int i = 0; i < it->childCount(); ++i) fold(it->child(i));
        };
        for (int i = 0; i < tree_->topLevelItemCount(); ++i) fold(tree_->topLevelItem(i));
    }
    // setCurrentItem scrolls to the item; put the view back where it was afterwards
    struct KeepScroll { QTreeWidget* t; int pos; bool on; ~KeepScroll() { if (on) t->verticalScrollBar()->setValue(pos); } } keepScroll{tree_, scrollPos, hadItems};
    if (!selectionSignals) {                             // keep the open list as it is: the caller decides whether it needs reloading
        if (restore) tree_->setCurrentItem(restore);
        return;
    }
    block.unblock();
    if (restore) tree_->setCurrentItem(restore);
    else onTreeSelection();
}

void TrakcratePanel::setSidebarWidth(int pixels)
{
    auto* splitter = findChild<QSplitter*>();
    if (!splitter || splitter->count() < 3) return;
    const int total = std::max(splitter->width(), pixels + 400);
    splitter->setSizes({pixels, std::max(300, (total - pixels) * 3 / 4), std::max(150, (total - pixels) / 4)});
}

void TrakcratePanel::liveRefresh()
{
    if (!lib_.isOpen()) return;
    rebuildTree(false);
    QTreeWidgetItem* cur = tree_->currentItem();
    if (!cur) return;
    const QString kind = cur->data(0, KindRole).toString();
    const std::string name = cur->data(0, NameRole).toString().toStdString();
    size_t now = 0;
    if (kind == QLatin1String("folder")) now = lib_.listTracks(name).size();
    else if (kind == QLatin1String("playlist")) now = lib_.playlistTracks(name).size();
    else return;                                                    // pendrives are only changed by the user, not by a running import
    if (now != size_t(model_->rowCount())) onTreeSelection();       // tracks arrived: reload (this clears the table selection, so only when it changed)
}

void TrakcratePanel::onTreeSelection()
{
    QTreeWidgetItem* cur = tree_->currentItem();
    const QString kind = cur ? cur->data(0, KindRole).toString() : QString();
    const QString name = cur ? cur->data(0, NameRole).toString() : QString();
    if (kind == QLatin1String("folder")) model_->showFolder(lib_, name);
    else if (kind == QLatin1String("playlist")) model_->showPlaylist(lib_, name);
    else if (kind == QLatin1String("entry")) model_->showPlaylist(lib_.driveLibrary(cur->data(0, DriveRole).toString().toStdString()), name);
    else model_->clear();
    showDetails(QModelIndex());
    // A playlist is shown in its own order, always: that order is the DJ's work. Only folders can be sorted by a column.
    const bool ordered = kind == QLatin1String("playlist") || kind == QLatin1String("entry");
    if (ordered) {
        table_->setSortingEnabled(false);
        table_->sortByColumn(-1, Qt::AscendingOrder);
        proxy_->sort(-1);
        table_->header()->setSortIndicatorShown(false);
    } else if (!table_->isSortingEnabled()) {
        table_->setSortingEnabled(true);
        table_->sortByColumn(-1, Qt::AscendingOrder);
    }
    model_->setDraggable(ordered);
    table_->setDragEnabled(ordered);
    table_->setAcceptDrops(ordered);
    table_->setDragDropMode(ordered ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
    preview_->setAllowedHint(kind == QLatin1String("playlist") || kind == QLatin1String("entry") || kind == QLatin1String("drive"));
    // a different list was chosen: highlight its first track so the artwork and details are shown straight away
    const QString key = kind + QLatin1Char('\x1f') + name + QLatin1Char('\x1f') + (cur ? cur->data(0, DriveRole).toString() : QString());
    if (key != shownList_) {
        shownList_ = key;
        if (proxy_->rowCount() > 0) table_->setCurrentIndex(proxy_->index(0, 0));
    }
}

QString TrakcratePanel::currentListKey() const
{
    QTreeWidgetItem* cur = tree_->currentItem();
    if (!cur) return QString();
    return cur->data(0, KindRole).toString() + QLatin1Char('\x1f') + cur->data(0, NameRole).toString() + QLatin1Char('\x1f') + cur->data(0, DriveRole).toString();
}

// "Next playlist": the list after the one that just played, in the order of the tree (playlists, or the lists of the same pendrive). Empty lists are
// skipped. A pendrive as a whole already plays all its lists, so it has no next. Nothing after it: the player stops.
void TrakcratePanel::onPreviewEnd(const QString& owner)
{
    const QString kind = owner.section(QLatin1Char('\x1f'), 0, 0), name = owner.section(QLatin1Char('\x1f'), 1, 1), drive = owner.section(QLatin1Char('\x1f'), 2, 2);
    if (kind != QLatin1String("playlist") && kind != QLatin1String("entry")) { preview_->stop(); return; }
    QTreeWidgetItem* parent = nullptr;
    int at = -1;
    for (int g = 0; g < tree_->topLevelItemCount() && at < 0; ++g) {
        QList<QTreeWidgetItem*> candidates;
        QTreeWidgetItem* top = tree_->topLevelItem(g);
        if (kind == QLatin1String("playlist")) candidates << top;
        else for (int d = 0; d < top->childCount(); ++d) candidates << top->child(d);
        for (QTreeWidgetItem* c : candidates)
            for (int i = 0; i < c->childCount(); ++i) {
                QTreeWidgetItem* it = c->child(i);
                if (it->data(0, KindRole).toString() == kind && it->data(0, NameRole).toString() == name && it->data(0, DriveRole).toString() == drive) { parent = c; at = i; }
            }
    }
    if (!parent) { preview_->stop(); return; }
    for (int i = at + 1; i < parent->childCount(); ++i) {
        tree_->setCurrentItem(parent->child(i));            // the list that plays is the one shown
        QString err;
        if (startPreview(-1, &err)) return;
    }
    preview_->stop();
}

void TrakcratePanel::setPlaybackBlocker(std::function<QString()> blocker) { preview_->setBlocker(std::move(blocker)); }

void TrakcratePanel::stopPreview() { preview_->stop(); }

// Plays what the tree has chosen: a playlist, a list of a pendrive, or (on the pendrive itself) all its lists one after the other. The order is the
// one on screen. proxyRow is where to start (-1: the first track).
bool TrakcratePanel::startPreview(int proxyRow, QString* err)
{
    QTreeWidgetItem* cur = tree_->currentItem();
    if (!lib_.isOpen() || !cur) { if (err) *err = QStringLiteral("choose a playlist or a pendrive"); return false; }
    const QString kind = cur->data(0, KindRole).toString();
    QStringList dirs;
    previewMap_.clear();
    int start = 0;
    if (kind == QLatin1String("playlist") || kind == QLatin1String("entry")) {
        for (int r = 0; r < proxy_->rowCount(); ++r) dirs << model_->trackDir(proxy_->mapToSource(proxy_->index(r, 0)).row());
        if (proxyRow > 0 && proxyRow < dirs.size()) start = proxyRow;
    } else if (kind == QLatin1String("drive")) {
        const std::string drive = cur->data(0, NameRole).toString().toStdString();
        const trakcrate::Library dl = lib_.driveLibrary(drive);
        for (const trakcrate::Library::DriveEntry& e : lib_.driveEntries(drive)) {
            int row = 0;
            for (const trakcrate::fs::path& t : dl.playlistTracks(e.dir)) {
                dirs << QString::fromStdString(t.string());
                previewMap_.push_back({QString::fromStdString(e.dir), row++});
            }
        }
    } else {
        if (err) *err = QStringLiteral("choose a playlist or a pendrive to play");
        return false;
    }
    dirs.removeAll(QString());
    return preview_->playList(currentListKey(), dirs, start, err);
}

// The track changed: show it. The view is only moved when it is the one playing (a pendrive moves from list to list); browsing elsewhere is left alone.
void TrakcratePanel::onPreviewTrack(const QString& owner, int index, const QString& dir)
{
    if (owner.startsWith(QLatin1String("drive")) && index >= 0 && size_t(index) < previewMap_.size()) {
        const PreviewItem it = previewMap_[size_t(index)];
        QTreeWidgetItem* cur = tree_->currentItem();
        QString driveName = owner.section(QLatin1Char('\x1f'), 1, 1);
        bool onEntry = cur && cur->data(0, KindRole).toString() == QLatin1String("entry") && cur->data(0, NameRole).toString() == it.entryDir
                       && cur->data(0, DriveRole).toString() == driveName;
        bool onDrive = cur && currentListKey() == owner;
        if (!onEntry && (onDrive || (cur && cur->data(0, KindRole).toString() == QLatin1String("entry") && cur->data(0, DriveRole).toString() == driveName))) {
            for (int g = 0; g < tree_->topLevelItemCount(); ++g)
                for (int d = 0; d < tree_->topLevelItem(g)->childCount(); ++d) {
                    QTreeWidgetItem* di = tree_->topLevelItem(g)->child(d);
                    if (di->data(0, KindRole).toString() != QLatin1String("drive") || di->data(0, NameRole).toString() != driveName) continue;
                    for (int e = 0; e < di->childCount(); ++e)
                        if (di->child(e)->data(0, NameRole).toString() == it.entryDir) { tree_->setCurrentItem(di->child(e)); onEntry = true; }
                }
        }
        if (!onEntry) return;
    } else if (currentListKey() != owner) {
        return;
    }
    for (int r = 0; r < proxy_->rowCount(); ++r)
        if (model_->trackDir(proxy_->mapToSource(proxy_->index(r, 0)).row()) == dir) {
            table_->setCurrentIndex(proxy_->index(r, 0));
            table_->selectionModel()->select(proxy_->index(r, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            table_->scrollTo(proxy_->index(r, 0));
            return;
        }
}

void TrakcratePanel::selectFolder(const QString& name)
{
    for (int g = 0; g < tree_->topLevelItemCount(); ++g)
        for (int i = 0; i < tree_->topLevelItem(g)->childCount(); ++i) {
            QTreeWidgetItem* it = tree_->topLevelItem(g)->child(i);
            if (it->data(0, KindRole).toString() == QLatin1String("folder") && it->data(0, NameRole).toString() == name) tree_->setCurrentItem(it);
        }
}

void TrakcratePanel::selectPlaylist(const QString& name)
{
    for (int g = 0; g < tree_->topLevelItemCount(); ++g)
        for (int i = 0; i < tree_->topLevelItem(g)->childCount(); ++i) {
            QTreeWidgetItem* it = tree_->topLevelItem(g)->child(i);
            if (it->data(0, KindRole).toString() == QLatin1String("playlist") && it->data(0, NameRole).toString() == name) tree_->setCurrentItem(it);
        }
}

int TrakcratePanel::visibleTrackCount() const { return proxy_->rowCount(); }

// ---- details --------------------------------------------------------------------------------------------------------------------------

void TrakcratePanel::onTrackSelection() { showDetails(table_->currentIndex()); }

void TrakcratePanel::showDetails(const QModelIndex& proxyIndex)
{
    trakcrate::Track t;
    if (!proxyIndex.isValid() || !model_->track(proxy_->mapToSource(proxyIndex).row(), &t)) {
        artwork_->setPixmap(QPixmap());
        artwork_->setText(QStringLiteral("no artwork"));
        details_->setText(QString());
        return;
    }
    QPixmap pix;
    for (const char* name : {"artwork.jpg", "artwork.jpeg", "artwork.png"}) {
        if (pix.load(QString::fromStdString((t.dir / name).string()))) break;
    }
    if (pix.isNull()) {
        artwork_->setPixmap(QPixmap());
        artwork_->setText(QStringLiteral("no artwork"));
    } else {
        artwork_->setPixmap(pix.scaled(artwork_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    auto row = [&](const char* label, const std::string& value) {
        return value.empty() ? QString()
                             : QStringLiteral("<b>%1</b> %2<br>").arg(QString::fromLatin1(label), QString::fromStdString(value).toHtmlEscaped());
    };
    const trakcrate::KvDoc& m = t.meta;
    QString html;
    html += QStringLiteral("<big><b>%1</b></big><br>").arg(QString::fromStdString(m.get("title", t.dir.filename().string())).toHtmlEscaped());
    html += row("Artist", m.get("artist"));
    html += row("Album", m.get("album"));
    html += row("Genre", m.get("genre"));
    html += row("Year", m.get("year"));
    trakcrate::BeatGrid g = trakcrate::beatGridFrom(t.grid);
    if (t.hasGrid && !g.empty()) html += QStringLiteral("<b>BPM</b> %1<br>").arg(g.bpm, 0, 'f', 2);
    {   // always five stars: solid for the rating, hollow for the rest
        const int stars = int(std::max(0LL, std::min(5LL, m.getInt("rating", 0))));
        html += QStringLiteral("<b>Rating</b> <span style=\"color:#f0b414\">%1</span><span style=\"color:#8c8c8c\">%2</span><br>")
                    .arg(QString(stars, QChar(0x2605)), QString(5 - stars, QChar(0x2606)));
    }
    html += row("Comment", m.get("comment"));
    html += row("Format", m.get("container") + " " + m.get("sample_rate") + " Hz");
    html += QStringLiteral("<b>Cuemarks</b> %1<br>").arg(int(t.cuemarks.marks.size()));
    html += QStringLiteral("<br><small>%1</small>").arg(QString::fromStdString(t.dir.string()).toHtmlEscaped());
    details_->setText(html);
}

// ---- importing ------------------------------------------------------------------------------------------------------------------------

void TrakcratePanel::rekordboxImportDialog()
{
    if (!lib_.isOpen()) return;
    TrakcrateRbImportDialog dlg(&lib_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    if (!importRekordbox(dlg.folder(), dlg.options()))
        QMessageBox::information(this, QStringLiteral("trakcrate"), QStringLiteral("Another job is still running."));
}

bool TrakcratePanel::importRekordbox(const QString& driveRoot, const trakcrate::RbImportOptions& opts)
{
    if (!lib_.isOpen() || jobs_->busy()) return false;
    rb_ = std::make_shared<trakcrate::RbImportReport>();
    report_.reset(); analysis_.reset(); bpm_.reset(); export_.reset();
    std::shared_ptr<trakcrate::RbImportReport> rep = rb_;
    const trakcrate::Library lib = lib_;
    const trakcrate::fs::path root = driveRoot.toStdString();
    const trakcrate::RbImportOptions o = opts;
    watchSuspended_ = true;
    setBusyUi(true);
    progressLabel_->setText(QStringLiteral("Importing the rekordbox drive %1").arg(driveRoot));
    const bool started = jobs_->start(QStringLiteral("Import drive"), [rep, lib, root, o](const std::atomic<bool>& cancel,
                                                                                          const TrakcrateJobRunner::Progress& progress) {
        *rep = trakcrate::importRekordboxDrive(lib, root, o, [&](int d, int t, const std::string& c) { progress(d, t, c); }, &cancel);
    });
    if (!started) { rb_.reset(); watchSuspended_ = false; setBusyUi(false); }
    return started;
}

void TrakcratePanel::importDialog()
{
    if (!lib_.isOpen()) return;
    TrakcrateImportDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    if (!importFolder(dlg.folder(), dlg.options()))
        QMessageBox::information(this, QStringLiteral("trakcrate"), QStringLiteral("Another job is still running."));
}

bool TrakcratePanel::importFolder(const QString& folder, const trakcrate::ImportOptions& opts)
{
    if (!lib_.isOpen() || jobs_->busy()) return false;
    report_ = std::make_shared<trakcrate::ImportReport>();
    analysis_.reset(); bpm_.reset(); rb_.reset();
    export_.reset();
    std::shared_ptr<trakcrate::ImportReport> rep = report_;
    const trakcrate::Library lib = lib_;
    const trakcrate::fs::path dir = folder.toStdString();
    const trakcrate::ImportOptions o = opts;
    watchSuspended_ = true;                              // the library changes under us; refresh once at the end
    setBusyUi(true);
    progressLabel_->setText(QStringLiteral("Importing %1").arg(folder));
    const bool started = jobs_->start(QStringLiteral("Import"), [rep, lib, dir, o](const std::atomic<bool>& cancel,
                                                                                      const TrakcrateJobRunner::Progress& progress) {
        *rep = trakcrate::importFolder(lib, dir, o, progress, &cancel);
    });
    if (!started) { watchSuspended_ = false; setBusyUi(false); }
    return started;
}

void TrakcratePanel::deleteOrphansDialog()
{
    if (orphans_.empty() || jobs_->busy()) return;
    uint64_t bytes = 0;
    QStringList list;
    for (const trakcrate::Orphan& o : orphans_) {
        bytes += o.size;
        list << QStringLiteral("%1  (%2)").arg(QString::fromStdString(o.rel), QLocale().formattedDataSize(qint64(o.size)));
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("Orphaned files"));
    box.setText(QStringLiteral("%1 file(s), %2, on %3 are not used by the database any more (tracks, artwork and analysis files of earlier exports).\n\n"
                               "Delete them to free space? This cannot be undone.").arg(orphans_.size()).arg(QLocale().formattedDataSize(qint64(bytes)), exportDest_));
    box.setDetailedText(list.join(QLatin1Char('\n')));
    QPushButton* del = box.addButton(QStringLiteral("Delete orphans"), QMessageBox::DestructiveRole);
    QPushButton* keep = box.addButton(QStringLiteral("Keep"), QMessageBox::RejectRole);
    box.setDefaultButton(keep);
    box.exec();
    if (box.clickedButton() != del) return;
    std::vector<std::string> failed;
    const uint64_t freed = trakcrate::deleteOrphans(exportDest_.toStdString(), orphans_, &failed);
    orphans_.clear();
    orphanButton_->hide();
    QString msg = QStringLiteral("Deleted orphans, %1 freed").arg(QLocale().formattedDataSize(qint64(freed)));
    if (!failed.empty()) msg += QStringLiteral(" (%1 could not be removed)").arg(failed.size());
    progressLabel_->setText(msg);
}

void TrakcratePanel::setBusyUi(bool busy)
{
    progress_->setVisible(busy);
    cancelButton_->setVisible(busy);
    if (busy) { orphanButton_->hide(); orphans_.clear(); }
    if (busy) { progress_->setRange(0, 0); progress_->setValue(0); }
    importButton_->setEnabled(!busy && lib_.isOpen());
    exportButton_->setEnabled(!busy && lib_.isOpen());
    rbImportButton_->setEnabled(!busy && lib_.isOpen());
    newPendriveButton_->setEnabled(!busy && lib_.isOpen());
}

void TrakcratePanel::onJobProgress(int done, int total, const QString& current)
{
    progress_->setRange(0, total > 0 ? total : 0);
    progress_->setValue(done);
    if (!current.isEmpty()) progressLabel_->setText(current);
    // the database write cannot be interrupted (it must be whole to be of use)
    if (export_ && current.endsWith(QStringLiteral("rekordbox/export.pdb"))) cancelButton_->setVisible(false);
    // an import fills the library as it goes: show the new folders, playlists and tracks every couple of seconds
    if ((report_ || rb_) && (!liveTimer_.isValid() || liveTimer_.elapsed() > 2000)) {
        liveTimer_.restart();
        liveRefresh();
    }
}

void TrakcratePanel::onJobFinished(const QString&, bool cancelled)
{
    watchSuspended_ = false;
    setBusyUi(false);
    refresh();
    QString summary;
    bool ok = false;
    QString detail;
    if (export_) {
        const trakcrate::ExportResult& r = *export_;
        ok = r.ok;
        if (r.ok) {
            summary = QStringLiteral("Exported %1 track(s) in %2 playlist(s) to %3 (%4 files, %5 MiB%6)")
                          .arg(r.tracks).arg(r.playlists).arg(exportDest_).arg(r.files).arg(qint64(r.bytesWritten >> 20))
                          .arg(r.verified ? QStringLiteral(", verified") : QString());
            if (r.skipped > 0) summary += QStringLiteral("; %1 unchanged file(s) skipped").arg(r.skipped);
            if (!r.settingsWritten.empty() || !r.settingsKept.empty())
                summary += QStringLiteral("; settings: %1 written, %2 kept").arg(r.settingsWritten.size()).arg(r.settingsKept.size());
            if (!r.backup.empty()) summary += QStringLiteral("; old database kept as %1").arg(QString::fromStdString(r.backup));
            if (!r.orphans.empty()) {
                uint64_t bytes = 0;
                for (const trakcrate::Orphan& o : r.orphans) bytes += o.size;
                orphans_ = r.orphans;
                summary += QStringLiteral("; %1 orphaned file(s) on the drive (%2)").arg(r.orphans.size()).arg(QLocale().formattedDataSize(qint64(bytes)));
                orphanButton_->setText(QStringLiteral("Orphans (%1)...").arg(r.orphans.size()));
                orphanButton_->setToolTip(QStringLiteral("Tracks, artwork and analysis files from earlier exports that the database no longer uses"));
                orphanButton_->show();
            }
        } else {
            summary = r.cancelled ? QStringLiteral("Export cancelled") : QStringLiteral("Export failed: %1").arg(QString::fromStdString(r.error));
        }
        for (const std::string& w : r.warnings) detail += QString::fromStdString(w) + QLatin1Char('\n');
        progressLabel_->setText(summary);
        progressLabel_->setToolTip(detail);
        emit exportFinished(summary, ok, exportDest_);
        if (ok && !exportRegisterName_.isEmpty()) emit registerPendriveRequested(exportRegisterName_, exportDest_);
        return;
    }
    if (rb_) {
        const trakcrate::RbImportReport r = *rb_;
        rb_.reset();
        if (!r.ok) {
            summary = QStringLiteral("Drive import failed: %1").arg(QString::fromStdString(r.error));
        } else {
            ok = true;
            summary = QStringLiteral("%1 %2 of %3 track(s) (%4 already there, %5 without audio, %6 failed): %7 grids, %8 with cues, %9 waveforms, %10 artwork, %11 playlist(s)")
                          .arg(r.imported).arg(cancelled || r.cancelled ? QStringLiteral("imported (cancelled)") : QStringLiteral("imported")).arg(r.tracks)
                          .arg(r.duplicates).arg(r.missingAudio).arg(r.failed).arg(r.withGrid).arg(r.withCues).arg(r.withWaveform).arg(r.withArtwork)
                          .arg(r.playlists);
            if (r.settingsFiles) summary += QStringLiteral(", %1 settings file(s)").arg(r.settingsFiles);
        }
        QString detail;
        for (const std::string& w : r.warnings) detail += QString::fromStdString(w) + QLatin1Char('\n');
        progressLabel_->setText(summary);
        progressLabel_->setToolTip(detail);
        emit rekordboxImportFinished(summary, ok);
        return;
    }
    if (bpm_) {
        std::shared_ptr<trakcrate::BpmDetection> found = std::move(bpm_);
        const QString dir = bpmDir_;
        if (cancelled || !found->ok) {
            summary = cancelled ? QStringLiteral("BPM detection cancelled")
                                : QStringLiteral("BPM detection failed: %1").arg(QString::fromStdString(found->error));
        } else if (found->bpm <= 0) {
            summary = QStringLiteral("No tempo found; enter the BPM by hand");
        } else {
            double chosen = found->bpm;
            bool replace = false;
            bool accepted = false;
            if (bpmPrompt_) {
                accepted = bpmPrompt_(dir, *found, &chosen, &replace);
            } else {
                trakcrate::Track t;
                trakcrate::loadTrack(dir.toStdString(), &t);
                const bool hasGrid = t.hasGrid && !trakcrate::beatGridFrom(t.grid).empty();
                QString title = QString::fromStdString(t.title());
                if (title.isEmpty()) title = QString::fromStdString(trakcrate::fs::path(dir.toStdString()).filename().string());
                TrakcrateBpmDialog dlg(title, found->bpm, found->confidence, t.meta.getDouble("bpm_tag", 0), hasGrid, this);
                accepted = dlg.exec() == QDialog::Accepted;
                chosen = dlg.bpm();
                replace = dlg.replaceGrid();
            }
            QString err;
            if (!accepted) summary = QStringLiteral("BPM rejected, nothing changed");
            else if (bool written = false; applyBpm(dir, chosen, found->firstBeatUs, replace, &err, &written)) {
                summary = written ? QStringLiteral("BPM set to %1, beat grid written").arg(chosen, 0, 'f', 2)
                                  : QStringLiteral("Beat grid kept as it was (%1 not applied)").arg(chosen, 0, 'f', 2);
                ok = true;
            } else summary = QStringLiteral("Could not save the BPM: %1").arg(err);
        }
        progressLabel_->setText(summary);
        emit bpmFinished(summary, ok);
        refresh();
        return;
    }
    if (analysis_) {
        int done = 0, skipped = 0, failed = 0;
        for (const trakcrate::AnalyseResult& r : *analysis_) {
            if (!r.ok) { ++failed; detail += QStringLiteral("failed: %1\n").arg(QString::fromStdString(r.error)); }
            else if (r.beatgridWritten || r.waveformWritten) ++done;
            else ++skipped;
            for (const std::string& s : r.skipped) detail += QStringLiteral("skipped %1\n").arg(QString::fromStdString(s));
        }
        ok = failed == 0;
        summary = QStringLiteral("Analysed %1, %2 left as they were, %3 failed%4").arg(done).arg(skipped).arg(failed)
                      .arg(cancelled ? QStringLiteral(" (cancelled)") : QString());
        progressLabel_->setText(summary);
        progressLabel_->setToolTip(detail);
        emit analysisFinished(summary, ok);
        return;
    }
    if (report_) {
        const trakcrate::ImportReport& r = *report_;
        ok = r.ok;
        if (!r.ok) {
            summary = QStringLiteral("Import failed: %1").arg(QString::fromStdString(r.error));
        } else {
            summary = QStringLiteral("Imported %1, %2 duplicate, %3 skipped, %4 failed").arg(r.imported).arg(r.duplicates).arg(r.skipped).arg(r.failed);
            if (!r.playlist.empty()) summary += QStringLiteral(", playlist \"%1\"").arg(QString::fromStdString(r.playlist));
            if (cancelled || r.cancelled) summary += QStringLiteral(" (cancelled)");
        }
        for (const trakcrate::ImportItem& it : r.items)
            if (it.status == "failed" || it.status == "skipped")
                detail += QStringLiteral("%1: %2 (%3)\n").arg(QString::fromStdString(it.status), QString::fromStdString(it.source.string()),
                                                                QString::fromStdString(it.detail));
    }
    progressLabel_->setText(summary);
    progressLabel_->setToolTip(detail);
    emit importFinished(summary, ok);
}

// ---- export ----------------------------------------------------------------------------------------------------------------------

void TrakcratePanel::exportDialog()
{
    if (!lib_.isOpen()) return;
    TrakcrateExportDialog dlg(lib_, this, true);
    const QStringList selected = tree_->currentItem() && tree_->currentItem()->data(0, KindRole).toString() == QLatin1String("playlist")
                                     ? QStringList(tree_->currentItem()->data(0, NameRole).toString()) : QStringList();
    for (const QString& n : selected) dlg.selectPlaylist(n);
    if (dlg.exec() != QDialog::Accepted) return;
    if (!exportDrive(dlg.options(), dlg.registerAsVirtualPendrive() ? dlg.virtualPendriveName() : QString()))
        QMessageBox::information(this, QStringLiteral("trakcrate"), QStringLiteral("Another job is still running, or the template database is missing."));
}

bool TrakcratePanel::exportDrive(trakcrate::ExportOptions opts, const QString& registerName)
{
    if (!lib_.isOpen() || jobs_->busy()) return false;
    QFile tpl(QStringLiteral(":/trakcrate/template-export.pdb"));
    if (!tpl.open(QIODevice::ReadOnly)) return false;
    opts.templatePdb.assign(tpl.readAll().constData(), size_t(tpl.size()));
    opts.encodeArtwork = [](const std::string& src, int size, std::string* jpeg, std::string* err) {
        QString e;
        const QByteArray out = encodeArtworkJpeg(QByteArray(src.data(), int(src.size())), size, &e);
        if (out.isEmpty()) { *err = e.toStdString(); return false; }
        jpeg->assign(out.constData(), size_t(out.size()));
        return true;
    };
    if (opts.includeSettings && !opts.settingsBuiltIn && trakcrate::listSettingsSets(lib_).empty()) {
        // a library always has settings after its first export: made from the built in files with the user's login name on the profile
        std::string e;
        if (trakcrate::createSettingsSet(lib_, "Default", defaultSettingsUserName().toStdString(), &e)) rebuildTree(false);
    }
    // the player support files come from opts.settingsSet (empty = the library's default settings set), resolved by exportDrive
    export_ = std::make_shared<trakcrate::ExportResult>();
    analysis_.reset(); bpm_.reset(); rb_.reset();
    report_.reset();
    exportDest_ = QString::fromStdString(opts.dest.string());
    exportRegisterName_ = registerName;
    std::shared_ptr<trakcrate::ExportResult> result = export_;
    const trakcrate::Library lib = lib_;
    watchSuspended_ = true;
    setBusyUi(true);
    progressLabel_->setText(QStringLiteral("Exporting to %1").arg(exportDest_));
    const bool started = jobs_->start(QStringLiteral("Export"), [result, lib, opts](const std::atomic<bool>& cancel, const TrakcrateJobRunner::Progress& progress) {
        *result = trakcrate::exportDrive(lib, opts, progress, &cancel);
    });
    if (!started) { watchSuspended_ = false; setBusyUi(false); }
    return started;
}

// ---- analysis ---------------------------------------------------------------------------------------------------------------------

QStringList TrakcratePanel::selectedTrackDirs() const
{
    QStringList dirs;
    for (const QModelIndex& idx : table_->selectionModel()->selectedRows()) {
        const QString d = model_->trackDir(proxy_->mapToSource(idx).row());
        if (!d.isEmpty()) dirs << d;
    }
    return dirs;
}

bool TrakcratePanel::analyseTracks(const QStringList& trackDirs, bool force)
{
    if (!lib_.isOpen() || jobs_->busy() || trackDirs.isEmpty()) return false;
    analysis_ = std::make_shared<std::vector<trakcrate::AnalyseResult>>();
    report_.reset();
    export_.reset();
    std::shared_ptr<std::vector<trakcrate::AnalyseResult>> results = analysis_;
    std::vector<trakcrate::fs::path> paths;
    for (const QString& d : trackDirs) paths.push_back(d.toStdString());
    trakcrate::AnalyseOptions opts;
    opts.force = force;
    watchSuspended_ = true;
    setBusyUi(true);
    progressLabel_->setText(QStringLiteral("Analysing %1 track(s)").arg(paths.size()));
    const bool started = jobs_->start(QStringLiteral("Analyse"), [results, paths, opts](const std::atomic<bool>& cancel,
                                                                                         const TrakcrateJobRunner::Progress& progress) {
        int i = 0;
        for (const trakcrate::fs::path& p : paths) {
            progress(i++, int(paths.size()), p.filename().string());
            if (cancel.load()) break;
            results->push_back(trakcrate::analyseTrack(p, opts, &cancel));
        }
    });
    if (!started) { watchSuspended_ = false; setBusyUi(false); }
    return started;
}

bool TrakcratePanel::detectBpm(const QString& trackDir)
{
    if (!lib_.isOpen() || jobs_->busy() || trackDir.isEmpty()) return false;
    const trakcrate::fs::path audio = trakcrate::audioFileOf(trackDir.toStdString());
    if (audio.empty()) { progressLabel_->setText(QStringLiteral("No audio file in that track folder")); return false; }
    analysis_.reset();
    report_.reset();
    export_.reset();
    rb_.reset();
    bpm_ = std::make_shared<trakcrate::BpmDetection>();
    bpmDir_ = trackDir;
    std::shared_ptr<trakcrate::BpmDetection> result = bpm_;
    watchSuspended_ = true;
    setBusyUi(true);
    progressLabel_->setText(QStringLiteral("Detecting BPM of %1").arg(QString::fromStdString(audio.filename().string())));
    const bool started = jobs_->start(QStringLiteral("Detect BPM"), [result, audio](const std::atomic<bool>& cancel,
                                                                                    const TrakcrateJobRunner::Progress& progress) {
        progress(0, 1, audio.filename().string());
        *result = trakcrate::detectBpm(audio, &cancel);
    });
    if (!started) { bpm_.reset(); watchSuspended_ = false; setBusyUi(false); }
    return started;
}

bool TrakcratePanel::applyBpm(const QString& trackDir, double bpm, long long firstBeatUs, bool replaceGrid, QString* err, bool* gridWritten)
{
    if (gridWritten) *gridWritten = false;
    if (bpm <= 0) { if (err) *err = QStringLiteral("BPM must be positive"); return false; }
    trakcrate::Track t;
    std::string e;
    if (!trakcrate::loadTrack(trackDir.toStdString(), &t, &e)) { if (err) *err = QString::fromStdString(e); return false; }
    const bool missing = !t.hasGrid || trakcrate::beatGridFrom(t.grid).empty();
    if (missing || replaceGrid) {
        if (!t.hasGrid) t.grid.addComment("trakcrate beatgrid");
        t.hasGrid = true;
        trakcrate::BeatGrid g;
        g.bpm = std::round(bpm * 1000.0) / 1000.0;
        g.firstBeatUs = firstBeatUs;
        trakcrate::setBeatGrid(t.grid, g);
        t.grid.set("analyser", "aubio");
        trakcrate::setOrigin(t.source, "beatgrid", "edited");       // the user vouched for the BPM, so a re-analysis must not overwrite it
        if (!trakcrate::saveBeatGrid(t, &e) || !trakcrate::saveSource(t, &e)) { if (err) *err = QString::fromStdString(e); return false; }
        if (gridWritten) *gridWritten = true;
    }
    return true;
}

void TrakcratePanel::tableContextMenu(const QPoint& pos)
{
    const QModelIndex under = table_->indexAt(pos);
    if (under.isValid() && !table_->selectionModel()->isSelected(under))
        table_->selectionModel()->select(under, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    const QStringList dirs = selectedTrackDirs();
    QMenu menu(this);
    QAction* analyse = menu.addAction(QStringLiteral("Analyse (beat grid, waveform, loudness)"));
    QAction* force = menu.addAction(QStringLiteral("Re-analyse, replacing rekordbox/edited data"));
    QAction* detect = menu.addAction(trakcrate::bpmDetectAvailable() ? QStringLiteral("Detect BPM...")
                                                                       : QStringLiteral("Detect BPM... (needs libaubio-dev)"));
    detect->setEnabled(dirs.size() == 1 && !jobs_->busy() && trakcrate::bpmDetectAvailable());
    QAction* load = menu.addAction(loadText_);
    load->setEnabled(dirs.size() == 1);
    QAction* playHere = menu.addAction(QStringLiteral("Play the list from here"));
    playHere->setEnabled(under.isValid() && currentListKey() != QString() && currentListKey() != QLatin1String("folder"));
    menu.addSeparator();
    QAction *mvUp = nullptr, *mvDown = nullptr, *mvStart = nullptr, *mvEnd = nullptr, *entryRemove = nullptr;
    const QString curKind = tree_->currentItem() ? tree_->currentItem()->data(0, KindRole).toString() : QString();
    if (curKind == QLatin1String("playlist") || curKind == QLatin1String("entry")) {
        mvStart = menu.addAction(QStringLiteral("Move to start"));
        mvUp = menu.addAction(QStringLiteral("Move up"));
        mvDown = menu.addAction(QStringLiteral("Move down"));
        mvEnd = menu.addAction(QStringLiteral("Move to end"));
        for (QAction* a : {mvStart, mvUp, mvDown, mvEnd}) a->setEnabled(!dirs.isEmpty());
        if (curKind == QLatin1String("entry")) {
            menu.addSeparator();
            entryRemove = menu.addAction(QStringLiteral("Remove from this pendrive playlist"));
            entryRemove->setEnabled(!dirs.isEmpty());
        }
        menu.addSeparator();
    }
    analyse->setEnabled(!dirs.isEmpty() && !jobs_->busy());
    force->setEnabled(analyse->isEnabled());
    QAction* chosen = menu.exec(table_->viewport()->mapToGlobal(pos));
    if (chosen == analyse) analyseTracks(dirs, false);
    else if (chosen == force) analyseTracks(dirs, true);
    else if (chosen == load) { preview_->stop(); emit loadRequested(dirs.first()); }
    else if (chosen == playHere) { QString err; startPreview(under.isValid() ? under.row() : -1, &err); }
    else if (chosen && (chosen == mvStart || chosen == mvUp || chosen == mvDown || chosen == mvEnd)) {
        QString err;
        if (!moveSelected(chosen == mvStart ? -2 : chosen == mvUp ? -1 : chosen == mvDown ? 1 : 2, &err) && !err.isEmpty()) emit status(err);
    }
    else if (entryRemove && chosen == entryRemove) editEntryTracks(0, true);
    else if (chosen == detect) detectBpm(dirs.first());
}

// ---- user defined pendrives ---------------------------------------------------------------------------------------------------------

bool TrakcratePanel::createPendrive(const QString& name, const QStringList& folders, const QStringList& playlists, QString* err)
{
    std::string e;
    auto fail = [&](const std::string& m) { if (err) *err = QString::fromStdString(m); return false; };
    if (!lib_.isOpen()) return fail("no library");
    const std::string n = name.trimmed().toStdString();
    if (!lib_.createDrive(n, &e)) return fail(e);
    for (const QString& f : folders)
        if (!lib_.addFolderToDrive(n, f.toStdString(), &e)) { lib_.deleteDrive(n); return fail(e); }
    for (const QString& p : playlists)
        if (!lib_.addPlaylistToDrive(n, p.toStdString(), &e)) { lib_.deleteDrive(n); return fail(e); }
    refresh();
    return true;
}

bool TrakcratePanel::addToPendrive(const QString& drive, const QString& kind, const QString& name, QString* err)
{
    std::string e;
    const bool ok = kind == QLatin1String("folder") ? lib_.addFolderToDrive(drive.toStdString(), name.toStdString(), &e)
                                                    : lib_.addPlaylistToDrive(drive.toStdString(), name.toStdString(), &e);
    if (!ok && err) *err = QString::fromStdString(e);
    if (ok) refresh();
    return ok;
}

trakcrate::Library::Impact TrakcratePanel::folderImpact(const QString& folder) const
{
    std::vector<trakcrate::fs::path> dirs;
    for (const std::string& t : lib_.listTracks(folder.toStdString())) dirs.push_back(lib_.trackDir(folder.toStdString(), t));
    return lib_.impactOfRemoving(dirs);
}

bool TrakcratePanel::deleteFolder(const QString& folder, QString* err)
{
    if (!lib_.isOpen() || jobs_->busy()) { if (err) *err = QStringLiteral("a job is running"); return false; }
    const std::string f = folder.toStdString();
    if (!trakcrate::isSafeName(f) || !trakcrate::fs::is_directory(lib_.masterDir() / f)) { if (err) *err = QStringLiteral("no such folder"); return false; }
    std::vector<trakcrate::fs::path> dirs;
    for (const std::string& t : lib_.listTracks(f)) dirs.push_back(lib_.trackDir(f, t));
    // to the trash first (it can be restored from there); only when that worked are the links removed, so a failure leaves everything as it was
    if (!QFile::moveToTrash(QString::fromStdString((lib_.masterDir() / f).string()))) {
        if (err) *err = QStringLiteral("could not move the folder to the trash");
        return false;
    }
    std::string e;
    const bool ok = lib_.forgetTracks(dirs, &e);
    if (!ok && err) *err = QString::fromStdString(e);
    refresh();
    return ok;
}

void TrakcratePanel::newPendriveDialog()
{
    if (!lib_.isOpen()) return;
    TrakcratePendriveDialog dlg(lib_, this);
    if (tree_->currentItem()) {                                    // start from what is selected
        const QString kind = tree_->currentItem()->data(0, KindRole).toString(), name = tree_->currentItem()->data(0, NameRole).toString();
        if (kind == QLatin1String("folder")) dlg.tickFolder(name);
        else if (kind == QLatin1String("playlist")) dlg.tickPlaylist(name);
    }
    if (dlg.exec() != QDialog::Accepted) return;
    QString err;
    if (!createPendrive(dlg.name(), dlg.folders(), dlg.playlists(), &err))
        QMessageBox::warning(this, QStringLiteral("trakcrate"), QStringLiteral("Could not create the pendrive: %1").arg(err));
}

void TrakcratePanel::deleteFolderDialog(const QString& folder)
{
    const trakcrate::Library::Impact im = folderImpact(folder);
    QString text = QStringLiteral("Move the folder \"%1\" with its %2 track(s) to the trash?").arg(folder).arg(lib_.listTracks(folder.toStdString()).size());
    if (im.any()) {
        text += QStringLiteral("\n\nWARNING: %1 link(s) to these tracks will be removed from:\n").arg(im.links);
        for (const std::string& p : im.playlists) text += QStringLiteral("  playlist %1\n").arg(QString::fromStdString(p));
        for (const std::string& d : im.driveEntries) text += QStringLiteral("  pendrive %1\n").arg(QString::fromStdString(d));
        text += QStringLiteral("\nThe tracks disappear from those lists too.");
    }
    QMessageBox box(im.any() ? QMessageBox::Warning : QMessageBox::Question, QStringLiteral("Delete folder"), text, QMessageBox::NoButton, this);
    QPushButton* yes = box.addButton(im.any() ? QStringLiteral("Delete and remove the links") : QStringLiteral("Move to trash"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != yes) return;
    QString err;
    if (!deleteFolder(folder, &err)) QMessageBox::warning(this, QStringLiteral("trakcrate"), err);
}

void TrakcratePanel::exportPendrive(const QString& drive, bool physical)
{
    if (!lib_.isOpen()) return;
    TrakcrateExportDialog dlg(lib_, this, true);
    dlg.setPendrive(drive, physical);
    bool dangling = false;
    trakcrate::effectiveDriveSettingsSet(lib_, drive.toStdString(), &dangling);
    if (dangling) emit status(QStringLiteral("The settings linked to \"%1\" no longer exist: the default settings are used").arg(drive));
    if (dlg.exec() != QDialog::Accepted) return;
    if (!exportDrive(dlg.options(), dlg.registerAsVirtualPendrive() ? dlg.virtualPendriveName() : QString()))
        QMessageBox::information(this, QStringLiteral("trakcrate"), QStringLiteral("Another job is still running, or the template database is missing."));
}

bool TrakcratePanel::moveRows(const std::vector<int>& rows, int insertBefore, QString* err)
{
    QTreeWidgetItem* cur = tree_->currentItem();
    const QString kind = cur ? cur->data(0, KindRole).toString() : QString();
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    if (!lib_.isOpen() || (kind != QLatin1String("playlist") && kind != QLatin1String("entry"))) return fail(QStringLiteral("only a playlist can be reordered"));
    std::vector<int> src;
    for (int r : rows) src.push_back(proxy_->mapToSource(proxy_->index(r, 0)).row());      // the table is never sorted here: rows are positions
    std::string e;
    bool ok;
    if (kind == QLatin1String("playlist")) ok = lib_.moveEntries(cur->data(0, NameRole).toString().toStdString(), src, insertBefore, &e);
    else ok = lib_.driveLibrary(cur->data(0, DriveRole).toString().toStdString()).moveEntries(cur->data(0, NameRole).toString().toStdString(), src, insertBefore, &e);
    if (!ok) return fail(QString::fromStdString(e));
    // show the new order with the moved rows selected where they landed
    std::sort(src.begin(), src.end());
    src.erase(std::unique(src.begin(), src.end()), src.end());
    int before = 0;
    for (int r : src) if (r < insertBefore) ++before;
    const int first = insertBefore - before;
    onTreeSelection();
    QItemSelection sel;
    for (size_t i = 0; i < src.size(); ++i) {
        const QModelIndex a = proxy_->index(first + int(i), 0), b = proxy_->index(first + int(i), proxy_->columnCount() - 1);
        if (a.isValid()) sel.select(a, b);
    }
    table_->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    if (proxy_->index(first, 0).isValid()) table_->scrollTo(proxy_->index(first, 0));
    return true;
}

bool TrakcratePanel::moveSelected(int how, QString* err)
{
    std::vector<int> rows;
    for (const QModelIndex& i : table_->selectionModel()->selectedRows()) rows.push_back(i.row());
    std::sort(rows.begin(), rows.end());
    if (rows.empty()) return false;
    const int n = proxy_->rowCount();
    const int at = how == -2 ? 0 : how == 2 ? n : how == -1 ? std::max(0, rows.front() - 1) : std::min(n, rows.back() + 2);
    return moveRows(rows, at, err);
}

bool TrakcratePanel::moveChosenEntry(int direction, QString* err)
{
    QTreeWidgetItem* cur = tree_->currentItem();
    if (!cur || cur->data(0, KindRole).toString() != QLatin1String("entry")) return false;
    const int i = cur->data(0, IndexRole).toInt();
    return moveEntry(cur->data(0, DriveRole).toString(), i, direction < 0 ? i - 1 : i + 2, err);
}

bool TrakcratePanel::moveEntry(const QString& drive, int from, int insertBefore, QString* err)
{
    if (!lib_.isOpen()) return false;
    const int count = int(lib_.driveEntries(drive.toStdString()).size());
    int to = insertBefore > from ? insertBefore - 1 : insertBefore;
    to = std::max(0, std::min(count - 1, to));                         // Ctrl+Up on the first list, Ctrl+Down on the last: nothing to do
    if (to == from) return true;
    std::string e;
    if (!lib_.moveDriveEntry(drive.toStdString(), from, to, &e)) { if (err) *err = QString::fromStdString(e); return false; }
    rebuildTree(false);
    for (int g = 0; g < tree_->topLevelItemCount(); ++g)                    // the list that was moved stays the chosen one (its folder name changed)
        for (int d = 0; d < tree_->topLevelItem(g)->childCount(); ++d) {
            QTreeWidgetItem* di = tree_->topLevelItem(g)->child(d);
            if (di->data(0, KindRole).toString() == QLatin1String("drive") && di->data(0, NameRole).toString() == drive && to < di->childCount())
                tree_->setCurrentItem(di->child(to));
        }
    return true;
}

void TrakcratePanel::editEntryTracks(int move, bool remove)
{
    QTreeWidgetItem* cur = tree_->currentItem();
    if (!cur || cur->data(0, KindRole).toString() != QLatin1String("entry")) return;
    const std::string drive = cur->data(0, DriveRole).toString().toStdString();
    const int entry = cur->data(0, IndexRole).toInt();
    std::vector<int> rows;
    for (const QModelIndex& idx : table_->selectionModel()->selectedRows()) rows.push_back(proxy_->mapToSource(idx).row());
    if (rows.empty()) return;
    std::sort(rows.begin(), rows.end());
    std::string err;
    if (remove) {
        for (auto it = rows.rbegin(); it != rows.rend(); ++it) lib_.removeDriveTrack(drive, entry, *it, &err);
    } else if (move < 0) {
        for (int r : rows) if (r > 0) lib_.moveDriveTrack(drive, entry, r, r - 1, &err);
    } else {
        for (auto it = rows.rbegin(); it != rows.rend(); ++it) if (*it + 1 < model_->rowCount()) lib_.moveDriveTrack(drive, entry, *it, *it + 1, &err);
    }
    refresh();                                                      // keeps the entry selected
}

static bool settingsResult(bool ok, const std::string& e, QString* err)
{
    if (!ok && err) *err = QString::fromStdString(e);
    return ok;
}

bool TrakcratePanel::createSettingsSet(const QString& name, const QString& userName, QString* err)
{
    std::string e;
    const bool ok = lib_.isOpen() && trakcrate::createSettingsSet(lib_, name.toStdString(), userName.toStdString(), &e);
    if (ok) refresh();
    return settingsResult(ok, lib_.isOpen() ? e : "no library is open", err);
}

bool TrakcratePanel::copySettingsSet(const QString& name, const QString& driveFolder, QString* err)
{
    std::string e;
    const bool ok = lib_.isOpen() && trakcrate::copySettingsSetFromDrive(lib_, name.toStdString(), driveFolder.toStdString(), nullptr, &e);
    if (ok) refresh();
    return settingsResult(ok, lib_.isOpen() ? e : "no library is open", err);
}

bool TrakcratePanel::renameSettingsSet(const QString& from, const QString& to, QString* err)
{
    std::string e;
    const bool ok = lib_.isOpen() && trakcrate::renameSettingsSet(lib_, from.toStdString(), to.toStdString(), &e);
    if (ok) refresh();
    return settingsResult(ok, lib_.isOpen() ? e : "no library is open", err);
}

bool TrakcratePanel::duplicateSettingsSet(const QString& from, const QString& to, QString* err)
{
    std::string e;
    const bool ok = lib_.isOpen() && trakcrate::duplicateSettingsSet(lib_, from.toStdString(), to.toStdString(), &e);
    if (ok) refresh();
    return settingsResult(ok, lib_.isOpen() ? e : "no library is open", err);
}

bool TrakcratePanel::deleteSettingsSet(const QString& name, QString* err)
{
    std::string e;
    const bool ok = lib_.isOpen() && trakcrate::deleteSettingsSet(lib_, name.toStdString(), &e);
    if (ok) refresh();
    return settingsResult(ok, lib_.isOpen() ? e : "no library is open", err);
}

bool TrakcratePanel::makeDefaultSettingsSet(const QString& name, QString* err)
{
    std::string e;
    const bool ok = lib_.isOpen() && trakcrate::setDefaultSettingsSet(lib_, name.toStdString(), &e);
    if (ok) refresh();
    return settingsResult(ok, lib_.isOpen() ? e : "no library is open", err);
}

bool TrakcratePanel::setSettingsUserName(const QString& set, const QString& userName, QString* err)
{
    std::string e;
    const bool ok = lib_.isOpen() && trakcrate::setSettingsSetUserName(lib_, set.toStdString(), userName.toStdString(), &e);
    if (ok) refresh();
    return settingsResult(ok, lib_.isOpen() ? e : "no library is open", err);
}

bool TrakcratePanel::setPendriveSettings(const QString& drive, const QString& set, QString* err)
{
    std::string e;
    const bool ok = lib_.isOpen() && trakcrate::setDriveSettingsSet(lib_, drive.toStdString(), set.toStdString(), &e);
    if (ok) refresh();
    return settingsResult(ok, lib_.isOpen() ? e : "no library is open", err);
}

void TrakcratePanel::newSettingsDialog()
{
    if (!lib_.isOpen()) return;
    NewSettingsDialog dlg(lib_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString err;
    if (!createSettingsSet(dlg.setName(), dlg.userName(), &err)) QMessageBox::warning(this, QStringLiteral("trakcrate"), err);
}

void TrakcratePanel::copySettingsDialog()
{
    if (!lib_.isOpen()) return;
    CopySettingsDialog dlg(lib_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString err;
    if (!copySettingsSet(dlg.setName(), dlg.driveFolder(), &err)) QMessageBox::warning(this, QStringLiteral("trakcrate"), err);
}

TrakcrateSettingsViewer* TrakcratePanel::showSettingsViewer(const QString& set, const QString& file, const QString& compareWith)
{
    if (!lib_.isOpen() || !trakcrate::hasSettingsSet(lib_, set.toStdString())) return nullptr;
    auto* v = new TrakcrateSettingsViewer(lib_, this);
    v->setAttribute(Qt::WA_DeleteOnClose);
    v->setSet(set);
    if (!file.isEmpty()) v->setFile(file);
    v->setCompareSet(compareWith);
    connect(v, &TrakcrateSettingsViewer::userNameChanged, this, [this] { refresh(); });
    v->show();
    return v;
}

void TrakcratePanel::onTreeDoubleClick(QTreeWidgetItem* item)
{
    if (!item) return;
    const QString kind = item->data(0, KindRole).toString();
    if (kind == QLatin1String("settings-hint")) newSettingsDialog();
    else if (kind == QLatin1String("settings-set")) showSettingsViewer(item->data(0, NameRole).toString());
    else if (kind == QLatin1String("setting")) showSettingsViewer(item->data(0, DriveRole).toString(), item->text(0));
}

void TrakcratePanel::treeContextMenu(const QPoint& pos)
{
    if (!lib_.isOpen()) return;
    QTreeWidgetItem* it = tree_->itemAt(pos);
    const QString kind = it ? it->data(0, KindRole).toString() : QString();
    const QString name = it ? it->data(0, NameRole).toString() : QString();
    QMenu menu(this);
    const bool idle = !jobs_->busy();
    QAction* newDrive = menu.addAction(QStringLiteral("New pendrive..."));
    QAction *newSettings = nullptr, *copySettings = nullptr, *defSettings = nullptr, *renSettings = nullptr, *dupSettings = nullptr, *delSettings = nullptr, *userSettings = nullptr, *viewSettings = nullptr, *cmpSettings = nullptr;
    if (kind == QLatin1String("settings") || kind == QLatin1String("settings-hint") || kind == QLatin1String("settings-set") || kind == QLatin1String("setting")) {
        menu.addSeparator();
        newSettings = menu.addAction(QStringLiteral("New settings..."));
        copySettings = menu.addAction(QStringLiteral("Copy settings from a rekordbox pendrive..."));
        const QString set = kind == QLatin1String("settings-set") ? name : kind == QLatin1String("setting") ? it->data(0, DriveRole).toString() : QString();
        if (!set.isEmpty()) {
            menu.addSeparator();
            viewSettings = menu.addAction(QStringLiteral("View..."));
            cmpSettings = menu.addAction(QStringLiteral("Compare with another..."));
            cmpSettings->setEnabled(trakcrate::listSettingsSets(lib_).size() > 1);
            defSettings = menu.addAction(QStringLiteral("Make default"));
            defSettings->setEnabled(trakcrate::defaultSettingsSet(lib_) != set.toStdString());
            userSettings = menu.addAction(QStringLiteral("Change the name on the players..."));
            renSettings = menu.addAction(QStringLiteral("Rename..."));
            dupSettings = menu.addAction(QStringLiteral("Duplicate..."));
            delSettings = menu.addAction(QStringLiteral("Delete..."));
        }
    }
    QMenu* addMenu = nullptr;
    QAction *delFolder = nullptr, *delList = nullptr, *physical = nullptr, *folderOut = nullptr, *delDrive = nullptr, *renDrive = nullptr, *up = nullptr, *down = nullptr, *removeEntry = nullptr;
    std::map<QAction*, QString> targets, driveSettings;
    if (kind == QLatin1String("folder") || kind == QLatin1String("playlist")) {
        addMenu = menu.addMenu(QStringLiteral("Add to pendrive"));
        for (const std::string& d : lib_.listDrives()) targets[addMenu->addAction(QString::fromStdString(d))] = QString::fromStdString(d);
        if (addMenu->isEmpty()) addMenu->addAction(QStringLiteral("(no pendrives yet)"))->setEnabled(false);
        menu.addSeparator();
        if (kind == QLatin1String("folder")) { delFolder = menu.addAction(QStringLiteral("Delete folder...")); delFolder->setEnabled(idle); }
        else { delList = menu.addAction(QStringLiteral("Delete playlist...")); delList->setEnabled(idle); }
    } else if (kind == QLatin1String("drive")) {
        menu.addSeparator();
        physical = menu.addAction(QStringLiteral("Write to a physical device..."));
        folderOut = menu.addAction(QStringLiteral("Write as a rekordbox folder..."));
        physical->setEnabled(idle);
        folderOut->setEnabled(idle);
        menu.addSeparator();
        {
            QMenu* sm = menu.addMenu(QStringLiteral("Settings"));
            const std::string linked = trakcrate::driveSettingsSet(lib_, name.toStdString());
            QAction* follow = sm->addAction(QStringLiteral("Follow the default settings"));
            follow->setCheckable(true);
            follow->setChecked(linked.empty());
            driveSettings[follow] = QString();
            for (const trakcrate::SettingsSet& x : trakcrate::listSettingsSets(lib_)) {
                QAction* a = sm->addAction(QString::fromStdString(x.name));
                a->setCheckable(true);
                a->setChecked(x.name == linked);
                driveSettings[a] = QString::fromStdString(x.name);
            }
        }
        renDrive = menu.addAction(QStringLiteral("Rename pendrive..."));
        delDrive = menu.addAction(QStringLiteral("Delete pendrive..."));
    } else if (kind == QLatin1String("entry")) {
        menu.addSeparator();
        up = menu.addAction(QStringLiteral("Move up"));
        down = menu.addAction(QStringLiteral("Move down"));
        removeEntry = menu.addAction(QStringLiteral("Remove from pendrive"));
    }
    QAction* chosen = menu.exec(tree_->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    QString err;
    if (chosen == newDrive) newPendriveDialog();
    else if (driveSettings.count(chosen)) {
        if (!setPendriveSettings(name, driveSettings[chosen], &err)) QMessageBox::warning(this, QStringLiteral("trakcrate"), err);
    } else if (chosen == newSettings) newSettingsDialog();
    else if (chosen == copySettings) copySettingsDialog();
    else if (chosen && (chosen == viewSettings || chosen == cmpSettings)) {
        const QString set = kind == QLatin1String("settings-set") ? name : it->data(0, DriveRole).toString();
        QString other;
        if (chosen == cmpSettings) {
            QStringList others;
            for (const trakcrate::SettingsSet& x : trakcrate::listSettingsSets(lib_))
                if (QString::fromStdString(x.name) != set) others << QString::fromStdString(x.name);
            bool got = false;
            other = QInputDialog::getItem(this, QStringLiteral("Compare settings"), QStringLiteral("Compare \"%1\" with:").arg(set), others, 0, false, &got);
            if (!got) return;
        }
        showSettingsViewer(set, kind == QLatin1String("setting") ? it->text(0) : QString(), other);
    }
    else if (chosen && (chosen == defSettings || chosen == renSettings || chosen == dupSettings || chosen == delSettings || chosen == userSettings)) {
        const QString set = kind == QLatin1String("settings-set") ? name : it->data(0, DriveRole).toString();
        QString e;
        bool ok = true;
        if (chosen == defSettings) ok = makeDefaultSettingsSet(set, &e);
        else if (chosen == renSettings || chosen == dupSettings) {
            bool got = false;
            const bool dup = chosen == dupSettings;
            const QString to = QInputDialog::getText(this, dup ? QStringLiteral("Duplicate settings") : QStringLiteral("Rename settings"), QStringLiteral("Name:"),
                                                     QLineEdit::Normal, dup ? QString::fromStdString(trakcrate::uniqueSettingsSetName(lib_, set.toStdString())) : set, &got).trimmed();
            if (got && to != set) ok = dup ? duplicateSettingsSet(set, to, &e) : renameSettingsSet(set, to, &e);
        } else if (chosen == userSettings) {
            trakcrate::DjProfile prof;
            std::string bytes;
            trakcrate::readFile(trakcrate::settingsSetDir(lib_, set.toStdString()) / "djprofile.nxs", &bytes);
            if (bytes.empty()) bytes = trakcrate::defaultDjprofile();
            trakcrate::parseDjprofile(bytes, &prof);
            bool got = false;
            const QString to = QInputDialog::getText(this, QStringLiteral("Name on the players"), QStringLiteral("The name the players show for this pendrive:"),
                                                     QLineEdit::Normal, QString::fromStdString(prof.userName), &got).trimmed();
            if (got) ok = setSettingsUserName(set, to, &e);
        } else if (QMessageBox::question(this, QStringLiteral("Delete settings"), QStringLiteral("Delete the settings \"%1\"?").arg(set)) == QMessageBox::Yes) {
            ok = deleteSettingsSet(set, &e);
        }
        if (!ok) QMessageBox::warning(this, QStringLiteral("trakcrate"), e);
    } else if (targets.count(chosen)) {
        if (!addToPendrive(targets[chosen], kind, name, &err)) QMessageBox::warning(this, QStringLiteral("trakcrate"), err);
    } else if (chosen == delFolder) deleteFolderDialog(name);
    else if (chosen == delList) {
        if (QMessageBox::question(this, QStringLiteral("Delete playlist"), QStringLiteral("Delete the playlist \"%1\"? The tracks stay in the library.").arg(name)) == QMessageBox::Yes) {
            std::string e;
            lib_.deletePlaylist(name.toStdString(), &e);
            refresh();
        }
    } else if (chosen == physical) exportPendrive(name, true);
    else if (chosen == folderOut) exportPendrive(name, false);
    else if (chosen == renDrive) {
        bool ok = false;
        const QString to = QInputDialog::getText(this, QStringLiteral("Rename pendrive"), QStringLiteral("New name:"), QLineEdit::Normal, name, &ok).trimmed();
        if (ok && to != name) {
            std::string e;
            if (!lib_.renameDrive(name.toStdString(), to.toStdString(), &e)) QMessageBox::warning(this, QStringLiteral("trakcrate"), QString::fromStdString(e));
            else {
                tree_->setCurrentItem(nullptr);
                refresh();
            }
        }
    }
    else if (chosen == delDrive) {
        if (QMessageBox::question(this, QStringLiteral("Delete pendrive"), QStringLiteral("Delete the pendrive \"%1\"? The tracks, folders and playlists stay in the library.").arg(name)) == QMessageBox::Yes) {
            std::string e;
            lib_.deleteDrive(name.toStdString(), &e);
            refresh();
        }
    } else if (chosen == up || chosen == down || chosen == removeEntry) {
        const std::string drive = it->data(0, DriveRole).toString().toStdString();
        const int idx = it->data(0, IndexRole).toInt();
        std::string e;
        if (chosen == removeEntry) lib_.removeDriveEntry(drive, idx, &e);
        else lib_.moveDriveEntry(drive, idx, idx + (chosen == up ? -1 : 1), &e);
        refresh();
    }
}

// ---- keeping up with the disk -----------------------------------------------------------------------------------------------------

void TrakcratePanel::updateWatches()
{
    if (!watcher_->directories().isEmpty()) watcher_->removePaths(watcher_->directories());
    if (!lib_.isOpen()) return;
    QStringList dirs;
    dirs << QString::fromStdString(lib_.masterDir().string()) << QString::fromStdString(lib_.playlistsDir().string());
    for (const std::string& f : lib_.listFolders()) dirs << QString::fromStdString((lib_.masterDir() / f).string());
    for (const std::string& p : lib_.listPlaylists()) dirs << QString::fromStdString((lib_.playlistsDir() / p / "symlinks").string());
    watcher_->addPaths(dirs);
}

void TrakcratePanel::fileSystemChanged()
{
    if (!watchSuspended_) watchTimer_->start();          // debounced: one refresh for a burst of changes
}

void TrakcratePanel::refresh()
{
    if (!lib_.isOpen()) return;
    rebuildTree();
    onTreeSelection();
    updateWatches();
}

} // namespace vdj
