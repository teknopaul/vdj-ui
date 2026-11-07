#include "dir_usage.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace vdj {

DirUsage measureRekordbox(const QString& base, const std::atomic<bool>* cancel)
{
    DirUsage u;
    int seen = 0;
    for (const char* sub : {"PIONEER", "Contents"}) {
        const QString dir = QDir(base).filePath(QLatin1String(sub));
        if (!QFileInfo(dir).isDir()) continue;
        u.ok = true;
        const bool contents = QLatin1String(sub) == QLatin1String("Contents");
        QDirIterator it(dir, QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            u.bytes += it.fileInfo().size();
            if (contents) ++u.audioFiles;
            if (cancel && (++seen & 255) == 0 && cancel->load()) return u;
        }
    }
    return u;
}

} // namespace vdj
