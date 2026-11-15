#ifndef MIX_CANVAS_H
#define MIX_CANVAS_H

#include "trakcrate/mix_model.h"

#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QScrollBar;

namespace vdj {

// What the canvas needs to draw one track besides the model.
struct MixLaneInfo {
    QString title;
    QVector<quint16> color;            // the colour waveform of the original track (150 columns per second); empty = not available
};

// The studio mix on a shared time axis: a ruler on top, one lane per track (waveform of the stretched and edited track, beat grid, label), the green play
// position. Wheel: scrolls the lanes, Ctrl+wheel zooms around the pointer, Shift+wheel scrolls in time. Draws only what is visible.
class MixCanvas : public QWidget {
    Q_OBJECT

public:
    static constexpr int kRulerHeight = 24;
    static constexpr int kLaneGap = 4;
    static constexpr double kGrabRadius = 14;                  // px around a curve dot that grabs it
    static constexpr double kMinSpanMs = 20000;

    explicit MixCanvas(QWidget* parent = nullptr);

    // The mix is not owned: call refresh() after changing it. facts and lanes are parallel to mix->tracks.
    void setMix(const trakcrate::Mix* mix, const std::vector<trakcrate::TrackFacts>& facts, const std::vector<MixLaneInfo>& lanes);
    void refresh();
    // The tracks of the mix changed (one removed or put back): new facts and lane infos, parallel to the mix. Clears the selection.
    void setTracks(const std::vector<trakcrate::TrackFacts>& facts, const std::vector<MixLaneInfo>& lanes);
    const std::vector<MixLaneInfo>& laneInfos() const { return info_; }                    // recompute lanes after the model changed
    void setStretching(int lane, bool on);

    int laneCount() const { return int(eff_.size()); }
    QRect laneRect(int lane) const;                            // in widget coordinates (may be outside the visible area)
    double laneStartMs(int lane) const { return lane >= 0 && lane < int(starts_.size()) ? starts_[size_t(lane)] : 0; }
    const trakcrate::EffectiveTrack& effective(int lane) const { return eff_[size_t(lane)]; }
    const QVector<quint16>& editedColumns(int lane) const { return edited_[lane]; }
    double mixLengthMs() const { return lengthMs_; }

    int laneHeight() const { return laneHeight_; }
    void setLaneHeight(int h);

    double viewStartMs() const { return viewStart_; }
    double spanMs() const { return span_; }
    void setView(double startMs, double spanMs);
    void zoomAt(double ms, double factor);                     // factor < 1 zooms in
    void zoomFit();
    // Centres the mix period (the one nearest the play position, else the middle of the view; or a given pair) with 8 bars of each track on both sides.
    void fitPeriod(int pair = -1);
    void fitNextPeriod(int delta);                             // the previous / next period of the one last fitted
    int nearestPeriod(double ms) const;

    double playPosition() const { return playMs_; }
    void setPlayPosition(double ms);
    void ensureVisible(double ms);                             // scrolls (page wise) when ms is outside the view
    int trackAt(int y) const;                                  // lane under a widget y, -1 for the ruler or a gap
    double msAt(int x) const;
    int xAt(double ms) const;
    int selectedLane() const { return selected_; }
    void selectLane(int lane);

    QScrollBar* verticalScrollBar() const { return vbar_; }
    QScrollBar* horizontalScrollBar() const { return hbar_; }

    // ---- editing (phase 7): every change goes through the editor so the owner can make it one undo step ----
    using ChangeFn = std::function<void(trakcrate::Mix&, const std::vector<trakcrate::EffectiveTrack>&)>;
    using EditFn = std::function<void(const QString& text, ChangeFn change, const QString& mergeKey)>;
    void setEditor(EditFn f) { editor_ = std::move(f); }
    enum Curve { VolumeCurve = 0, BassCurve = 1, HighCurve = 2 };
    int curveMode() const { return curveMode_; }
    void setCurveMode(int c);
    struct PointRef { int lane = -1, curve = 0, index = -1; };
    PointRef selectedPoint() const { return selPoint_; }
    void removeSelectedPoint();
    // widget coordinates of things the mouse can grab (for the owner and the tests)
    QPoint pointPos(int lane, int curve, int index) const;
    int periodStartX(int pair) const;
    int periodEndX(int pair) const;
    static QColor curveColor(int curve);

    // Cuts and loops: in region mode a drag on the selected lane marks a range (snapped to its beats) instead of editing curves and periods.
    void setRegionMode(bool on);
    bool regionMode() const { return regionMode_; }
    void setRange(int lane, double startMs, double endMs);     // lane relative ms of the edited track; start >= end clears
    void clearRange() { setRange(-1, 0, 0); }

    static QString formatTime(double ms);                      // "m:ss", "120:00", "1:05:09" is not used: minutes keep counting
    static double tickStepMs(double msPerPixel);               // ruler tick spacing for a zoom

signals:
    void viewChanged();
    void playPositionChanged(double ms);
    void laneClicked(int lane, double ms);
    void hovered(int lane, double ms);
    void curveModeChanged(int curve);
    void mixTypeChosen(int pair, int type);
    void removeTrackRequested(int lane);
    void rangeDragged(int lane, double startMs, double endMs);       // region mode: ms from the start of the edited track, on beats

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    int contentWidth() const;
    int contentHeight() const;
    void layoutBars();
    void clampView();
    void paintLane(class QPainter& p, int lane);
    void paintRuler(class QPainter& p);
    void paintPeriods(class QPainter& p, int lane);
    void paintCurves(class QPainter& p, int lane);
    int yOfValue(int lane, double v) const;
    double valueAtY(int lane, int y) const;
    const trakcrate::Curve& curveOf(int lane, int curve) const;
    int findPoint(int lane, const QPoint& pos) const;                 // index in the edited curve, -1
    int findPeriodLine(int lane, int x, int* pairOut) const;          // 0 none, 1 out start, 2 in start, 3 end
    int pairAtX(int lane, int x) const;                               // the pair whose overlay on this lane contains x, -1

    EditFn editor_;
    int curveMode_ = 0;
    PointRef selPoint_;
    struct Drag {
        enum Kind { None, OutStart, InStart, End, Point, Range } kind = None;
        int pair = -1, lane = -1, curve = 0, index = -1;
        double press = 0, startValue = 0, startBeat = 0, pressMs = 0;
        QString key;
    } drag_;
    int dragCounter_ = 0;
    int currentPair_ = -1;
    bool regionMode_ = false;
    int rangeLane_ = -1;
    double rangeA_ = 0, rangeB_ = 0;

    const trakcrate::Mix* mix_ = nullptr;
    std::vector<trakcrate::TrackFacts> facts_;
    std::vector<MixLaneInfo> info_;
    std::vector<trakcrate::EffectiveTrack> eff_;
    std::vector<double> starts_;
    std::vector<QVector<quint16>> edited_;
    std::vector<bool> stretching_;
    double lengthMs_ = 0;
    int laneHeight_ = 100;
    double viewStart_ = 0, span_ = 60000;
    double playMs_ = 0;
    int selected_ = -1;
    QScrollBar* vbar_;
    QScrollBar* hbar_;
    bool adjusting_ = false;
};

} // namespace vdj

#endif
