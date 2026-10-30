#include "level_meter_widget.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace vdj {

namespace {
constexpr int kHoldMs = 1500;
constexpr float kFallDbPerSec = 24.0f;
constexpr int kClipBox = 14;
}

LevelMeterWidget::LevelMeterWidget(QWidget* parent) : QWidget(parent)
{
    setToolTip(QStringLiteral("Input level, -60 to 0 dBFS. Aim for peaks between -18 and -6 dB (the green zone) on the loudest music: yellow is hot, "
                              "red clips. Level the channels against each other by their peak figures. The clip light stays on until you click it."));
    setMinimumHeight(56);
    for (QElapsedTimer& t : holdAge_) t.start();
}

float LevelMeterWidget::toDb(float linear)
{
    if (!(linear > 0.0f)) return kFloorDb;
    return std::max(kFloorDb, 20.0f * std::log10(linear));
}

void LevelMeterWidget::setLevels(float peakL, float peakR, float rmsL, float rmsR)
{
    const float peaks[2] = {toDb(peakL), toDb(peakR)};
    const float rmss[2] = {toDb(rmsL), toDb(rmsR)};
    for (int c = 0; c < 2; ++c) {
        // the bar falls smoothly instead of flickering; a louder block jumps up at once
        const float fall = kFallDbPerSec * 0.023f;                      // one block is about 23 ms
        rmsDb_[c] = rmss[c] > rmsDb_[c] ? rmss[c] : std::max(rmss[c], rmsDb_[c] - fall);
        peakDb_[c] = peaks[c];
        if (peaks[c] >= hold_[c]) {
            hold_[c] = peaks[c];
            holdAge_[c].restart();
        } else if (holdAge_[c].elapsed() > kHoldMs) {
            hold_[c] = std::max(peaks[c], hold_[c] - kFallDbPerSec * 0.023f);
        }
        if (peaks[c] >= kClipDb) clip_ = true;
    }
    signal_ = peaks[0] > kFloorDb || peaks[1] > kFloorDb;
    update();
}

void LevelMeterWidget::reset()
{
    for (int c = 0; c < 2; ++c) { peakDb_[c] = rmsDb_[c] = hold_[c] = kFloorDb; holdAge_[c].restart(); }
    signal_ = false;
    clip_ = false;
    update();
}

void LevelMeterWidget::clearClip()
{
    clip_ = false;
    update();
}

QString LevelMeterWidget::zone() const
{
    const float p = std::max(hold_[0], hold_[1]);
    if (clip_ || p >= kClipDb) return QStringLiteral("clip");
    if (p >= kHotDb) return QStringLiteral("hot");
    if (p >= kTargetLowDb) return QStringLiteral("good");
    return QStringLiteral("low");
}

float LevelMeterWidget::xFor(float db, int width) const
{
    return (std::clamp(db, kFloorDb, 0.0f) - kFloorDb) / (0.0f - kFloorDb) * float(width);
}

void LevelMeterWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) clearClip();
}

void LevelMeterWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor(24, 24, 24));
    const int labelW = 14, readW = 62, top = 4, barH = 14, gap = 4;
    const int x0 = labelW, barW = std::max(20, width() - labelW - readW - kClipBox - 8);
    const QFontMetrics fm(font());

    for (int c = 0; c < 2; ++c) {
        const int y = top + c * (barH + gap);
        p.setPen(QColor(170, 170, 170));
        p.drawText(QRect(0, y, labelW, barH), Qt::AlignCenter, c == 0 ? QStringLiteral("L") : QStringLiteral("R"));
        // zones, dim behind the level
        struct Zone { float from, to; QColor on, off; };
        const Zone zones[] = {
            {kFloorDb, kTargetLowDb, QColor(60, 150, 90), QColor(34, 52, 40)},
            {kTargetLowDb, kHotDb, QColor(70, 210, 110), QColor(36, 70, 46)},
            {kHotDb, kRedDb, QColor(240, 200, 40), QColor(78, 68, 24)},
            {kRedDb, 0.0f, QColor(235, 60, 50), QColor(80, 28, 26)},
        };
        for (const Zone& z : zones) {
            const int xa = x0 + int(xFor(z.from, barW)), xb = x0 + int(xFor(z.to, barW));
            p.fillRect(xa, y, xb - xa, barH, z.off);
            const float lit = std::min(z.to, rmsDb_[c]);
            if (lit > z.from) p.fillRect(xa, y, int(xFor(lit, barW)) + x0 - xa, barH, z.on);
        }
        // the peak of the last block, and the held peak
        p.setPen(QColor(255, 255, 255));
        p.drawLine(x0 + int(xFor(peakDb_[c], barW)), y, x0 + int(xFor(peakDb_[c], barW)), y + barH - 1);
        if (hold_[c] > kFloorDb) {
            p.setPen(QPen(hold_[c] >= kClipDb ? QColor(255, 90, 80) : QColor(230, 230, 230), 2));
            const int hx = x0 + std::min(barW - 1, int(xFor(hold_[c], barW)));
            p.drawLine(hx, y, hx, y + barH - 1);
        }
        // the peak figure: this is what to compare between inputs
        p.setPen(hold_[c] >= kHotDb ? (hold_[c] >= kRedDb ? QColor(255, 100, 90) : QColor(240, 200, 40)) : QColor(200, 200, 200));
        p.drawText(QRect(x0 + barW + 4, y, readW, barH), Qt::AlignVCenter | Qt::AlignRight,
                   hold_[c] > kFloorDb ? QStringLiteral("%1 dB").arg(double(hold_[c]), 0, 'f', 1) : QStringLiteral("-inf"));
    }
    // scale ticks under the bars
    const int ty = top + 2 * (barH + gap) - 2;
    p.setPen(QColor(120, 120, 120));
    for (int db : {-60, -48, -36, -24, -18, -12, -6, 0}) {
        const int tx = x0 + std::min(barW - 1, int(xFor(float(db), barW)));
        p.drawLine(tx, ty, tx, ty + 3);
        if (db == -48 || db == -24 || db == -12 || db == -6 || db == 0)
            p.drawText(QRect(tx - 14, ty + 3, 28, fm.height()), Qt::AlignHCenter | Qt::AlignTop, QString::number(db));
    }
    // clip light and signal light
    const int cx = width() - kClipBox - 2;
    p.fillRect(cx, top, kClipBox, barH, clip_ ? QColor(235, 40, 30) : QColor(62, 28, 26));
    p.setPen(QColor(210, 210, 210));
    p.drawText(QRect(cx - 4, top, kClipBox + 8, barH), Qt::AlignCenter, QStringLiteral("C"));
    p.fillRect(cx, top + barH + gap, kClipBox, barH, signal_ ? QColor(70, 210, 110) : QColor(30, 50, 36));
    p.setPen(signal_ ? QColor(10, 30, 10) : QColor(150, 150, 150));
    p.drawText(QRect(cx - 4, top + barH + gap, kClipBox + 8, barH), Qt::AlignCenter, QStringLiteral("S"));
}

} // namespace vdj
