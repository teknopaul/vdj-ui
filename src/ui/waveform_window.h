#ifndef WAVEFORM_WINDOW_H
#define WAVEFORM_WINDOW_H

#include <QWidget>
#include "track_analysis.h"

class QLabel;
class QScrollBar;

namespace vdj {

class WaveformWidget;

// A separate, resizable window with the track's waveform, beat grid and cue points, to zoom and scroll through
// the fine detail of the grid: mouse wheel zooms around the pointer, dragging pans, double click or "Fit" shows
// the whole track, the scroll bar scrolls. The label under the waveform tells the time and nearest beat under
// the pointer.
class WaveformWindow : public QWidget {
    Q_OBJECT

public:
    explicit WaveformWindow(QWidget* parent = nullptr);

    void setTrack(const QString& title, const TrackAnalysis& analysis);

    WaveformWidget* waveform() const { return waveform_; }
    QScrollBar* scrollBar() const { return scroll_; }
    QLabel* positionLabel() const { return position_; }
    QLabel* viewLabel() const { return view_; }

private:
    void syncControls();

    WaveformWidget* waveform_;
    QScrollBar* scroll_;
    QLabel* position_;
    QLabel* view_;
    bool syncing_ = false;
};

} // namespace vdj

#endif // WAVEFORM_WINDOW_H
