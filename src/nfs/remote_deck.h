#ifndef REMOTE_DECK_H
#define REMOTE_DECK_H

#include <QSet>
#include <QString>
#include <memory>
#include "xdj_nfs.h"

namespace vdj {

// A connected player whose rekordbox export is read over NFS and mirrored, file by file as needed, into a local
// cache folder that has the same layout as a rekordbox USB stick (so the normal track browser can open it).
class RemoteDeck {
public:
    RemoteDeck(int playerId, const QString& ip);

    // Connect, choose an export (exportIndex < 0 and several exports: the caller must have chosen, see exports()),
    // and fetch PIONEER/rekordbox/export.pdb. Shows a progress dialog while fetching.
    bool connect(QString* error);
    QStringList exports() const { return nfs_.exports(); }
    bool open(int exportIndex, QString* error);
    // Forget everything mirrored so far except the audio: the database and the analysis files (grids, cues, waveforms) are
    // fetched again, because the deck may have changed them since (a file keeps its size when a cue moves, so the size check
    // of ensure() cannot see it).
    bool refresh(QString* error);

    QString root() const { return root_; }
    int playerId() const { return playerId_; }
    QString ip() const { return ip_; }
    QString lastError() const { return lastError_; }

    // Make the file (path as in the database, e.g. "/Contents/Artist/x.mp3") available below root().
    // Skips the transfer when the local copy has the size the player reports. Shows progress for big files.
    bool ensure(const QString& relPath);
    // The .DAT / .EXT / .2EX files of a track (failures of the optional ones are ignored).
    bool ensureAnalysis(const QString& analyzePath);

    static QString cacheRoot();

private:
    bool fetch(const QString& relPath, bool showProgress);

    int playerId_;
    QString ip_;
    QString root_;
    QString lastError_;
    XdjNfs nfs_;
    QSet<QString> verified_;
};

} // namespace vdj

#endif // REMOTE_DECK_H
