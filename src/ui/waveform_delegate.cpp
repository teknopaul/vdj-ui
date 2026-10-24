#include "waveform_delegate.h"
#include "track_analysis.h"
#include <QApplication>
#include <QPainter>
#include <QStyle>

namespace vdj {

MiniWaveformDelegate::MiniWaveformDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void MiniWaveformDelegate::setRoot(const QString& root)
{
    root_ = root;
    cache_.clear();
}

const QVector<quint8>& MiniWaveformDelegate::columnsFor(const QString& analyzePath) const
{
    auto it = cache_.find(analyzePath);
    if (it == cache_.end())
        it = cache_.insert(analyzePath, root_.isEmpty() ? QVector<quint8>() : loadMiniWaveform(root_, analyzePath));
    return it.value();
}

QSize MiniWaveformDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    s.setWidth(104);
    return s;
}

void MiniWaveformDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    // background / selection as usual, no text
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();
    opt.features &= ~QStyleOptionViewItem::HasDisplay;
    const QWidget* w = option.widget;
    (w ? w->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &opt, painter, w);

    const QString path = index.siblingAtColumn(0).data(kAnalyzePathRole).toString();
    if (path.isEmpty())
        return;
    const QVector<quint8>& cols = columnsFor(path);
    if (cols.isEmpty())
        return;

    const QRect r = option.rect.adjusted(2, 2, -2, -2);
    if (r.width() < 4 || r.height() < 4)
        return;
    const bool selected = option.state & QStyle::State_Selected;
    painter->save();
    painter->setClipRect(option.rect);
    painter->setPen(Qt::NoPen);
    painter->setBrush(selected ? option.palette.highlightedText().color() : QColor(70, 160, 255));
    const int n = cols.size();
    for (int i = 0; i < n; ++i) {
        const int x0 = r.left() + i * r.width() / n;
        const int x1 = r.left() + (i + 1) * r.width() / n;
        const int h = qMax(1, r.height() * cols[i] / 15);
        painter->drawRect(x0, r.bottom() - h + 1, qMax(1, x1 - x0), h);
    }
    painter->restore();
}

} // namespace vdj
