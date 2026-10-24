#ifndef RB_MODE_H
#define RB_MODE_H

#include <QObject>
#include <memory>

#include "db_server.h"
#include "nfs_observer.h"
#include "rb_library.h"
#include "rb_mode_options.h"
#include "rb_presence.h"

extern "C" {
#include "nfsd.h"
}

namespace vdj {

// Owns everything of the pretend-rekordbox mode: the library, the TCP servers, the UDP presence and the NFS observer, and
// wires them together (a deck that goes silent on UDP gets its TCP connections closed). No widgets: the GUI panel and the
// tests drive it through start() / stop() and the signals.
class RbMode : public QObject {
    Q_OBJECT

public:
    explicit RbMode(QObject* parent = nullptr);
    ~RbMode() override;

    bool start(const RbModeOptions& opts, QString* error = nullptr);
    void stop();
    bool isRunning() const { return running_; }

    const db::RbLibrary& library() const { return lib_; }
    DbServer& server() { return server_; }
    RbPresence& presence() { return presence_; }
    NfsObserver& nfs() { return nfs_; }
    bool nfsServing() const { return nfsd_ != nullptr; }
    quint64 nfsBytesServed() const;
    QString localIp() const { return ip_; }
    QString interfaceName() const { return iface_; }

signals:
    void runningChanged(bool running);
    void logLine(const QString& line);

private:
    void forward(const QString& s) { emit logLine(s); }

    db::RbLibrary lib_;
    DbServer      server_;
    RbPresence    presence_;
    NfsObserver   nfs_;
    nfsd_t*       nfsd_ = nullptr;          // the file service when serveNfs is on (the observer only logs)
    QString       nfsLog_;
    bool          running_ = false;
    QString       ip_, iface_;
};

} // namespace vdj

#endif // RB_MODE_H
