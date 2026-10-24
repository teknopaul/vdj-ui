#ifndef XDJ_NFS_H
#define XDJ_NFS_H

#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

namespace vdj {

// Userspace NFS client for Pioneer players. XDJ/CDJ export NFS version 2 over UDP, which the Linux kernel client
// cannot mount, so files are read through libcdj's own RPC/NFS client (compiled in from the libcdj sources).
// Blocking calls; keep them off the GUI thread or call them from a progress dialog's event loop.
class XdjNfs {
public:
    explicit XdjNfs(const QString& host);
    ~XdjNfs();
    XdjNfs(const XdjNfs&) = delete;
    XdjNfs& operator=(const XdjNfs&) = delete;

    // Ask the player which file systems it exports (typically "/C/" for the USB stick or SD card).
    bool connect(QString* error = nullptr);
    QStringList exports() const;

    // Mount one of exports() and keep the root handle for fetchFile / fileSize.
    bool mountExport(int index, QString* error = nullptr);

    // Size in bytes of a file below the export root ("PIONEER/rekordbox/export.pdb"), -1 when it does not exist.
    qint64 fileSize(const QString& relPath, QString* error = nullptr);

    // Copy a file below the export root to localPath. progress(done, total) returns false to cancel.
    using Progress = std::function<bool(quint32 done, quint32 total)>;
    bool fetchFile(const QString& relPath, const QString& localPath, QString* error = nullptr,
                   const Progress& progress = Progress());

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace vdj

#endif // XDJ_NFS_H
