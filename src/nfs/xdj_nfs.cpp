#include "xdj_nfs.h"
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <cstring>

extern "C" {
#include "rpc.h"
#include "nfs_types.h"
#include "utf16.h"
#include "mount_client.h"
#include "nfs_client.h"
#include "portmap_client.h"
}

namespace vdj {

namespace {
constexpr int kTimeoutMs = 5000;
constexpr uint32_t kReadChunk = 2048;   // what the players are known to accept
}

struct XdjNfs::Impl {
    QByteArray host;
    uint16_t mountPort = 0;
    uint16_t nfsPort = 2049;
    QList<QByteArray> rawExports;       // UTF-16LE, as the mount protocol wants them
    QStringList exportNames;
    rpc_client_t nfs{};
    bool nfsOpen = false;
    fhandle_t root{};
    bool mounted = false;

    void closeNfs()
    {
        if (nfsOpen) {
            rpc_client_destroy(&nfs);
            nfsOpen = false;
        }
        mounted = false;
    }
};

XdjNfs::XdjNfs(const QString& host)
    : d(new Impl)
{
    d->host = host.toLatin1();
}

XdjNfs::~XdjNfs()
{
    d->closeNfs();
}

static bool fail(QString* error, const QString& msg)
{
    if (error) *error = msg;
    return false;
}

bool XdjNfs::connect(QString* error)
{
    d->closeNfs();
    d->rawExports.clear();
    d->exportNames.clear();

    if (portmap_getport(d->host.constData(), MOUNT_PROG, MOUNT_VERS, &d->mountPort) != 0)
        return fail(error, QStringLiteral("%1 does not answer the portmapper (is it an XDJ/CDJ with a media stick?)")
                               .arg(QString::fromLatin1(d->host)));
    if (nfs_getport(d->host.constData(), &d->nfsPort) != 0)
        d->nfsPort = 2049;

    rpc_client_t mc;
    if (rpc_client_init(&mc, d->host.constData(), d->mountPort, MOUNT_PROG, MOUNT_VERS, kTimeoutMs) < 0)
        return fail(error, QStringLiteral("cannot reach mountd"));
    export_entry_t* list = nullptr;
    const int rc = mount_export(&mc, &list);
    rpc_client_destroy(&mc);
    if (rc < 0 || !list)
        return fail(error, QStringLiteral("the player exports no file systems"));

    for (export_entry_t* e = list; e; e = e->next) {
        char name[MNTPATHLEN + 1];
        if (utf16le_to_utf8(e->filesystem.val, e->filesystem.len, name, sizeof(name)) < 0)
            continue;
        d->rawExports.append(QByteArray(reinterpret_cast<const char*>(e->filesystem.val), int(e->filesystem.len)));
        d->exportNames.append(QString::fromUtf8(name));
    }
    mount_export_free(list);
    if (d->exportNames.isEmpty())
        return fail(error, QStringLiteral("the player exports no readable file systems"));
    return true;
}

QStringList XdjNfs::exports() const
{
    return d->exportNames;
}

bool XdjNfs::mountExport(int index, QString* error)
{
    if (index < 0 || index >= d->rawExports.size())
        return fail(error, QStringLiteral("no such export"));
    d->closeNfs();

    rpc_client_t mc;
    if (rpc_client_init(&mc, d->host.constData(), d->mountPort, MOUNT_PROG, MOUNT_VERS, kTimeoutMs) < 0)
        return fail(error, QStringLiteral("cannot reach mountd"));
    QByteArray raw = d->rawExports[index];
    dirpath_t dp = { reinterpret_cast<uint8_t*>(raw.data()), uint32_t(raw.size()) };
    fhstatus_t fhs;
    const int rc = mount_mnt(&mc, &dp, &fhs);
    rpc_client_destroy(&mc);
    if (rc < 0)
        return fail(error, QStringLiteral("mount request failed (no answer)"));
    if (fhs.status != 0)
        return fail(error, QStringLiteral("the player refused the mount of %1 (error %2%3)")
                               .arg(d->exportNames[index]).arg(fhs.status)
                               .arg(fhs.status == NFSERR_ACCES ? QStringLiteral(": check the network/IP") : QString()));
    d->root = fhs.directory;

    if (rpc_client_init(&d->nfs, d->host.constData(), d->nfsPort, NFS_PROG, NFS_VERS, kTimeoutMs) < 0)
        return fail(error, QStringLiteral("cannot reach the NFS service"));
    d->nfsOpen = true;
    d->mounted = true;
    return true;
}

qint64 XdjNfs::fileSize(const QString& relPath, QString* error)
{
    if (!d->mounted) {
        fail(error, QStringLiteral("not mounted"));
        return -1;
    }
    fhandle_t fh;
    fattr_t attr;
    const QByteArray p = relPath.toUtf8();
    const int rc = nfs_path_lookup(&d->nfs, &d->root, p.constData(), &fh, &attr);
    if (rc != 0) {
        fail(error, rc == NFSERR_NOENT ? QStringLiteral("%1 not found on the player").arg(relPath)
                                       : QStringLiteral("lookup of %1 failed (%2)").arg(relPath).arg(rc));
        return -1;
    }
    return attr.size;
}

bool XdjNfs::fetchFile(const QString& relPath, const QString& localPath, QString* error, const Progress& progress)
{
    if (!d->mounted)
        return fail(error, QStringLiteral("not mounted"));
    fhandle_t fh;
    fattr_t attr;
    const QByteArray p = relPath.toUtf8();
    int rc = nfs_path_lookup(&d->nfs, &d->root, p.constData(), &fh, &attr);
    if (rc != 0)
        return fail(error, rc == NFSERR_NOENT ? QStringLiteral("%1 not found on the player").arg(relPath)
                                              : QStringLiteral("lookup of %1 failed (%2)").arg(relPath).arg(rc));

    QDir().mkpath(QFileInfo(localPath).absolutePath());
    const QString part = localPath + QStringLiteral(".part");
    QFile out(part);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(error, QStringLiteral("cannot write %1: %2").arg(part, out.errorString()));

    const uint32_t total = attr.size;
    uint32_t offset = 0;
    while (offset < total) {
        readargs_t ra;
        ra.file = fh;
        ra.offset = offset;
        ra.count = kReadChunk;
        ra.totalcount = 0;
        readres_t rr;
        std::memset(&rr, 0, sizeof(rr));
        if (nfs_read(&d->nfs, &ra, &rr) < 0 || rr.status != NFS_OK) {
            const uint32_t st = rr.status;
            nfs_readres_free(&rr);
            out.close();
            QFile::remove(part);
            return fail(error, QStringLiteral("read of %1 failed at byte %2 (%3)").arg(relPath).arg(offset).arg(st));
        }
        if (rr.data_len == 0) {
            nfs_readres_free(&rr);
            break;
        }
        out.write(reinterpret_cast<const char*>(rr.data), qint64(rr.data_len));
        offset += rr.data_len;
        nfs_readres_free(&rr);
        if (progress && !progress(offset, total)) {
            out.close();
            QFile::remove(part);
            return fail(error, QStringLiteral("cancelled"));
        }
    }
    out.close();
    if (offset != total) {
        QFile::remove(part);
        return fail(error, QStringLiteral("%1 is incomplete (%2 of %3 bytes)").arg(relPath).arg(offset).arg(total));
    }
    QFile::remove(localPath);
    if (!QFile::rename(part, localPath))
        return fail(error, QStringLiteral("cannot create %1").arg(localPath));
    return true;
}

} // namespace vdj
