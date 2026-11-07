#ifndef MOUNTED_STICKS_H
#define MOUNTED_STICKS_H

#include <QList>
#include <QString>

#include <functional>

// Mounted media that hold a rekordbox database (PIONEER/rekordbox/export.pdb): the pendrives "Add to rekordbox" can add a playlist to.
struct MountedVolume {
    QString root, name;
    bool ready = true, readOnly = false;
    qint64 freeBytes = 0;
};

struct RekordboxStick {
    QString root;                  // the mount point
    QString label;                 // what the menu shows: the volume name (or the folder name) and the free space
};

// The volumes the system reports (QStorageInfo).
QList<MountedVolume> systemVolumes();

// Ready, writable volumes that have PIONEER/rekordbox/export.pdb, in the order given. `volumes` is a parameter so that tests can use folders.
QList<RekordboxStick> listRekordboxSticks(const std::function<QList<MountedVolume>()>& volumes = systemVolumes);

#endif
