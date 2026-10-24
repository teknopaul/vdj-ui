#ifndef BEAT_GRID_WIDGET_H
#define BEAT_GRID_WIDGET_H

#include <QWidget>
#include <QTimer>

namespace vdj {

class BeatGridWidget : public QWidget {
    Q_OBJECT

public:
    explicit BeatGridWidget(QWidget* parent = nullptr);

    void setBpm(double bpm);
    void setBeatTimestamp(qint64 millisSinceEpoch);
    void setBarPos(int barPos);
    void setPlaying(bool playing);
    // Drive the playhead from the VDJ's ALSA sequencer: beats since the sequencer started (the bar's beat 1 is a
    // whole multiple of 4). Negative switches back to the network beat reference.
    void setSequencerBeats(double beats);
    bool sequencerDriven() const { return seqBeats_ >= 0.0; }
    void startClock();
    void stopClock();

    double bpm() const { return bpm_; }
    bool playing() const { return playing_; }

    // True once the animation clock is running (drive by a beat reference).
    bool clockActive() const { return clock_ && clock_->isActive(); }

    // Playhead position (0..1) within the displayed bar. Correctly spans all
    // beatsPerBar beats so the playhead takes one full bar to traverse the
    // grid width.
    static double barPosition(int currentBeat, double beatPhase, int beatsPerBar);

    // Playhead bar-fraction driven by the network's 1-based bar_pos (1..4)
    // plus the sub-beat phase. This is what the paint code uses; keeping it
    // here makes the "one full bar epoch" guarantee testable.
    static double playheadBarFraction(int barPos, double beatPhase, int beatsPerBar);

signals:
    void playheadAdvanced();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double beatPhase() const;
    void addBeat();

    double bpm_ = 120.0;
    qint64 lastBeatMs_ = 0;
    int barPos_ = 1;
    double seqBeats_ = -1.0;
    bool playing_ = true;
    QTimer* clock_;
};

} // namespace vdj

#endif // BEAT_GRID_WIDGET_H
