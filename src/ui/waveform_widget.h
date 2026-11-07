#ifndef WAVEFORM_WIDGET_H
#define WAVEFORM_WIDGET_H

#include <QImage>
#include <QList>
#include <QString>
#include <QWidget>
#include "track_analysis.h"

namespace vdj {

// A trakcrate cuemark as the widget shows it (doc/trakcrate/cuemarks.md): drawn as a green line; hot is 0 or 'A'..'C'.
struct WaveCuemark {
    double ms = 0;
    QString label;
    char hot = 0;
};

// The colour waveform of a track (PWV5, else the PWAV preview) with its beat grid and cue points drawn over it.
//
//  - beats: white lines, bars (beat 1): red lines, from the bottom edge up to the middle only (the waveform is
//    drawn mirrored about the middle). Lines that would be closer than 3 px are left out.
//  - cues: a yellow vertical line with a small arrow at the top pointing down; loops get a faint band.
//
// The widget shows a window of the track (viewStart() .. viewStart() + viewSpan()); by default the whole track.
// In interactive mode the mouse wheel zooms around the pointer, dragging pans, double click fits the track and
// the arrow keys / + / - / Home do the same from the keyboard.
class WaveformWidget : public QWidget {
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    void setAnalysis(const TrackAnalysis& analysis);
    void clear();

    bool hasWaveform() const { return !analysis_.color.isEmpty() || !analysis_.preview.isEmpty(); }
    const TrackAnalysis& analysis() const { return analysis_; }

    // Playhead (a green line at the play position, ms; negative hides it). With setSeekable a click in the
    // non-interactive view emits seekRequested(ms) for the time under the pointer.
    void setPlayPosition(double ms);
    double playPosition() const { return playMs_; }
    void setSeekable(bool on);
    bool isSeekable() const { return seekable_; }

    void setInteractive(bool on);
    bool isInteractive() const { return interactive_; }

    // View window in ms. setView clamps to the track; the span never goes below kMinSpanMs.
    void setView(double startMs, double spanMs);
    void fit();                                  // the whole track
    void zoomAt(int x, double factor);           // factor > 1 zooms in, around the time under pixel x
    void panBy(double ms);
    double viewStart() const { return viewStart_; }
    double viewSpan() const { return viewSpan_; }
    double zoom() const;                         // 1 = whole track

    int cueX(quint32 timeMs) const;              // x in this widget of a time in the track (the current view)
    int timeX(double timeMs) const;
    double timeAt(int x) const;
    QString describePosition(int x) const;       // the time under x and the nearest beat

    // Cuemark editing (an interactive widget only). The widget never changes the marks itself: it shows the list it was given and reports what the
    // user asked for; the owner applies the change and calls setCuemarks again.
    //   double click / key M    cuemarkAdded(ms)      snapped to the grid unless Alt is held (M uses the play position, else the pointer)
    //   drag a mark             cuemarkMoved(i, ms)   on release
    //   Delete / context menu   cuemarkRemoved(i)
    //   context menu            cuemarkHotChanged(i, 'A'..'C' or 0)
    enum class Snap { None, Beat, Bar };
    void setCuemarks(const QList<WaveCuemark>& marks);
    const QList<WaveCuemark>& cuemarks() const { return cuemarks_; }
    void setCuemarkEditing(bool on);
    bool cuemarkEditing() const { return editing_; }
    void setSnap(Snap s) { snap_ = s; update(); }
    Snap snap() const { return snap_; }
    double snapped(double ms) const;             // ms moved to the nearest beat or bar of the grid in the current mode
    int cuemarkAt(int x) const;                  // index of the mark within kHitPx of x, or -1
    int selectedCuemark() const { return selected_; }
    void setSelectedCuemark(int i);

    // Beat grid editing: dragging moves the beat lines (they follow the pointer, the result is reported on release), Left/Right nudge by 1 ms
    // (Shift 10 ms, Ctrl 0.1 ms), B or the context menu says "beat 1 here". Cuemark editing and grid editing are exclusive; grid editing wins.
    void setGridEditing(bool on);
    bool gridEditing() const { return gridEditing_; }
    static constexpr double kGridStepMs = 1.0;

    static const QColor kCuemarkColor;           // green
    static constexpr int kHitPx = 5;

    static const QColor kCueColor;               // yellow
    static const QColor kBeatColor;              // white
    static const QColor kBarColor;               // red
    static constexpr double kMinSpanMs = 20.0;
    static constexpr int kMinLineSpacing = 3;    // px: beat/bar lines closer than this are not drawn

    QSize sizeHint() const override { return QSize(300, 80); }
    QSize minimumSizeHint() const override { return QSize(100, 60); }

signals:
    void viewChanged();
    void hovered(const QString& text);
    void seekRequested(double ms);
    void activated();                            // double click in the non-interactive (embedded) view
    void cuemarkAdded(double ms);
    void cuemarkMoved(int index, double ms);
    void cuemarkRemoved(int index);
    void cuemarkHotChanged(int index, int hot);  // hot: 0, or 'A'..'C'
    void cuemarkSelected(int index);
    void gridNudged(double deltaMs);             // the grid was dragged or nudged by this much (negative = earlier)
    void gridBeatOneRequested(double ms);        // the nearest beat should land on ms

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    double lengthMs() const { return analysis_.lengthMs(); }
    void paintWaveform(QPainter& p, double t0, double span);
    void paintWaveformSharp(QPainter& p, double t0, double span);   // one-pixel columns, no blur
    void paintBeats(QPainter& p, double t0, double span);
    void paintCues(QPainter& p);
    void paintCuemarks(QPainter& p);
    void paintPlayhead(QPainter& p);
    int nearestBeat(double ms) const;

    TrackAnalysis analysis_;
    QImage waveCache_;                       // blurred waveform layer, rebuilt when the view changes
    double waveT0_ = -1, waveSpan_ = -1;
    bool interactive_ = false;
    double viewStart_ = 0.0;
    double viewSpan_ = 0.0;                      // 0 = not set yet: the whole track
    double playMs_ = -1.0;
    bool seekable_ = false;
    bool dragging_ = false;
    int dragX_ = 0;
    double dragStart_ = 0.0;

    QList<WaveCuemark> cuemarks_;
    bool editing_ = false;
    Snap snap_ = Snap::Beat;
    int selected_ = -1;
    int markDrag_ = -1;                          // the mark being dragged
    double markDragMs_ = 0.0;
    int hoverX_ = -1;

    bool gridEditing_ = false;
    bool gridDrag_ = false;
    int gridDragX_ = 0;
    double gridDragMs_ = 0.0;                    // how far the grid is dragged right now
};

} // namespace vdj

#endif // WAVEFORM_WIDGET_H
