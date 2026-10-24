#ifndef PLAYLIST_EXPORT_H
#define PLAYLIST_EXPORT_H

#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

namespace vdj {

struct ExportTrack {
    QString sourcePath;   // absolute path of the audio file
    QString title;
    QString artist;
};

struct ExportResult {
    bool ok = false;
    QString error;
    int copied = 0;              // files copied (a track repeated in the playlist is copied once)
    QStringList missing;         // source files that do not exist
    QStringList failed;          // source files that could not be copied
    QStringList files;           // file names in playlist order, as written to the list file
    QString listFile;            // absolute path of the .txt list
};

// Copy the tracks of a playlist into an empty directory and write "<playlist name>.txt" beside
// them listing the copied file names, one per line, in playlist order. Files keep their original
// names; a name clash gets " (2)", " (3)" ... before the extension. Tracks whose file is missing
// are reported but not listed. progress(done, total) may return false to cancel.
ExportResult exportPlaylist(const QString& dir, const QString& playlistName,
                            const QList<ExportTrack>& tracks,
                            const std::function<bool(int, int)>& progress = {});

} // namespace vdj

#endif // PLAYLIST_EXPORT_H
