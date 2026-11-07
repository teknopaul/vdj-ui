#include "mounted_sticks.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QStorageInfo>

QList<MountedVolume> systemVolumes()
{
    QList<MountedVolume> out;
    for (const QStorageInfo& s : QStorageInfo::mountedVolumes()) {
        MountedVolume v;
        v.root = s.rootPath();
        v.name = s.displayName();
        v.ready = s.isValid() && s.isReady();
        v.readOnly = s.isReadOnly();
        v.freeBytes = s.bytesAvailable();
        out << v;
    }
    return out;
}

QList<RekordboxStick> listRekordboxSticks(const std::function<QList<MountedVolume>()>& volumes)
{
    QList<RekordboxStick> out;
    for (const MountedVolume& v : volumes()) {
        if (!v.ready || v.readOnly) continue;
        if (!QFileInfo(QDir(v.root).filePath(QStringLiteral("PIONEER/rekordbox/export.pdb"))).isFile()) continue;
        RekordboxStick s;
        s.root = v.root;
        QString name = v.name.isEmpty() ? QFileInfo(v.root).fileName() : v.name;
        if (name.isEmpty()) name = v.root;
        s.label = QStringLiteral("%1 (%2 free)").arg(name, QLocale().formattedDataSize(v.freeBytes));
        out << s;
    }
    return out;
}
