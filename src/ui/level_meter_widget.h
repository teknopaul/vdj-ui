#ifndef LEVEL_METER_WIDGET_H
#define LEVEL_METER_WIDGET_H

#include <QElapsedTimer>
#include <QWidget>

namespace vdj {

// A stereo input meter for balancing recording levels (dark theme). Each channel is a bar from -60 dBFS to 0 dBFS, drawn in zones:
// below -18 dB dim green (low), -18 to -6 green (a good recording level), -6 to -1 yellow (hot), above -1 red. The bar shows the RMS level
// with a bright tick for the peak, a peak hold marker that falls after a second and a half, and a clip light that stays lit until it is
// clicked. "Signal" lights when either channel is above -60 dBFS, so you can see which input is receiving audio at all.
class LevelMeterWidget : public QWidget {
    Q_OBJECT
public:
    static constexpr float kFloorDb = -60.0f;       // the bottom of the scale, and the threshold for "signal present"
    static constexpr float kClipDb = -0.3f;         // a peak at or above this counts as clipping
    static constexpr float kTargetLowDb = -18.0f;
    static constexpr float kHotDb = -6.0f;
    static constexpr float kRedDb = -1.0f;

    explicit LevelMeterWidget(QWidget* parent = nullptr);

    // Linear 0..1 levels of one block of audio per channel (peak = largest sample, rms = root mean square).
    void setLevels(float peakL, float peakR, float rmsL, float rmsR);
    // Back to silence (e.g. when the input is closed); also clears the clip light and the holds.
    void reset();
    void clearClip();

    static float toDb(float linear);                // 20 log10, floored at kFloorDb
    float peakDb(int channel) const { return peakDb_[channel & 1]; }
    float peakHoldDb(int channel) const { return hold_[channel & 1]; }
    float rmsDb(int channel) const { return rmsDb_[channel & 1]; }
    bool hasSignal() const { return signal_; }
    bool clipped() const { return clip_; }
    // "low", "good", "hot" or "clip": where the louder channel's peak is, which is the advice shown under the bars
    QString zone() const;

    QSize sizeHint() const override { return QSize(260, 64); }
    QSize minimumSizeHint() const override { return QSize(160, 56); }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    float xFor(float db, int width) const;

    float peakDb_[2] = {kFloorDb, kFloorDb};
    float rmsDb_[2] = {kFloorDb, kFloorDb};
    float hold_[2] = {kFloorDb, kFloorDb};
    QElapsedTimer holdAge_[2];
    bool signal_ = false;
    bool clip_ = false;
};

} // namespace vdj
#endif
