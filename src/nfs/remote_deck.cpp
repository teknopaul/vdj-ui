#include "remote_deck.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QProgressDialog>
#include <QStandardPaths>

namespace vdj {

RemoteDeck::RemoteDeck(int playerId, const QString& ip)
    : playerId_(playerId), ip_(ip), nfs_(ip)
{
}

QString RemoteDeck::cacheRoot()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.cache/vdj-ui");
    return base + QStringLiteral("/xdj");
}

bool RemoteDeck::connect(QString* error)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = nfs_.connect(&lastError_);
    QApplication::restoreOverrideCursor();
    if (!ok && error) *error = lastError_;
    return ok;
}

bool RemoteDeck::open(int exportIndex, QString* error)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = nfs_.mountExport(exportIndex, &lastError_);
    QApplication::restoreOverrideCursor();
    if (ok) {
        const QString exportName = nfs_.exports().value(exportIndex);
        QString tag = exportName;
        tag.remove(QLatin1Char('/'));
        root_ = cacheRoot() + QStringLiteral("/p%1_%2_%3").arg(playerId_).arg(ip_, tag);
        QDir().mkpath(root_ + QStringLiteral("/Contents"));   // the track browser wants a Contents folder
        verified_.clear();
        // always refresh the database: the stick may have been exported again since last time
        ok = fetch(QStringLiteral("/PIONEER/rekordbox/export.pdb"), true);
    }
    if (!ok && error) *error = lastError_;
    return ok;
}

bool RemoteDeck::refresh(QString* error)
{
    if (root_.isEmpty())
        return false;
    QDir(root_ + QStringLiteral("/PIONEER/USBANLZ")).removeRecursively();
    verified_.clear();
    const bool ok = fetch(QStringLiteral("/PIONEER/rekordbox/export.pdb"), true);
    if (!ok && error) *error = lastError_ == QLatin1String("cancelled") ? QString() : lastError_;
    return ok;
}

bool RemoteDeck::fetch(const QString& relPath, bool showProgress)
{
    QString rel = relPath;
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);
    const QString local = root_ + QLatin1Char('/') + rel;

    QProgressDialog* dlg = nullptr;
    XdjNfs::Progress cb;
    if (showProgress) {
        dlg = new QProgressDialog(QStringLiteral("Reading %1 from deck %2...").arg(QFileInfo(rel).fileName()).arg(playerId_),
                                  QStringLiteral("Cancel"), 0, 100);
        dlg->setWindowModality(Qt::ApplicationModal);
        dlg->setMinimumDuration(300);
        cb = [dlg](quint32 done, quint32 total) {
            dlg->setMaximum(int(total / 1024) + 1);
            dlg->setValue(int(done / 1024));
            QApplication::processEvents();
            return !dlg->wasCanceled();
        };
    }
    const bool ok = nfs_.fetchFile(rel, local, &lastError_, cb);
    delete dlg;
    return ok;
}

bool RemoteDeck::ensure(const QString& relPath)
{
    if (root_.isEmpty())
        return false;
    QString rel = relPath;
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);
    if (verified_.contains(rel))
        return true;
    const QString local = root_ + QLatin1Char('/') + rel;
    const qint64 remoteSize = nfs_.fileSize(rel, &lastError_);
    if (remoteSize < 0)
        return false;
    if (QFileInfo(local).size() != remoteSize || !QFileInfo::exists(local)) {
        if (!fetch(rel, remoteSize > 256 * 1024))
            return false;
    }
    verified_.insert(rel);
    return true;
}

bool RemoteDeck::ensureAnalysis(const QString& analyzePath)
{
    if (!ensure(analyzePath))
        return false;
    const int dot = analyzePath.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) {
        const QString stem = analyzePath.left(dot);
        const QString saved = lastError_;
        ensure(stem + QStringLiteral(".EXT"));
        ensure(stem + QStringLiteral(".2EX"));
        lastError_ = saved;
    }
    return true;
}

} // namespace vdj
