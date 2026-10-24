#include "beat_grid_widget.h"
#include <QPainter>
#include <QTimer>
#include <QDateTime>
#include <QtMath>
#include <algorithm>

namespace vdj {

BeatGridWidget::BeatGridWidget(QWidget* parent)
    : QWidget(parent)
    , clock_(new QTimer(this))
{
    setMinimumHeight(64);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    clock_->setInterval(16); // ~60fps
    connect(clock_, &QTimer::timeout, this, [this]() {
        update();
        emit playheadAdvanced();
    });
}

void BeatGridWidget::setBpm(double bpm)
{
    if (bpm > 0.0 && bpm_ != bpm) {
        bpm_ = bpm;
        update();
    }
}

void BeatGridWidget::setBeatTimestamp(qint64 millisSinceEpoch)
{
    lastBeatMs_ = millisSinceEpoch;
    // A real beat reference means this deck might be playing; ensure its
    // animation clock is running so the playhead actually sweeps. Decks can
    // be created after startup (deck left then rejoined), so the clock must
    // not depend solely on the initial startDemoClocks() pass.
    if (lastBeatMs_ != 0 && !clock_->isActive())
        clock_->start();
    update();
}

void BeatGridWidget::setBarPos(int barPos)
{
    int clamped = std::clamp(barPos, 1, 4);
    if (barPos_ != clamped) {
        barPos_ = clamped;
        update();
    }
}

void BeatGridWidget::setSequencerBeats(double beats)
{
    if (beats == seqBeats_)
        return;
    seqBeats_ = beats < 0.0 ? -1.0 : beats;
    update();
}

void BeatGridWidget::setPlaying(bool playing)
{
    playing_ = playing;
    update();
}

void BeatGridWidget::startClock()
{
    clock_->start();
}

void BeatGridWidget::stopClock()
{
    clock_->stop();
}

double BeatGridWidget::beatPhase() const
{
    if (!playing_ || lastBeatMs_ == 0 || bpm_ <= 0.0) {
        // idle or unknown beat/bpm: hold the beat position steady
        return 0.0;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 elapsed = now - lastBeatMs_;
    double beatMs = 60000.0 / bpm_;
    double t = static_cast<double>(elapsed) / beatMs;
    return t - std::floor(t);
}

void BeatGridWidget::addBeat()
{
    lastBeatMs_ = QDateTime::currentMSecsSinceEpoch();
    update();
}

double BeatGridWidget::barPosition(int currentBeat, double beatPhase, int beatsPerBar)
{
    if (beatsPerBar <= 0)
        return 0.0;
    int beat = std::clamp(currentBeat, 0, beatsPerBar - 1);
    double phase = std::clamp(beatPhase, 0.0, 0.999999);
    return (beat + phase) / beatsPerBar;
}

double BeatGridWidget::playheadBarFraction(int barPos, double beatPhase, int beatsPerBar)
{
    // bar_pos is 1-based (1..4). Deriving the beat index purely from the
    // sub-beat phase would cycle the bar every single beat (4x too fast).
    int barIndex = std::clamp(barPos - 1, 0, beatsPerBar - 1);
    return barPosition(barIndex, beatPhase, beatsPerBar);
}

void BeatGridWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);

    p.fillRect(rect(), QColor(0x14, 0x14, 0x14));

    // draw beat grid: one bar = 4 beats, subdivide into 16ths
    const int bars = 1;
    const int beatsPerBar = 4;
    const int subdiv = 4; // 16th notes

    const int gridW = width();
    const double beatW = static_cast<double>(gridW) / (bars * beatsPerBar);
    const double subW = beatW / subdiv;

    // sub-beat lines
    for (int b = 0; b < bars * beatsPerBar * subdiv; ++b) {
        double x = b * subW;
        bool isBeat = (b % subdiv == 0);
        bool isDown = (b % (subdiv * beatsPerBar) == 0);
        p.setPen(QColor(0x26, 0x26, 0x26));
        if (isBeat) p.setPen(QColor(0x3a, 0x3a, 0x3a));
        if (isDown) p.setPen(QColor(0x4a, 0x4a, 0x4a));
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
    }

    // highlight current beat position
    // Only draw the playhead when the deck is actually playing and has a
    // real beat reference; otherwise the timeline must stay static.
    if (seqBeats_ >= 0.0) {
        // the VDJ's own sequencer is running: one bar is 4 beats of its queue
        const double inBar = std::fmod(seqBeats_, double(beatsPerBar));
        const int barIndex = std::clamp(int(inBar), 0, beatsPerBar - 1);
        p.fillRect(QRectF(barIndex * beatW, 0, beatW, height()), QColor(0x1c, 0x30, 0x1c));
        const double playheadX = inBar / beatsPerBar * gridW;
        p.setPen(QPen(QColor(0x66, 0xcc, 0xff), 2));
        p.drawLine(QPointF(playheadX, 0), QPointF(playheadX, height()));
    } else if (playing_ && lastBeatMs_ != 0) {
        double phase = beatPhase(); // 0..1 within the current beat
        // bar_pos is the 1-based position of the beat within the bar
        // (1..4), supplied by the network. Use it as the beat index so the
        // playhead advances across all 4 beats over one full bar. Deriving
        // the index from `phase * beatsPerBar` instead made it cycle 0..3
        // within a single beat, crossing the bar 4x too fast.
        double barPhase = playheadBarFraction(barPos_, phase, beatsPerBar);
        double playheadX = barPhase * gridW;
        int barIndex = std::clamp(barPos_ - 1, 0, beatsPerBar - 1);

        // bar marker: shade the current beat
        QRectF beatRect(barIndex * beatW, 0, beatW, height());
        p.fillRect(beatRect, QColor(0x1c, 0x30, 0x1c));

        // playhead vertical line
        p.setPen(QPen(QColor(0x66, 0xcc, 0xff), 2));
        p.drawLine(QPointF(playheadX, 0), QPointF(playheadX, height()));
    } else {
        p.setPen(QColor(0x55, 0x55, 0x55));
        p.drawText(4, height() - 4, "STOPPED");
    }
}

} // namespace vdj
