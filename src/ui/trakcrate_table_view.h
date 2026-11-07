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

    std::function<void()> onEnter;                       // Enter / Return on the current row (not while a cell is being edited)

    std::function<void(int direction)> onKeyMove;      // Ctrl+Up (-1) / Ctrl+Down (+1): move the selected rows one place

    // Keys that remotely operate the Local Track Player (Space, Page Up / Down) while this view keeps the focus. playerWantsKey says whether the
    // player would act on the key (so Space stays the VDJ's play / pause when nothing is loaded); onPlayerKey does it. Not used while editing a cell.
    std::function<bool(const QKeyEvent*)> playerWantsKey;
    std::function<bool(QKeyEvent*)> onPlayerKey;

    // Where a drop at y would insert: before the row under the pointer when in its upper half, after it otherwise, the end below the last row.
    int insertionRowAt(const QPoint& pos) const
    {
        const QModelIndex idx = indexAt(pos);
        if (!idx.isValid()) return model() ? model()->rowCount() : 0;
        const QRect r = visualRect(idx);
        return idx.row() + (pos.y() > r.center().y() ? 1 : 0);
    }

protected:
    // Up and Down are the application's pitch nudge shortcuts; in the track list they move the highlighted track instead.
    bool event(QEvent* e) override
    {
        if (e->type() == QEvent::ShortcutOverride) {
            auto* k = static_cast<QKeyEvent*>(e);
            if (playerWantsKey && state() != EditingState && playerWantsKey(k)) { e->accept(); return true; }
            if ((k->key() == Qt::Key_Up || k->key() == Qt::Key_Down) && !(k->modifiers() & ~Qt::KeypadModifier)) { e->accept(); return true; }
        }
        return QTreeView::event(e);
    }
    void keyPressEvent(QKeyEvent* e) override
    {
        if (onEnter && state() != EditingState && !(e->modifiers() & ~Qt::KeypadModifier) && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
            onEnter();
            e->accept();
            return;
        }
        if (onPlayerKey && state() != EditingState && onPlayerKey(e))
            return;
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
