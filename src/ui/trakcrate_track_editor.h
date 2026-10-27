#ifndef TRAKCRATE_TRACK_EDITOR_H
#define TRAKCRATE_TRACK_EDITOR_H

#include <QString>
#include <QWidget>

#include "trakcrate/track.h"

#include <functional>

class QComboBox;
class QLabel;
class QListWidget;
class QLineEdit;
class QPushButton;
class QUndoStack;

namespace vdj {

class ArtworkView;
class ConfigManager;
class StarRatingWidget;
class WaveformWidget;

// The per track editor (doc/trakcrate/track-editor.md): the track loaded in the player, with its artwork, facts, a star rating and a comment.
// Edits are written to meta.txt at once (atomically, the meta group is marked "edited" in source.txt) and can be undone. Loading a track never
// writes anything. Without a track the widgets are disabled.
class TrakcrateTrackEditor : public QWidget {
    Q_OBJECT

public:
    explicit TrakcrateTrackEditor(QWidget* parent = nullptr);

    void setConfig(ConfigManager* config);          // remembers the chosen image editor
    ArtworkView* artworkView() const { return art_; }
    void setTrack(const QString& trackDir);        // empty: no track
    void clear() { setTrack(QString()); }
    QString trackDir() const { return dir_; }
    int rating() const;
    QString comment() const;

    // The same actions as the widgets, for code and tests. False when there is no track or the file could not be written.
    bool setRating(int stars);
    bool setComment(const QString& text);

    // Cuemarks (doc/trakcrate/cuemarks.md): edits go through the undo stack and are written to cuemarks.txt at once (the cues group becomes "edited").
    // The snap mode is the one chosen in the toolbar. Each returns false when there is no track, nothing could change, or the file could not be written.
    bool addCuemark(double ms, const QString& label = QString());     // ms is used as given: the waveform snaps before it asks
    bool moveCuemark(int index, double ms);
    bool removeCuemark(int index);
    bool setCuemarkHot(int index, char hot);                           // 'A'..'C', 0 clears
    bool setCuemarkLabel(int index, const QString& label);
    int cuemarkCount() const;
    WaveformWidget* waveform() const { return wave_; }
    QListWidget* cuemarkList() const { return markList_; }
    QPushButton* editMarksButton() const { return editMarks_; }
    QComboBox* snapBox() const { return snapBox_; }

    // Beat grid nudging (doc/trakcrate/track-editor.md). Every change rewrites beatgrid.txt (origin "edited") and the beat columns that depend on it,
    // through the undo stack; consecutive nudges of one track merge into one undo step. A grid that came from rekordbox asks for confirmation the
    // first time it is edited in a session (setGridConfirmer replaces the question, for tests). False when refused or nothing changed.
    bool nudgeGrid(double deltaMs);
    bool setBeatOneAt(double ms);
    bool revertGrid();                                  // the grid the track had when it was loaded into the editor
    double gridOffsetMs() const;                        // how far the grid is from that one now
    void setGridConfirmer(std::function<bool(const QString&)> f) { confirmGrid_ = std::move(f); }
    QPushButton* gridEditButton() const { return gridEdit_; }
    QLabel* gridReadout() const { return gridReadout_; }

    QUndoStack* undoStack() const { return undo_; }
    StarRatingWidget* stars() const { return stars_; }
    QLineEdit* commentEdit() const { return comment_; }

signals:
    void gridChanged(const QString& trackDir);     // beatgrid.txt was rewritten (edit, undo or redo): a deck playing this track should reload it
    void trackChanged(const QString& trackDir);    // meta.txt was rewritten (edit, undo or redo)
    void status(const QString& message);

private slots:
    void commentCommitted();

private:
    friend class MetaEditCommand;
    friend class CuemarkEditCommand;
    friend class GridEditCommand;
    bool applyGrid(const QString& dir, const trakcrate::BeatGrid& grid, const std::string& origin);
    bool pushGrid(const trakcrate::BeatGrid& after, const QString& text, bool merge);
    bool confirmed();
    void showGrid();
    void refreshWave();
    bool applyMarks(const QString& dir, const trakcrate::CuemarkFile& marks);        // the single place that writes cuemarks.txt
    bool pushMarks(const trakcrate::CuemarkFile& after, const QString& text);
    void showMarks();
    bool writeMeta(const QString& dir, const QString& key, const QString& value);     // the single place that touches the disk
    void showFacts();
    void reload();

    QString dir_;
    QUndoStack* undo_;
    QLabel* title_;
    ArtworkView* art_;
    QLabel* facts_;
    StarRatingWidget* stars_;
    QLineEdit* comment_;
    QPushButton* gridEdit_;
    QLabel* gridReadout_;
    trakcrate::BeatGrid sessionGrid_;              // the grid as loaded, for Revert
    std::string sessionOrigin_;
    bool sessionKnown_ = false;
    QString confirmedDir_;                         // a rekordbox grid of this track was confirmed for editing
    std::function<bool(const QString&)> confirmGrid_;
    WaveformWidget* wave_;
    QPushButton* editMarks_;
    QComboBox* snapBox_;
    QListWidget* markList_;
    QPushButton* undoButton_;
    QPushButton* redoButton_;
    QString shownComment_;                         // what the field held when it was last loaded or committed
};

} // namespace vdj

#endif
