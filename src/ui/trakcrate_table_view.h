#ifndef TRAKCRATE_TABLE_VIEW_H
#define TRAKCRATE_TABLE_VIEW_H

#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QTreeView>

#include <functional>
#include <vector>

namespace vdj {

// The track table. In a playlist its rows can be dragged to a new place: the view does not move anything itself, it tells the owner which rows were
// dragged and before which row they were dropped (rows.size() = at the end), and the owner reorders the playlist on disk and reloads.
class TrakcrateTableView : public QTreeView {
public:
    using QTreeView::QTreeView;

    std::function<void(const std::vector<int>& rows, int insertBefore)> onRowsDropped;      // rows are the view's rows

    std::function<void(int direction)> onKeyMove;      // Ctrl+Up (-1) / Ctrl+Down (+1): move the selected rows one place

    // Where a drop at y would insert: before the row under the pointer when in its upper half, after it otherwise, the end below the last row.
    int insertionRowAt(const QPoint& pos) const
    {
        const QModelIndex idx = indexAt(pos);
        if (!idx.isValid()) return model() ? model()->rowCount() : 0;
        const QRect r = visualRect(idx);
        return idx.row() + (pos.y() > r.center().y() ? 1 : 0);
    }

protected:
    void keyPressEvent(QKeyEvent* e) override
    {
        if (onKeyMove && (e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & (Qt::ShiftModifier | Qt::AltModifier)) &&
            (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
            onKeyMove(e->key() == Qt::Key_Up ? -1 : 1);
            e->accept();
            return;
        }
        QTreeView::keyPressEvent(e);
    }
    void dragEnterEvent(QDragEnterEvent* e) override
    {
        if (e->source() == this && dragDropMode() != NoDragDrop) e->acceptProposedAction(); else e->ignore();
    }
    void dragMoveEvent(QDragMoveEvent* e) override
    {
        if (e->source() == this && dragDropMode() != NoDragDrop) e->acceptProposedAction(); else e->ignore();
    }
    void dropEvent(QDropEvent* e) override
    {
        if (e->source() != this || !onRowsDropped) { e->ignore(); return; }
        std::vector<int> rows;
        for (const QModelIndex& i : selectionModel()->selectedRows()) rows.push_back(i.row());
        const int at = insertionRowAt(e->position().toPoint());
        e->setDropAction(Qt::CopyAction);              // a "move" would make Qt delete the dragged rows from the model afterwards
        e->accept();
        if (!rows.empty()) onRowsDropped(rows, at);
    }
};

} // namespace vdj

#endif
