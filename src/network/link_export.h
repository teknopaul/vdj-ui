#ifndef LINK_EXPORT_H
#define LINK_EXPORT_H

#include <QObject>
#include <QString>
#include <QUdpSocket>
#include <functional>
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
        QString  displayName;             // a label for the UI and logs; empty = the volume name (decks do not show stick names)
        QString  logDir;
        // overrides for tests
        quint16  queryPort = 12523, dbPort = 1051;
        quint16  altPortmapPort = 50111, portmapPort = 111, mountPort = 50112, nfsPort = 2049;
        quint16  replyToPort = 50002;     // where media responses go on the asker
        // the USB STOP / swap sequence as a real deck does it (captures pcaps/prolink-usb-extracts.pcap, references/adj/doc/usb-switching-protocol.md)
        int      unloadingMs = 1500;      // status byte 0x6f 02 -> 03
        int      removableMs = 200;       // 03 -> 04 (empty)
        int      refusedMs = 800;         // a refused STOP shows 02 for 0.6 - 1.4 s, then goes back to 00
        int      swapGapMs = 2000;        // virtual time to "pull the stick and insert the next one"
        int      insertMs = 1100;         // media present (0x77) -> ready (0x6f 00)
        int      busyWindowMs = 3000;     // NFS reads this recent count as "in use"
    };

    // What the USB slot reports in our status packets (byte 0x6f, plus 0x77 = media present).
    enum class UsbState : quint8 { Ready = 0x00, Unloading = 0x02, Removable = 0x03, Empty = 0x04 };

    bool start(const Config& cfg, QString* error = nullptr);
    void stop();
    bool running() const { return running_; }
    const db::RbLibrary& library() const { return lib_; }
    DbServer* server() { return server_.get(); }
    quint16 nfsMountPort() const { return nfsd_ ? nfsd_mount_port(nfsd_) : 0; }
    quint64 nfsBytesServed() const { return nfsd_ ? quint64(nfsd_bytes_read(nfsd_)) : 0; }
    db::MediaInfo mediaInfo() const;                       // what a media query for slot 3 gets

    // ---- switching the exported pendrive at runtime ----
    UsbState usbState() const { return usbState_; }
    bool mediaPresent() const { return mediaPresent_; }
    QString folder() const { return cfg_.folder; }
    QString displayName() const;
    // Something that says why the pendrive is in use (a remote deck has a track loaded from it), or an empty string; may be called on the GUI thread only.
    void setUsageProbe(std::function<QString()> probe) { usageProbe_ = std::move(probe); }
    // Why the pendrive cannot be unmounted right now (a remote deck is reading or has a track loaded from it), or an empty string.
    QString busyReason();
    // USB STOP: when the pendrive is idle go ready -> unloading -> removable -> empty and return an empty string; when it is in use
    // show "unloading" for refusedMs, go back to ready and return the reason.
    QString requestUnmount();
    // Swap to another folder like pulling the stick and inserting another: STOP sequence, then after swapGapMs the new folder is loaded
    // and mounted. Returns the reason when refused (in use) or when the folder is not a rekordbox export; empty when under way.
    // From the empty state the folder is inserted at once.
    QString switchTo(const QString& folder, const QString& displayName = QString());
    // Cancel a pending switch and go to empty without a gap (used when the folder disappears under us).
    void forceEmpty(const QString& why);

    // A packet that arrived on the status port and is not a status (any thread): answers a media query for our USB slot.
    void onStatusPortPacket(const QByteArray& packet);

signals:
    // the values for the status packets (vdj_set_usb_slot): 0x6f and 0x77
    void usbSlotChanged(quint8 state, bool mediaPresent);
    void mountChanged(const QString& folder, const QString& displayName, bool mounted);
    void switchRefused(const QString& reason);
    void logLine(const QString& line);
    void mediaQueried(const QString& ip, quint32 slot, bool answered);
    void portmapNotBound();                 // UDP 111 could not be bound: the players cannot find our file service, nothing will load

private slots:
    void handlePacket(const QByteArray& packet);

private:
    void setUsb(UsbState s, bool media);
    bool startNfs();
    void stopNfs();
    void finishUnmount();
    void insertPending();
    Config cfg_;
    QString name_;
    UsbState usbState_ = UsbState::Ready;
    bool mediaPresent_ = true;
    bool transitioning_ = false;
    QString pendingFolder_, pendingName_;
    bool pendingSwitch_ = false;
    quint64 lastNfsBytes_ = 0;
    qint64 lastNfsMs_ = 0;
    std::function<QString()> usageProbe_;
    db::RbLibrary lib_;
    std::unique_ptr<DbServer> server_;
    nfsd_t* nfsd_ = nullptr;
    QUdpSocket reply_;                                     // media responses leave from an ephemeral port, as the deck's do
    QString nfsLogPath_;
    bool running_ = false;
};

} // namespace vdj

#endif // LINK_EXPORT_H
