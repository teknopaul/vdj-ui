#ifndef NFS_MOUNT_MANAGER_H
#define NFS_MOUNT_MANAGER_H

#include <QObject>
#include <QHash>
#include <QList>
#include <QString>

class QProcess;

namespace vdj {

struct MountEntry {
    int playerId = 0;
    QString ip;
    QString exportPath;   // e.g. "C"
    QString mountPoint;   // local dir
    bool mounted = false;
    QString lastError;
};

class NfsMountManager : public QObject {
    Q_OBJECT

public:
    explicit NfsMountManager(QObject* parent = nullptr);

    static QString mountsRoot();

    void mountPlayer(int playerId, const QString& ip,
                     const QString& exportPath = QStringLiteral("C"));
    void unmountPlayer(int playerId);
    bool isMounted(int playerId) const;
    QString mountPoint(int playerId) const;
    QString lastError(int playerId) const;
    QList<int> mountedPlayerIds() const;

signals:
    void mounted(int playerId, const QString& mountPoint);
    void unmounted(int playerId);
    void mountFailed(int playerId, const QString& error);

private:
    bool runMount(const MountEntry& entry);
    void processFinished(int playerId, int exitCode, const QString& stderrText);

    QHash<int, MountEntry> entries_;
    QHash<int, QProcess*> pending_;
};

} // namespace vdj

#endif // NFS_MOUNT_MANAGER_H