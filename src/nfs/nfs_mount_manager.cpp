#include "nfs_mount_manager.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

namespace vdj {

NfsMountManager::NfsMountManager(QObject* parent)
    : QObject(parent)
{
}

QString NfsMountManager::mountsRoot()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.cache/vdj-ui");
    QDir(base).mkpath(QStringLiteral("."));
    return base + QStringLiteral("/mnt");
}

bool NfsMountManager::isMounted(int playerId) const
{
    return entries_.contains(playerId) && entries_.value(playerId).mounted;
}

QString NfsMountManager::mountPoint(int playerId) const
{
    return entries_.contains(playerId) ? entries_.value(playerId).mountPoint : QString();
}

QString NfsMountManager::lastError(int playerId) const
{
    return entries_.contains(playerId) ? entries_.value(playerId).lastError : QString();
}

QList<int> NfsMountManager::mountedPlayerIds() const
{
    QList<int> result;
    auto it = entries_.constBegin();
    while (it != entries_.constEnd()) {
        if (it.value().mounted)
            result.append(it.key());
        ++it;
    }
    return result;
}

void NfsMountManager::mountPlayer(int playerId, const QString& ip,
                                  const QString& exportPath)
{
    if (ip.isEmpty()) {
        emit mountFailed(playerId, QStringLiteral("No IP address for player"));
        return;
    }

    MountEntry entry;
    entry.playerId = playerId;
    entry.ip = ip;
    entry.exportPath = exportPath.isEmpty() ? QStringLiteral("C") : exportPath;
    entry.mountPoint = mountsRoot() + QStringLiteral("/p%1_%2").arg(playerId).arg(ip);
    QDir().mkpath(entry.mountPoint);
    entries_.insert(playerId, entry);

    if (!runMount(entry)) {
        emit mountFailed(playerId, entry.lastError);
    }
}

void NfsMountManager::unmountPlayer(int playerId)
{
    if (!entries_.contains(playerId))
        return;
    MountEntry entry = entries_.value(playerId);
    if (!entry.mounted)
        return;

    QProcess proc;
    proc.start(QStringLiteral("umount"), QStringList() << entry.mountPoint);
    proc.waitForFinished(5000);
    entry.mounted = false;
    entries_.insert(playerId, entry);
    emit unmounted(playerId);
}

bool NfsMountManager::runMount(const MountEntry& entry)
{
    QString src = entry.ip + QStringLiteral(":/") + entry.exportPath;

    QStringList args;
    args << QStringLiteral("-t") << QStringLiteral("nfs");
    args << QStringLiteral("-o")
         << QStringLiteral("ro,nolock,vers=3,tcp,timeo=20,retrans=2");
    args << src << entry.mountPoint;

    QProcess* prior = pending_.take(entry.playerId);
    if (prior) {
        disconnect(prior, nullptr, this, nullptr);
        prior->kill();
        prior->deleteLater();
    }

    QProcess* proc = new QProcess(this);
    pending_.insert(entry.playerId, proc);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, playerId = entry.playerId](int exitCode,
                                                          QProcess::ExitStatus status) {
        Q_UNUSED(status);
        QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        if (pending_.value(playerId) == proc)
            pending_.remove(playerId);
        proc->deleteLater();
        processFinished(playerId, exitCode, err);
    });

    proc->start(QStringLiteral("mount"), args);
    proc->waitForStarted(3000);
    return true; // result delivered via processFinished
}

void NfsMountManager::processFinished(int playerId, int exitCode, const QString& stderrText)
{
    if (!entries_.contains(playerId))
        return;
    MountEntry entry = entries_.value(playerId);
    bool ok = (exitCode == 0);
    if (ok) {
        entry.mounted = true;
        entries_.insert(playerId, entry);
        emit mounted(playerId, entry.mountPoint);
    } else {
        entry.lastError = stderrText.isEmpty()
            ? QStringLiteral("mount failed (exit %1)").arg(exitCode)
            : stderrText;
        entries_.insert(playerId, entry);
        emit mountFailed(playerId, entry.lastError);
    }
}

} // namespace vdj