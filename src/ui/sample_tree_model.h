#ifndef SAMPLE_TREE_MODEL_H
#define SAMPLE_TREE_MODEL_H

#include <QFileSystemModel>

namespace vdj {

// The DJ Tools's sample folder as a tree: sub folders and .wav files (any case), nothing else.
class SampleTreeModel : public QFileSystemModel {
    Q_OBJECT

public:
    explicit SampleTreeModel(QObject* parent = nullptr);

    // Show `folder` (and below). Returns the index of the root, to give to QTreeView::setRootIndex. Empty folder: shows nothing.
    QModelIndex setRootFolder(const QString& folder);
    QString rootFolder() const { return rootPath_; }

    QModelIndex indexForPath(const QString& path) const;
    bool isSample(const QModelIndex& index) const;           // a .wav file
    QString samplePath(const QModelIndex& index) const;      // the file's path, empty for a folder

private:
    QString rootPath_;
};

} // namespace vdj

#endif // SAMPLE_TREE_MODEL_H
