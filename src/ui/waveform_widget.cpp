#include "waveform_widget.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace vdj {

// Colours the PWV5 waveform cannot produce, except white (beats) and red (bars), which are tested on the bottom row
// where the waveform never reaches.
const QColor WaveformWidget::kCueColor(255, 224, 0);
const QColor WaveformWidget::kBeatColor(255, 255, 255);
const QColor WaveformWidget::kBarColor(255, 40, 40);

WaveformWidget::WaveformWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(60);
}

void WaveformWidget::setInteractive(bool on)
{
    interactive_ = on;
    setMouseTracking(on);
    setFocusPolicy(on ? Qt::StrongFocus : Qt::NoFocus);
    setCursor(on ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void WaveformWidget::setPlayPosition(double ms)
{
    if (ms == playMs_)
        return;
    playMs_ = ms;
    update();
}

void WaveformWidget::setSeekable(bool on)
{
    seekable_ = on;
    if (!interactive_)
        setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void WaveformWidget::setAnalysis(const TrackAnalysis& analysis)
{
    analysis_ = analysis;
    viewStart_ = 0.0;
    viewSpan_ = 0.0;
    update();
    emit viewChanged();
}

void WaveformWidget::clear()
{
    setAnalysis(TrackAnalysis());
}

double WaveformWidget::zoom() const
{
    const double len = lengthMs();
    return (len > 0 && viewSpan_ > 0) ? len / viewSpan_ : 1.0;
}

void WaveformWidget::setView(double startMs, double spanMs)
{
    const double len = lengthMs();
    if (len <= 0) {
        viewStart_ = 0.0;
        viewSpan_ = 0.0;
        return;
    }
    spanMs = std::max(std::min(spanMs, len), std::min(kMinSpanMs, len));
    startMs = std::max(0.0, std::min(startMs, len - spanMs));
    if (startMs == viewStart_ && spanMs == viewSpan_)
        return;
    viewStart_ = startMs;
    viewSpan_ = spanMs;
    update();
    emit viewChanged();
}

void WaveformWidget::fit()
{
    viewStart_ = 0.0;
    viewSpan_ = 0.0;
    update();
    emit viewChanged();
}

void WaveformWidget::zoomAt(int x, double factor)
{
    const double len = lengthMs();
    if (len <= 0 || factor <= 0 || width() <= 0)
        return;
    const double span = viewSpan_ > 0 ? viewSpan_ : len;
    const double start = viewSpan_ > 0 ? viewStart_ : 0.0;
    const double anchor = start + double(x) * span / width();
    const double newSpan = std::max(std::min(span / factor, len), std::min(kMinSpanMs, len));
    setView(anchor - double(x) * newSpan / width(), newSpan);
}

void WaveformWidget::panBy(double ms)
{
    const double len = lengthMs();
    const double span = viewSpan_ > 0 ? viewSpan_ : len;
    setView((viewSpan_ > 0 ? viewStart_ : 0.0) + ms, span);
}

int WaveformWidget::timeX(double timeMs) const
{
    const double len = lengthMs();
    const double span = viewSpan_ > 0 ? viewSpan_ : len;
    const double start = viewSpan_ > 0 ? viewStart_ : 0.0;
    return span > 0 ? int(std::lround((timeMs - start) * width() / span)) : 0;
}

int WaveformWidget::cueX(quint32 timeMs) const
{
    return timeX(double(timeMs));
}

double WaveformWidget::timeAt(int x) const
{
    const double len = lengthMs();
    const double span = viewSpan_ > 0 ? viewSpan_ : len;
    const double start = viewSpan_ > 0 ? viewStart_ : 0.0;
    return width() > 0 ? start + double(x) * span / width() : 0.0;
}

int WaveformWidget::nearestBeat(double ms) const
{
    const auto& b = analysis_.beats;
    if (b.isEmpty())
        return -1;
    auto it = std::lower_bound(b.begin(), b.end(), ms, [](const AnalysisBeat& a, double t) { return a.ms < t; });
    int i = int(it - b.begin());
    if (i >= b.size()) return b.size() - 1;
    if (i > 0 && std::fabs(b[i - 1].ms - ms) <= std::fabs(b[i].ms - ms)) --i;
    return i;
}

static QString mmss(double ms)
{
    const qint64 t = qint64(std::llround(ms));
    return QStringLiteral("%1:%2.%3").arg(t / 60000).arg(t / 1000 % 60, 2, 10, QChar('0')).arg(t % 1000, 3, 10, QChar('0'));
}

QString WaveformWidget::describePosition(int x) const
{
    const double t = timeAt(x);
    QString s = mmss(t);
    const int i = nearestBeat(t);
    if (i >= 0) {
        int bar = 0;                                    // beats before the first downbeat (a pickup) are bar 0
        for (int k = 0; k <= i; ++k)
            if (analysis_.beats[k].number == 1) ++bar;
        const AnalysisBeat& b = analysis_.beats[i];
        s += QStringLiteral("   nearest beat %1 (bar %2, beat %3) at %4   %5%6 ms")
                 .arg(i + 1).arg(bar).arg(b.number).arg(mmss(b.ms))
                 .arg(t >= b.ms ? "+" : "-").arg(std::fabs(t - b.ms), 0, 'f', 1);
    }
    return s;
}

void WaveformWidget::paintWaveform(QPainter& p, double t0, double span)
{
    const int W = width(), H = height(), mid = H / 2;
    if (!analysis_.color.isEmpty()) {
        const int n = analysis_.color.size();
        for (int x = 0; x < W; ++x) {
            const double tA = t0 + double(x) * span / W, tB = t0 + double(x + 1) * span / W;
            int a = int(tA * 150.0 / 1000.0);
            int b = std::max(a + 1, int(tB * 150.0 / 1000.0));
            if (a < 0 || a >= n) continue;
            b = std::min(b, n);
            quint16 best = analysis_.color[a];
            int bestH = (best >> 2) & 31;
            for (int i = a + 1; i < b; ++i) {
                const int h = (analysis_.color[i] >> 2) & 31;
                if (h > bestH) { bestH = h; best = analysis_.color[i]; }
            }
            const QColor c(((best >> 13) & 7) * 255 / 7, ((best >> 10) & 7) * 255 / 7, ((best >> 7) & 7) * 255 / 7);
            const int half = bestH * (mid - 2) / 31;
            p.setPen(c);
            p.drawLine(x, mid - half, x, mid + half);
        }
    } else if (!analysis_.preview.isEmpty()) {
        const int n = analysis_.preview.size();
        const double len = lengthMs();
        p.setPen(QColor(90, 170, 255));
        for (int x = 0; x < W; ++x) {
            const double t = t0 + double(x) * span / W;
            const int i = len > 0 ? int(t * n / len) : 0;
            if (i < 0 || i >= n) continue;
            const int half = analysis_.preview[i] * (mid - 2) / 31;
            p.drawLine(x, mid - half, x, mid + half);
        }
    }
}

void WaveformWidget::paintBeats(QPainter& p, double t0, double span)
{
    const auto& beats = analysis_.beats;
    if (beats.size() < 2 || span <= 0)
        return;
    const int W = width(), H = height(), mid = H / 2;
    const double beatMs = (beats.last().ms - beats.first().ms) / (beats.size() - 1);
    const double pxPerBeat = beatMs * W / span;
    const bool drawBeats = pxPerBeat >= kMinLineSpacing;
    const bool drawBars = pxPerBeat * 4 >= kMinLineSpacing;
    if (!drawBars)
        return;

    auto first = std::lower_bound(beats.begin(), beats.end(), t0 - 2.0 * beatMs,
                                  [](const AnalysisBeat& b, double t) { return b.ms < t; });
    const QColor halo(0, 0, 0, 130);
    for (auto it = first; it != beats.end() && it->ms <= t0 + span + 2.0 * beatMs; ++it) {
        const bool bar = it->number == 1;
        if (!bar && !drawBeats)
            continue;
        const int x = timeX(it->ms);
        if (x < -2 || x > W + 1)
            continue;
        const int w = bar ? 2 : 1;
        p.fillRect(QRect(x - 1, mid, w + 2, H - mid), halo);                      // dark edge so it reads on bright audio
        p.fillRect(QRect(x, mid, w, H - mid), bar ? kBarColor : kBeatColor);     // bottom half only
    }
}

void WaveformWidget::paintCues(QPainter& p)
{
    const int H = height();
    for (const AnalysisCue& c : analysis_.cues) {
        const int x = cueX(c.timeMs);
        if (c.loop && c.loopEndMs > c.timeMs) {
            QColor band = kCueColor;
            band.setAlpha(45);
            p.fillRect(QRect(x, 0, std::max(2, cueX(c.loopEndMs) - x), H), band);
        }
        if (x < -6 || x > width() + 6)
            continue;
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(QRect(x - 1, 0, 3, H), QColor(0, 0, 0, 110));
        p.setPen(QPen(kCueColor, 1));
        p.drawLine(x, 0, x, H);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(kCueColor);
        const QPointF arrow[3] = { QPointF(x - 4.5, 0.0), QPointF(x + 5.5, 0.0), QPointF(x + 0.5, 6.0) };
        p.drawPolygon(arrow, 3);
        p.setRenderHint(QPainter::Antialiasing, false);
    }
}

void WaveformWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(16, 16, 20));

    const double len = lengthMs();
    if (!hasWaveform() && analysis_.beats.isEmpty()) {
        p.setPen(palette().color(QPalette::Disabled, QPalette::WindowText));
        p.drawText(rect(), Qt::AlignCenter, analysis_.valid ? QStringLiteral("no waveform") : QStringLiteral("no analysis"));
        return;
    }
    if (len <= 0)
        return;
    const double span = viewSpan_ > 0 ? viewSpan_ : len;
    const double t0 = viewSpan_ > 0 ? viewStart_ : 0.0;

    paintWaveform(p, t0, span);
    paintBeats(p, t0, span);
    paintCues(p);
    paintPlayhead(p);
}

void WaveformWidget::paintPlayhead(QPainter& p)
{
    if (playMs_ < 0 || lengthMs() <= 0)
        return;
    const int x = timeX(playMs_);
    if (x < 0 || x >= width())
        return;
    p.setPen(QPen(QColor(0, 255, 90), 2));
    p.drawLine(x, 0, x, height());
}

void WaveformWidget::wheelEvent(QWheelEvent* event)
{
    if (!interactive_) {
        event->ignore();
        return;
    }
    const double steps = event->angleDelta().y() / 120.0;
    if (steps != 0.0)
        zoomAt(int(event->position().x()), std::pow(1.25, steps));
    event->accept();
}

void WaveformWidget::mousePressEvent(QMouseEvent* event)
{
    if (!interactive_ && seekable_ && event->button() == Qt::LeftButton && lengthMs() > 0) {
        emit seekRequested(std::clamp(timeAt(int(event->position().x())), 0.0, lengthMs()));
        return;
    }
    if (!interactive_ || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    setFocus();
    dragging_ = true;
    dragX_ = int(event->position().x());
    dragStart_ = viewSpan_ > 0 ? viewStart_ : 0.0;
    setCursor(Qt::ClosedHandCursor);
}

void WaveformWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!interactive_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const int x = int(event->position().x());
    if (dragging_) {
        const double span = viewSpan_ > 0 ? viewSpan_ : lengthMs();
        setView(dragStart_ - double(x - dragX_) * span / std::max(1, width()), span);
    }
    emit hovered(describePosition(x));
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (dragging_ && event->button() == Qt::LeftButton) {
        dragging_ = false;
        setCursor(Qt::OpenHandCursor);
    }
}

void WaveformWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    if (interactive_)
        fit();
    else
        emit activated();
}

void WaveformWidget::keyPressEvent(QKeyEvent* event)
{
    if (!interactive_) {
        QWidget::keyPressEvent(event);
        return;
    }
    const double span = viewSpan_ > 0 ? viewSpan_ : lengthMs();
    switch (event->key()) {
        case Qt::Key_Left:  panBy(-span / 10); break;
        case Qt::Key_Right: panBy(span / 10); break;
        case Qt::Key_Plus:
        case Qt::Key_Equal: zoomAt(width() / 2, 1.5); break;
        case Qt::Key_Minus: zoomAt(width() / 2, 1.0 / 1.5); break;
        case Qt::Key_Home:  fit(); break;
        default: QWidget::keyPressEvent(event); return;
    }
}

void WaveformWidget::leaveEvent(QEvent* event)
{
    emit hovered(QString());
    QWidget::leaveEvent(event);
}

void WaveformWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    emit viewChanged();
}

} // namespace vdj
