#ifndef TRAKCRATE_EDIT_PANEL_H
#define TRAKCRATE_EDIT_PANEL_H

#include <QList>
#include <QString>
#include <QWidget>

#include <vector>

#include "trakcrate/edit_plan.h"
#include "trakcrate/track.h"
#include "waveform_widget.h"

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QUndoStack;

namespace vdj {

// The regions of a DJ edit (doc/trakcrate/track-edits.md) for the loaded track: a range dragged on the waveform becomes a cut (whole bars from a bar line)
// or a loop (1, 2, 4, 8 or 16 bars from any beat); the list and the parameter row below edit the selected region. The regions live here only (they are
// not written to the track) and every change goes through the editor's undo stack. The panel never touches the audio or the waveform: the owner shows
// waveRegions() and feeds the dragged range into setRange().
class TrakcrateEditPanel : public QWidget {
    Q_OBJECT

public:
    TrakcrateEditPanel(QUndoStack* undo, QWidget* parent = nullptr);

    // The grid and length of the loaded track (called again after a grid edit; regions are in beats so they stay on the grid).
    void setTrack(const trakcrate::BeatGrid& grid, double lengthMs);
    void resetRegions();                               // another track was loaded

    const std::vector<trakcrate::EditRegion>& regions() const { return regions_; }
    int selected() const { return selected_; }
    QList<WaveEditRegion> waveRegions() const;         // where each region sits on the waveform (an early cut shows the range it really removes)
    bool hasRange() const { return rangeB_ > rangeA_; }
    // Where a preview starts (ms of the original, from a click on the waveform), -1 when none was set. Cleared when another track is loaded.
    double previewStart() const { return previewStartMs_; }
    void setPreviewStart(double ms);
    // The window "Preview region" plays: 2 bars before the selected region (the range it really removes or loops) to 2 bars after it. False without a selection.
    bool regionPreviewWindow(double* startMs, double* stopMs) const;

    // The same actions as the widgets, for code and tests. Each returns false (and says why through status()) when nothing was done.
    bool setRange(double startMs, double endMs);
    bool addCutFromRange();
    bool addLoopFromRange();
    bool removeRegion(int index);
    bool selectRegion(int index);
    bool changeRegion(int index, const trakcrate::EditRegion& after);     // validated, through the undo stack
    bool setAlgo(trakcrate::EditAlgo algo);            // of the selected region; the parameter becomes a legal default
    bool setParam(double value);

    QString readout() const;                           // "Edit: 5:42 -> 4:51", or why the regions cannot be rendered
    QString problem() const;                           // "" when the regions are valid

    QListWidget* regionList() const { return list_; }
    QPushButton* cutButton() const { return cutButton_; }
    QPushButton* loopButton() const { return loopButton_; }
    QComboBox* algoBox() const { return algoBox_; }
    QComboBox* paramBox() const { return paramBox_; }
    QComboBox* loopBarsBox() const { return barsBox_; }
    QSpinBox* loopTimesSpin() const { return timesSpin_; }
    QComboBox* earlyShiftBox() const { return shiftBox_; }
    QComboBox* earlyDirectionBox() const { return dirBox_; }
    QComboBox* earlyJoinBox() const { return joinBox_; }
    QComboBox* earlyJoinParamBox() const { return joinParamBox_; }
    QPushButton* previewButton() const { return previewButton_; }
    QPushButton* previewRegionButton() const { return previewRegionButton_; }
    QPushButton* renderButton() const { return renderButton_; }
    QPushButton* loopExportButton() const { return loopExportButton_; }
    // The section an acid loop would be cut from: the selected loop region, else the dragged range from its nearest beat (4 beats). False when there is none.
    bool loopSection(int* startBeat, int* lenBeats) const;

signals:
    void regionsChanged();                             // the list or the selection changed: show waveRegions() / selected()
    void previewRequested(double startMs, double stopMs);   // "Preview...": play the edit from startMs of the original (-1: the start) to stopMs (-1: the end)
    void loopExportRequested(int startBeat, int lenBeats);   // "Export loop...": a sample for Acid
    void renderRequested();                            // "Render...": save it
    void rangeConsumed();                              // the dragged range became a region (the waveform stops drawing it)
    void status(const QString& message);

private:
    friend class EditRegionsCommand;
    bool pushRegions(std::vector<trakcrate::EditRegion> after, int selectedAfter, const QString& text, bool mergeable = false);
    void applyRegions(const std::vector<trakcrate::EditRegion>& regions, int selected);   // the single place that changes regions_
    bool addRegion(trakcrate::EditRegion r);
    void refresh();
    void paramsFromWidgets();
    long long totalFrames() const;
    double beatMs(double beat) const;
    double beatOfMs(double ms) const;

    QUndoStack* undo_;
    trakcrate::BeatGrid grid_;
    double lengthMs_ = 0;
    std::vector<trakcrate::EditRegion> regions_;       // sorted by start beat
    int selected_ = -1;
    double rangeA_ = 0, rangeB_ = 0;
    double previewStartMs_ = -1;
    bool updating_ = false;

    QLabel* rangeLabel_;
    QPushButton* cutButton_;
    QPushButton* loopButton_;
    QPushButton* removeButton_;
    QListWidget* list_;
    QWidget* params_;
    QComboBox* algoBox_;
    QComboBox* paramBox_;
    QLabel* barsLabel_;
    QComboBox* barsBox_;
    QLabel* timesLabel_;
    QSpinBox* timesSpin_;
    QWidget* earlyRow_;
    QComboBox* shiftBox_;
    QComboBox* dirBox_;
    QComboBox* joinBox_;
    QComboBox* joinParamBox_;
    QLabel* readout_;
    QPushButton* previewButton_;
    QPushButton* previewRegionButton_;
    QPushButton* renderButton_;
    QPushButton* loopExportButton_;
};

} // namespace vdj

#endif
