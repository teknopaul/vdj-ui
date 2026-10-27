#include "star_rating_widget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace vdj {

namespace {
constexpr int kStar = 18;
constexpr int kGap = 2;
}

StarRatingWidget::StarRatingWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

QSize StarRatingWidget::sizeHint() const { return QSize(5 * kStar + 4 * kGap, kStar + 2); }

void StarRatingWidget::setRating(int stars)
{
    stars = std::max(0, std::min(5, stars));
    if (stars == rating_) return;
    rating_ = stars;
    update();
}

void StarRatingWidget::setEditable(bool on)
{
    editable_ = on;
    if (!on) hover_ = 0;
    update();
}

int StarRatingWidget::starAt(const QPoint& p) const
{
    if (p.y() < 0 || p.y() > height()) return 0;
    const int n = p.x() / (kStar + kGap) + 1;
    return p.x() >= 0 && n >= 1 && n <= 5 ? n : 0;
}

void StarRatingWidget::userSet(int stars)
{
    if (!editable_) return;
    if (stars == rating_) stars = 0;                       // the same star again clears
    rating_ = stars;
    update();
    emit ratingChanged(rating_);
}

void StarRatingWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int shown = hover_ ? hover_ : rating_;
    for (int i = 0; i < 5; ++i) {
        const double cx = i * (kStar + kGap) + kStar / 2.0, cy = 1 + kStar / 2.0, r = kStar / 2.0 - 1;
        QPainterPath path;
        for (int k = 0; k < 10; ++k) {
            const double ang = -M_PI / 2 + k * M_PI / 5, rad = k % 2 ? r * 0.45 : r;
            const QPointF pt(cx + rad * std::cos(ang), cy + rad * std::sin(ang));
            if (k == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        path.closeSubpath();
        const bool on = i < shown;
        p.setPen(QPen(palette().color(isEnabled() ? QPalette::WindowText : QPalette::Mid), 1));
        p.setBrush(on ? (hover_ ? QColor(255, 210, 90) : QColor(240, 180, 20)) : Qt::NoBrush);
        p.drawPath(path);
    }
}

void StarRatingWidget::mouseMoveEvent(QMouseEvent* e)
{
    const int n = editable_ ? starAt(e->pos()) : 0;
    if (n != hover_) { hover_ = n; update(); }
}

void StarRatingWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    const int n = starAt(e->pos());
    if (n) userSet(n);
}

void StarRatingWidget::leaveEvent(QEvent*) { if (hover_) { hover_ = 0; update(); } }

void StarRatingWidget::keyPressEvent(QKeyEvent* e)
{
    if (e->key() >= Qt::Key_0 && e->key() <= Qt::Key_5 && editable_) {
        const int n = e->key() - Qt::Key_0;
        if (n != rating_) { rating_ = n; update(); emit ratingChanged(rating_); }
        return;
    }
    QWidget::keyPressEvent(e);
}

} // namespace vdj
