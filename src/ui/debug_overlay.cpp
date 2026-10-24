#include "debug_overlay.h"
#include <QPainter>
#include <QFontMetricsF>

namespace vdj {

DebugOverlay::DebugOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void DebugOverlay::setLines(const QStringList& lines)
{
    lines_ = lines;
    update();
}

void DebugOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setFont(QFont(QStringLiteral("monospace"), 8));
    QFontMetricsF fm(p.font());
    QRect bg(rect());
    p.fillRect(bg, QColor(0, 0, 0, 0xaa));

    p.setPen(QColor(0x88ff88));
    p.drawText(rect().adjusted(4, 4, -4, -4),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               lines_.join(QLatin1Char('\n')));
}

} // namespace vdj