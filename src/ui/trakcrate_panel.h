#ifndef TRAKCRATE_PANEL_H
#define TRAKCRATE_PANEL_H

#include <QElapsedTimer>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

#include "trakcrate/analysis.h"
#include "trakcrate/bpm_detect.h"
#include "trakcrate/exporter.h"
#include "trakcrate/importer.h"
#include "trakcrate/rb_import.h"
#include "trakcrate/library.h"

class QFileSystemWatcher;
class QLabel;
class QModelIndex;
class QProgressBar;
class QPushButton;
class QSortFilterProxyModel;
class QTimer;
class QTreeView;
class TrakcrateTableView;
class QTreeWidget;
class QTreeWidgetItem;

namespace vdj {

class ConfigManager;
class ScriptManager;
class TrakcrateTableView;
class TrakcrateTreeWidget;
class TrakcratePreviewPlayer;
class TrakcrateJobRunner;
class TrakcrateTrackModel;

// The trakcrate pane (doc/trakcrate/gui.md): folders and playlists on the left, the tracks of the selection in the middle, the selected
// track's details on the right. Importing runs as a background job; the pane stays usable and the tables update as files appear.
class TrakcratePanel : public QWidget {
    Q_OBJECT

public:
    explicit TrakcratePanel(ConfigManager* config = nullptr, QWidget* parent = nullptr);
    ~TrakcratePanel() override;

    bool openLibrary(const QString& path, QString* err = nullptr);
    bool createLibrary(const QString& path, QString* err = nullptr);     // creates the layout (if needed) and opens it
    // The mini player under the track list (playlists, pendrives and their lists). blocker returns why playing is not allowed (for example that the
    // app is on the Pro DJ Link network), or an empty string.
    TrakcratePreviewPlayer* preview() const { return preview_; }
    void setPlaybackBlocker(std::function<QString()> blocker);
    void stopPreview();
    // Moves rows of the shown playlist (or pendrive list) as one block so that they sit before row `insertBefore` (rows.size() = the end); the rows are
    // table rows. This is what a drag and drop and the Move menu items do. The change is written at once. False (with err) when the view is not a
    // playlist or pendrive list, or the move failed.
    bool moveRows(const std::vector<int>& rows, int insertBefore, QString* err = nullptr);
    // Moves a virtual pendrive's list to sit before list `insertBefore` of that pendrive (what a drag in the tree does).
    // Moves the selected rows of the shown playlist: -2 to the start, -1 up one, +1 down one, +2 to the end (the right click items and Ctrl+Up / Down).
    bool moveSelected(int how, QString* err = nullptr);
    // Ctrl+Up / Down on the chosen pendrive list: one place up (-1) or down (+1) among the lists of its pendrive.
    bool moveChosenEntry(int direction, QString* err = nullptr);
    bool moveEntry(const QString& drive, int from, int insertBefore, QString* err = nullptr);
    bool startPreview(int proxyRow = -1, QString* err = nullptr);     // plays the shown playlist / entry, or all entries of the chosen pendrive
    ScriptManager* scripts() const { return scripts_; }          // the library's scripts (doc/trakcrate/scripts.md)
    bool hasLibrary() const { return lib_.isOpen(); }
    const trakcrate::Library& library() const { return lib_; }

    // Starts the import as a background job. False when a job is already running or there is no library.
    bool importFolder(const QString& folder, const trakcrate::ImportOptions& opts);

    // Analyses track folders (beat grid, waveform, loudness) as a background job; force also redoes rekordbox/edited fields.
    bool analyseTracks(const QStringList& trackDirs, bool force = false);
    QStringList selectedTrackDirs() const;

    // Imports a real rekordbox pendrive as a background job (metadata, beat grids, cues, waveforms, artwork, playlists; the drive is only read).
    bool importRekordbox(const QString& driveRoot, const trakcrate::RbImportOptions& opts);

    // ---- user defined pendrives (Library::driveEntries): folders and playlists put on a pendrive, reordered, removed, written out ----
    bool createPendrive(const QString& name, const QStringList& folders, const QStringList& playlists, QString* err = nullptr);
    bool addToPendrive(const QString& drive, const QString& kind, const QString& name, QString* err = nullptr);   // kind: "folder" or "playlist"
    // What deleting a folder would break (playlists and pendrives that link to its tracks).
    trakcrate::Library::Impact folderImpact(const QString& folder) const;
    // Removes the links to the folder's tracks everywhere, then moves the folder to the trash. Does not ask: deleteFolderDialog() warns first.
    bool deleteFolder(const QString& folder, QString* err = nullptr);

    // Detects the BPM of one track with libaubio as a background job, then asks the user (keep, half, double, type, reject). Accepting
    // writes the BPM and creates the beat grid when the track has none.
    bool detectBpm(const QString& trackDir);
    // Stores a chosen BPM: creates the beat grid when it is missing (or replaceGrid), phase from firstBeatUs. False on a write error.
    bool applyBpm(const QString& trackDir, double bpm, long long firstBeatUs, bool replaceGrid, QString* err = nullptr, bool* gridWritten = nullptr);
    // Replaces the BPM dialog (tests): returns false to reject, else sets bpm and replaceGrid.
    using BpmPrompt = std::function<bool(const QString& dir, const trakcrate::BpmDetection& found, double* bpm, bool* replaceGrid)>;
    void setBpmPrompt(BpmPrompt p) { bpmPrompt_ = std::move(p); }

    // Exports playlists as a rekordbox drive as a background job (staged on tmpfs, checked, then written once). The template database and
    // the artwork encoder are filled in here. When registerName is not empty the destination is offered as a virtual pendrive on success.
    bool exportDrive(trakcrate::ExportOptions opts, const QString& registerName = QString());

    void refresh();                                                       // re-read the folder and playlist lists and the table
    void selectFolder(const QString& name);
    void selectPlaylist(const QString& name);

    TrakcrateTrackModel* trackModel() const { return model_; }
    int visibleTrackCount() const;
    bool busy() const;
    // Waits for the running job (tests).
    bool waitForJob(int timeoutMs = 10000);

signals:
    void status(const QString& message);
    void libraryChanged(const QString& path);
    void importFinished(const QString& summary, bool ok);
    void analysisFinished(const QString& summary, bool ok);
    void bpmFinished(const QString& summary, bool ok);
    void loadRequested(const QString& trackDir);          // the user asked to load this track into the player
    void rekordboxImportFinished(const QString& summary, bool ok);
    void exportFinished(const QString& summary, bool ok, const QString& dest);
    void registerPendriveRequested(const QString& name, const QString& folder);

private slots:
    void chooseLibrary();
    void newLibrary();
    void importDialog();
    void onTreeSelection();
    void onTrackSelection();
    void onJobProgress(int done, int total, const QString& current);
    void onJobFinished(const QString& title, bool cancelled);
    void fileSystemChanged();
    void tableContextMenu(const QPoint& pos);
    void exportDialog();
    void newPendriveDialog();
    void treeContextMenu(const QPoint& pos);
    void deleteFolderDialog(const QString& folder);
    void exportPendrive(const QString& drive, bool physical);
    void editEntryTracks(int move, bool remove);                 // on the open pendrive entry: move the selected tracks (-1 up, +1 down) or remove them
    void rekordboxImportDialog();

private:
    void rebuildTree(bool selectionSignals = true);
    void liveRefresh();                        // folders, playlists and the open list while an import is still running
    void showDetails(const QModelIndex& proxyIndex);
    void updateWatches();
    void setBusyUi(bool busy);
    void rememberLibrary(const QString& path);

    ConfigManager* config_;
    trakcrate::Library lib_;
    QString shownList_;                                   // which tree item the table last showed (to select its first track once)
    TrakcrateJobRunner* jobs_;
    ScriptManager* scripts_;
    TrakcratePreviewPlayer* preview_;
    struct PreviewItem { QString entryDir; int row; };
    std::vector<PreviewItem> previewMap_;               // a pendrive plays all its entries: which entry and row each track is
    void onPreviewEnd(const QString& owner);
    void onPreviewTrack(const QString& owner, int index, const QString& dir);
    QString currentListKey() const;
    QPushButton* scriptsButton_;
    TrakcrateTrackModel* model_;
    QSortFilterProxyModel* proxy_;
    TrakcrateTreeWidget* tree_;
    TrakcrateTableView* table_;
    QLabel* libraryLabel_;
    QPushButton* importButton_;
    QPushButton* refreshButton_;
    QPushButton* exportButton_;
    QPushButton* rbImportButton_;
    QPushButton* newPendriveButton_;
    QProgressBar* progress_;
    QLabel* progressLabel_;
    QPushButton* cancelButton_;
    QLabel* artwork_;
    QLabel* details_;
    QFileSystemWatcher* watcher_;
    QTimer* watchTimer_;
    std::shared_ptr<trakcrate::ImportReport> report_;
    std::shared_ptr<std::vector<trakcrate::AnalyseResult>> analysis_;
    std::shared_ptr<trakcrate::ExportResult> export_;
    std::shared_ptr<trakcrate::BpmDetection> bpm_;
    std::shared_ptr<trakcrate::RbImportReport> rb_;
    QString bpmDir_;
    BpmPrompt bpmPrompt_;
    QString exportDest_, exportRegisterName_;
    bool watchSuspended_ = false;
    QElapsedTimer liveTimer_;
};

} // namespace vdj

#endif
