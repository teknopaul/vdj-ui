#include "sample_tree_model.h"

#include <QDir>
#include <QFileInfo>

namespace vdj {

SampleTreeModel::SampleTreeModel(QObject* parent)
    : QFileSystemModel(parent)
{
    setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    setNameFilters(QStringList{ QStringLiteral("*.wav") });
    setNameFilterDisables(false);                    // hide the other files instead of greying them out
    setReadOnly(true);
}

QModelIndex SampleTreeModel::setRootFolder(const QString& folder)
{
    rootPath_ = folder.isEmpty() ? QString() : QFileInfo(folder).absoluteFilePath();
    return setRootPath(rootPath_);
}

QModelIndex SampleTreeModel::indexForPath(const QString& path) const
{
    return index(path);
}

bool SampleTreeModel::isSample(const QModelIndex& i) const
{
    return i.isValid() && !isDir(i) && fileName(i).endsWith(QLatin1String(".wav"), Qt::CaseInsensitive);
}

QString SampleTreeModel::samplePath(const QModelIndex& i) const
{
    return isSample(i) ? filePath(i) : QString();
}

} // namespace vdj
