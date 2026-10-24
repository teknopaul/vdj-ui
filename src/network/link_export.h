#ifndef LINK_EXPORT_H
#define LINK_EXPORT_H

#include <QObject>
#include <QString>
#include <QUdpSocket>
#include <memory>

#include "dbserver/db_server.h"
#include "dbserver/rb_library.h"
#include "dbserver/rb_packets.h"

extern "C" {
#include "nfsd.h"
}

namespace vdj {

// Our virtual player serving a pendrive (or a rekordbox export folder) to the other players the way a real deck serves its USB stick
// in LINK mode (captures pcaps/prolink-deck-to-deck-transfer.pcap):
//   - a media query for our USB slot (slot 3) is answered with the stick's description (type 0x06, 192 bytes),
//   - the asker then opens TCP 12523 / 1051: the dbserver answers its metadata requests from the folder's export.pdb and ANLZ files,
//   - the files are read over NFS: portmap (UDP 111 and 50111), mountd 50112, nfs 2049, export "/C/".
// Off by default; NetworkManager starts it after the join gave us a player number and stops it with the connection.
class LinkExport : public QObject {
    Q_OBJECT

public:
    explicit LinkExport(QObject* parent = nullptr);
    ~LinkExport() override;

    struct Config {
        QString  folder;                  // the folder with PIONEER/ (and Contents/)
        quint8   deviceNumber = 0;        // our player number
        quint8   mac[6] = {0, 0, 0, 0, 0, 0};
        quint8   ip[4] = {0, 0, 0, 0};
        QString  modelName = QStringLiteral("XDJ-1000");     // what we announce
        QString  logDir;
        // overrides for tests
        quint16  queryPort = 12523, dbPort = 1051;
        quint16  altPortmapPort = 50111, portmapPort = 111, mountPort = 50112, nfsPort = 2049;
        quint16  replyToPort = 50002;     // where media responses go on the asker
    };

    bool start(const Config& cfg, QString* error = nullptr);
    void stop();
    bool running() const { return running_; }
    const db::RbLibrary& library() const { return lib_; }
    DbServer* server() { return server_.get(); }
    quint16 nfsMountPort() const { return nfsd_ ? nfsd_mount_port(nfsd_) : 0; }
    quint64 nfsBytesServed() const { return nfsd_ ? quint64(nfsd_bytes_read(nfsd_)) : 0; }
    db::MediaInfo mediaInfo() const;                       // what a media query for slot 3 gets

    // A packet that arrived on the status port and is not a status (any thread): answers a media query for our USB slot.
    void onStatusPortPacket(const QByteArray& packet);

signals:
    void logLine(const QString& line);
    void mediaQueried(const QString& ip, quint32 slot, bool answered);
    void portmapNotBound();                 // UDP 111 could not be bound: the players cannot find our file service, nothing will load

private slots:
    void handlePacket(const QByteArray& packet);

private:
    Config cfg_;
    db::RbLibrary lib_;
    std::unique_ptr<DbServer> server_;
    nfsd_t* nfsd_ = nullptr;
    QUdpSocket reply_;                                     // media responses leave from an ephemeral port, as the deck's do
    QString nfsLogPath_;
    bool running_ = false;
};

} // namespace vdj

#endif // LINK_EXPORT_H
