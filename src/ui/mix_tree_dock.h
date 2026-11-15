#ifndef MIX_TREE_DOCK_H
#define MIX_TREE_DOCK_H

#include "trakcrate/library.h"

#include <QString>
#include <QWidget>

#include <functional>

class QTreeWidget;
class QTreeWidgetItem;

namespace vdj {

// The mixes/ folder of the library as a tree: double click opens, the context menu renames (the file), deletes and makes folders. Nothing outside mixes/
// is ever touched.
class MixTreeDock : public QWidget {
    Q_OBJECT

public:
    using LibraryProvider = std::function<const trakcrate::Library*()>;
    // The questions the context menu asks: injectable so tests need no dialogs. Defaults are QInputDialog / QMessageBox.
    using AskText = std::function<bool(const QString& title, const QString& label, const QString& initial, QString* out)>;
    using AskYesNo = std::function<bool(const QString& title, const QString& text)>;

    explicit MixTreeDock(LibraryProvider provider, QWidget* parent = nullptr);

    void setAskText(AskText f) { askText_ = std::move(f); }
    void setAskYesNo(AskYesNo f) { askYesNo_ = std::move(f); }
    QTreeWidget* tree() const { return tree_; }

    void refresh();
    // The actions of the context menu. Each returns false (and says why through status()) when nothing was done.
    bool renameMix(const QString& path, const QString& newName);        // newName without ".mix"
    bool deleteEntry(const QString& path);                              // a mix or an EMPTY folder; asks first
    bool newFolder(const QString& parentPath, const QString& name);     // parentPath: a folder in mixes/ (empty = mixes/ itself)

signals:
    void openRequested(const QString& path);
    void renamed(const QString& oldPath, const QString& newPath);
    void deleted(const QString& path);
    void status(const QString& message);

private:
    bool insideMixes(const QString& path) const;
    void contextMenu(const QPoint& pos);

    LibraryProvider provider_;
    QTreeWidget* tree_;
    AskText askText_;
    AskYesNo askYesNo_;
};

} // namespace vdj

#endif
