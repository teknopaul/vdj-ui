#ifndef TRACK_BROWSER_H
#define TRACK_BROWSER_H

#include <QWidget>
#include <QList>
#include "track_details.h"
#include "export/playlist_export.h"
#include <memory>

class QTreeWidget;
class QTreeWidgetItem;

namespace vdj { class RemoteDeck; }

namespace vdj { class MiniWaveformDelegate; }

namespace vdj {

class TrackBrowser : public QWidget {
    Q_OBJECT

public:
    explicit TrackBrowser(QWidget* parent = nullptr);

    void setMount(const QString& mountPoint);
    void setPlayerId(int playerId);
    void refresh();

    // Load a rekordbox export folder (must contain ./Contents; ./PIONEER/rekordbox/export.pdb
    // is used when present). Returns false and sets *error if the folder is not usable.
    bool openRekordboxFolder(const QString& folder, QString* error = nullptr);
    // Read the rekordbox database of a connected deck over NFS (the export is mirrored into a local cache as files
    // are needed). Asks which export when the deck has several. Returns false and sets *error on failure; a user
    // cancel returns false with an empty error.
    bool openRemoteDeck(int playerId, const QString& ip, QString* error = nullptr);
    // Fetch the database and the analysis files of the shown deck again and rebuild the tree (remote only).
    bool refreshFromDeck(QString* error = nullptr);
    bool isRemote() const { return remote_ != nullptr; }
    int remotePlayerId() const { return remote_ ? playerId_ : 0; }         // the deck whose database is shown, 0 for a local one
    // Tracks (in playlist order) of a playlist item, empty for any other item.
    QList<ExportTrack> playlistTracks(const QTreeWidgetItem* item) const;
    bool hasPdb() const { return hasPdb_; }
    // The folder of the last local rekordbox database that was loaded (the one with PIONEER/rekordbox/export.pdb), empty if none yet.
    // It stays when the view switches to a deck's database. The LINK export serves this folder.
    QString exportRoot() const { return exportRoot_; }

    QTreeWidget* treeWidget() const { return tree_; }
    TrackDetails* detailsWidget() const { return details_; }
    MiniWaveformDelegate* miniWaveformDelegate() const { return miniDelegate_; }
    static constexpr int kWaveformColumn = 3;

    enum class CopyBack { BeatGrid, CuePoints };

signals:
    // Right click "Copy back beatgrid" / "Copy back cue points" on a playlist or on track(s): the deck's edits of these
    // tracks should go into the trakcrate library. The browser knows nothing about trakcrate; the main window handles it.
    void copyBackRequested(TrackBrowser::CopyBack what, const QList<TrackInfo>& tracks);
    void trackSelected(int playerId, const QString& title, const QString& artist,
                       const QString& bpm, const QString& key);
    // Double click on a track: load it in the preview player.
    void previewRequested(const TrackInfo& info);
    void databaseLoaded(const QString& exportRoot);       // a local rekordbox folder with a database was loaded

private:
    void showContextMenu(const QPoint& pos);
    void addCopyBackActions(QMenu& menu, const QList<TrackInfo>& tracks);
    void exportPlaylistInteractive(const QString& name, const QList<ExportTrack>& tracks);
    void populateItem(QTreeWidgetItem* parent, const QString& path, int depth);

    bool loadPdb(const QString& pdbPath, const QString& root, QString* error);
    void populateContents(QTreeWidgetItem* parent, const QString& path, int depth);

    bool openFolder(const QString& folder, QString* error);
    std::shared_ptr<RemoteDeck> remote_;   // set while the shown database is a deck's
    bool hasPdb_ = false;
    QString exportRoot_;
    int playerId_ = 0;
    QString mountPoint_;
    QTreeWidget* tree_;
    TrackDetails* details_;
    MiniWaveformDelegate* miniDelegate_;
    struct PlaylistData { QString name; QList<ExportTrack> tracks; QList<int> dbIdx; };   // dbIdx: indexes into dbTracks_
    QList<PlaylistData> playlists_;   // indexed by the item's Qt::UserRole + 3
    QList<TrackInfo> dbTracks_;   // indexed by the item's Qt::UserRole + 2
};

} // namespace vdj

#endif // TRACK_BROWSER_H