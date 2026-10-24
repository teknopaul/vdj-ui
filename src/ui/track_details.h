#ifndef TRACK_DETAILS_H
#define TRACK_DETAILS_H

#include <QWidget>
#include <QList>
#include <QPair>
#include <QString>
#include <functional>
#include "track_analysis.h"

class QLabel;
class QFormLayout;

namespace vdj { class WaveformWidget; class WaveformWindow; }

namespace vdj {

// Everything we know about one track, ready to display.
struct TrackInfo {
    QString title;
    QString artworkPath;                          // absolute path of an image, empty if none
    QString root;                                 // folder holding PIONEER (for the analysis files)
    QString audioPath;                            // absolute path of the audio file, empty if unknown
    QString analyzePath;                          // the database's path of the track's ANLZ .DAT, empty if none
    // Set for tracks on a deck read over NFS: makes a file (database path) available below root, fetching it from
    // the deck when needed. Empty for local tracks.
    std::function<bool(const QString&)> ensureFile;
    std::function<bool(const QString&)> ensureAnalysis;   // the .DAT/.EXT of the given .DAT path
    QList<QPair<QString, QString>> fields;        // label, value (empty values are skipped)
};

class TrackDetails : public QWidget {
    Q_OBJECT

public:
    explicit TrackDetails(QWidget* parent = nullptr);

    void setTrack(const TrackInfo& info);
    void clear();

    const TrackInfo& info() const { return info_; }
    bool hasArtwork() const { return hasArtwork_; }
    // Beat grid, cues and waveforms read for the current track (valid == false when it has no analysis).
    const TrackAnalysis& analysis() const { return analysis_; }
    WaveformWidget* waveformWidget() const { return waveform_; }
    // The separate zoomable waveform window (created the first time it is opened, nullptr before).
    WaveformWindow* waveformWindow() const { return window_; }
    void openWaveformWindow();

private:
    TrackInfo info_;
    TrackAnalysis analysis_;
    WaveformWidget* waveform_;
    WaveformWindow* window_ = nullptr;
    bool hasArtwork_ = false;
    QLabel* artwork_;
    QLabel* title_;
    QWidget* form_;
    QFormLayout* formLayout_;
};

} // namespace vdj

#endif // TRACK_DETAILS_H
