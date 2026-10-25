#ifndef PENDRIVE_REGISTRY_H
#define PENDRIVE_REGISTRY_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace vdj {

class ConfigManager;

// A named folder with a rekordbox export (PIONEER/rekordbox/export.pdb) that vdj-ui can open in the browser or export to the decks.
struct VirtualPendrive {
    QString name;
    QString folder;
};

// A pendrive mounted in this PC (under /media/<user>).
struct PhysicalPendrive {
    QString name;                  // the mount point's folder name (the volume label)
    QString mountPoint;
    bool    hasRekordbox = false;  // has PIONEER/rekordbox/export.pdb
    qint64  totalBytes = 0;
    qint64  freeBytes = 0;
};

// The user's list of virtual pendrives, kept in the "virtual_pendrives" config section.
class PendriveRegistry : public QObject {
    Q_OBJECT

public:
    explicit PendriveRegistry(QObject* parent = nullptr);

    static bool isRekordboxFolder(const QString& folder);               // has PIONEER/rekordbox/export.pdb
    static QString defaultNameFor(const QString& folder);               // the folder's name, "VDJ-USB" for the root
    // The pendrives mounted under root (one folder per stick, as /media/<user> has them); newest first is not promised, sorted by name.
    static QList<PhysicalPendrive> scanPhysicalIn(const QString& root);
    static QList<PhysicalPendrive> scanPhysical();                      // /media/<username> (and /run/media/<username>)

    const QList<VirtualPendrive>& virtualPendrives() const { return list_; }
    int indexOfName(const QString& name) const;
    int indexOfFolder(const QString& folder) const;

    // Each returns an empty string on success, else why it was refused.
    QString add(const QString& name, const QString& folder);            // empty name: the folder's name
    QString rename(int index, const QString& name);
    bool remove(int index);

    // persistence
    QStringList toLines() const;                                        // "name<TAB>folder"
    void fromLines(const QStringList& lines);
    void load(ConfigManager* cfg);
    void save(ConfigManager* cfg) const;

    // Watch /media/<user> and tell when sticks come and go.
    void startWatchingPhysical(int intervalMs = 3000);
    void stopWatchingPhysical();

signals:
    void changed();                                                     // the virtual list changed
    void physicalChanged();                                             // a stick was plugged in or removed

private:
    QList<VirtualPendrive> list_;
    QTimer watch_;
    QStringList lastPhysical_;
};

} // namespace vdj

#endif // PENDRIVE_REGISTRY_H
