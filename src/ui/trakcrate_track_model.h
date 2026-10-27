#ifndef TRAKCRATE_TRACK_MODEL_H
#define TRAKCRATE_TRACK_MODEL_H

#include <QAbstractTableModel>
#include <QPixmap>
#include <QString>

#include <vector>

#include "trakcrate/library.h"

namespace vdj {

// The tracks of one master folder or one playlist. Track files are read the first time a row is displayed, not when the list is built,
// so a library of thousands of tracks opens at once.
class TrakcrateTrackModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { Title, Artist, Album, Bpm, Time, Wave, Origin, Flags, ColumnCount };
    enum { SortRole = Qt::UserRole + 1 };

    explicit TrakcrateTrackModel(QObject* parent = nullptr);

    void clear();
    void showFolder(const trakcrate::Library& lib, const QString& folder);
    void showPlaylist(const trakcrate::Library& lib, const QString& name);

    QString trackDir(int row) const;                               // empty for a bad row
    bool track(int row, trakcrate::Track* out) const;              // reads the files of that row

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    struct Row {
        trakcrate::fs::path dir;
        mutable bool loaded = false;
        mutable trakcrate::Track track;
        mutable bool hasAudio = false;
        mutable QString bpm, time, origin, flags;
        mutable double bpmValue = 0, seconds = 0;
        mutable QPixmap wave;                 // mini waveform, null until the track has been analysed
    };
    void load(const Row& r) const;
    void setRows(const std::vector<trakcrate::fs::path>& dirs);

    std::vector<Row> rows_;
};

} // namespace vdj

#endif
