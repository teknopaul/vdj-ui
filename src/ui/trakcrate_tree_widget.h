#ifndef TRAKCRATE_TREE_WIDGET_H
#define TRAKCRATE_TREE_WIDGET_H

#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QTreeWidget>

#include <functional>

namespace vdj {

// The folder / playlist / pendrive tree. The lists of a virtual pendrive (items with role `entryRole` set) can be dragged into a new order within their
// pendrive. The widget moves nothing: it says which list moved and before which list it was dropped, and the owner reorders on disk and rebuilds.
class TrakcrateTreeWidget : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

    int kindRole = Qt::UserRole;          // the data role holding "entry" for a pendrive list
    int driveRole = Qt::UserRole + 3;     // the role holding the pendrive's name for a list
    std::function<void(const QString& drive, int fromIndex, int insertBefore)> onEntryDropped;      // indexes within the pendrive's lists

    std::function<void(int direction)> onKeyMove;      // Ctrl+Up (-1) / Ctrl+Down (+1) on a pendrive list: one place up or down in its pendrive

    // Where a drop at pos would insert, as an index among the pendrive's lists: -1 when the drop is not allowed (another pendrive, not a list).
    int insertionIndexAt(const QPoint& pos, QString* drive = nullptr, int* from = nullptr) const
    {
        QTreeWidgetItem* dragged = currentItem();
        if (!dragged || dragged->data(0, kindRole).toString() != QLatin1String("entry")) return -1;
        QTreeWidgetItem* parent = dragged->parent();
        if (!parent) return -1;
        if (drive) *drive = dragged->data(0, driveRole).toString();
        if (from) *from = parent->indexOfChild(dragged);
        QTreeWidgetItem* over = itemAt(pos);
        if (!over) return -1;
        if (over == parent) return parent->childCount();                  // on the pendrive itself: the end
        if (over->parent() != parent) return -1;
        const int i = parent->indexOfChild(over);
        return i + (pos.y() > visualItemRect(over).center().y() ? 1 : 0);
    }

protected:
    void keyPressEvent(QKeyEvent* e) override
    {
        if (onKeyMove && (e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & (Qt::ShiftModifier | Qt::AltModifier)) &&
            (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) && currentItem() && currentItem()->data(0, kindRole).toString() == QLatin1String("entry")) {
            onKeyMove(e->key() == Qt::Key_Up ? -1 : 1);
            e->accept();
            return;
        }
        QTreeWidget::keyPressEvent(e);
    }
    void dragMoveEvent(QDragMoveEvent* e) override
    {
        QTreeWidget::dragMoveEvent(e);                                    // draws the drop indicator
        if (e->source() == this && insertionIndexAt(e->position().toPoint()) >= 0) e->acceptProposedAction(); else e->ignore();
    }
    void dropEvent(QDropEvent* e) override
    {
        QString drive;
        int from = -1;
        const int at = e->source() == this ? insertionIndexAt(e->position().toPoint(), &drive, &from) : -1;
        e->setDropAction(Qt::CopyAction);                                 // never let Qt move or delete the item itself
        if (at < 0 || !onEntryDropped) { e->ignore(); return; }
        e->accept();
        onEntryDropped(drive, from, at);
    }
};

} // namespace vdj

#endif
