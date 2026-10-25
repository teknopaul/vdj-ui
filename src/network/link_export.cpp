#include "link_export.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QMetaObject>
#include <QStorageInfo>
#include <QTimer>
#include <cstring>

namespace vdj {

LinkExport::LinkExport(QObject* parent) : QObject(parent) {}

LinkExport::~LinkExport()
{
    stop();
}

db::MediaInfo LinkExport::mediaInfo() const
{
    db::MediaInfo m;
    const QStorageInfo st(cfg_.folder);
    m.slot = 3;
    m.name = displayName().toStdString();
    m.trackCount = quint16(qMin<size_t>(lib_.trackCount(), 65535));
    m.playlistCount = quint16(qMin<size_t>(lib_.playlistCount(), 65535));
    m.fromPlayer = true;
    m.date = QFileInfo(cfg_.folder + QStringLiteral("/PIONEER/rekordbox/export.pdb")).lastModified().toString(QStringLiteral("yyyy-MM-dd")).toStdString();
    m.version = "1000";
    if (st.isValid()) {
        m.totalBytes = quint64(st.bytesTotal());
        m.freeBytes = quint64(st.bytesFree());
    }
    return m;
}

bool LinkExport::startNfs()
{
    stopNfs();
    const auto& cfg = cfg_;
    // the files: the players ask a player's portmapper at UDP 111 (needs root or setcap) and a computer's at 50111
    nfsLogPath_ = cfg.logDir.isEmpty() ? QString() : cfg.logDir + QStringLiteral("/link-nfs.log");
    const QByteArray root = cfg.folder.toUtf8();
    const char* exports[] = { "/C/", nullptr };
    nfsd_config_t nc;
    std::memset(&nc, 0, sizeof nc);
    nc.root = root.constData();
    nc.exports = exports;
    nc.portmap_ports[0] = cfg.portmapPort;
    if (cfg.altPortmapPort) nc.portmap_ports[1] = cfg.altPortmapPort;
    nc.mount_port = cfg.mountPort;
    nc.nfs_port = cfg.nfsPort;
    nc.log = [](void* ctx, const char* line) {                   // runs on the service thread
        auto* self = static_cast<LinkExport*>(ctx);
        const QString text = QString::fromUtf8(line);
        if (!self->nfsLogPath_.isEmpty()) {
            QFile f(self->nfsLogPath_);
            if (f.open(QIODevice::WriteOnly | QIODevice::Append))
                f.write((QDateTime::currentDateTime().toString("hh:mm:ss.zzz") + ' ' + text + '\n').toUtf8());
        }
        QMetaObject::invokeMethod(self, [self, text]() { emit self->logLine(QStringLiteral("NFS: ") + text); }, Qt::QueuedConnection);
    };
    nc.log_ctx = this;
    char nerr[256];
    nfsd_ = nfsd_start(&nc, nerr, sizeof nerr);
    if (!nfsd_) {
        emit logLine(QStringLiteral("LINK export: the file service did not start: %1 (the metadata is still served)").arg(QString::fromUtf8(nerr)));
    } else {
        uint16_t pm[4];
        const int n = nfsd_portmap_ports(nfsd_, pm, 4);
        QStringList ports;
        bool has111 = false;
        for (int i = 0; i < n; ++i) { ports << QString::number(pm[i]); has111 |= pm[i] == 111; }
        emit logLine(QStringLiteral("LINK export: files served read-only as /C/, portmap UDP %1, mountd %2, nfs %3")
                         .arg(ports.join(" and ")).arg(nfsd_mount_port(nfsd_)).arg(nfsd_nfs_port(nfsd_)));
        if (!has111) {
            emit portmapNotBound();
            emit logLine(QStringLiteral("LINK export: portmap UDP 111 is not bound, the players ask a player there: run "
                                        "'sudo setcap cap_net_bind_service=+ep <vdj-ui binary>' once (%1)").arg(QString::fromUtf8(nerr)));
        }
    }

    return nfsd_ != nullptr;
}

void LinkExport::stopNfs()
{
    if (nfsd_) { nfsd_stop(nfsd_); nfsd_ = nullptr; }
}

bool LinkExport::start(const Config& cfg, QString* error)
{
    stop();
    cfg_ = cfg;
    name_ = cfg.displayName;
    usbState_ = UsbState::Ready;
    mediaPresent_ = true;
    transitioning_ = false;
    pendingSwitch_ = false;
    auto fail = [&](const QString& m) {
        if (error) *error = m;
        stop();
        return false;
    };
    if (cfg.deviceNumber == 0) return fail(QStringLiteral("no player number yet"));

    std::string err;
    if (!lib_.load(cfg.folder.toStdString(), &err)) return fail(QString::fromStdString(err));

    server_ = std::make_unique<DbServer>(&lib_);
    connect(server_.get(), &DbServer::logLine, this, &LinkExport::logLine);
    DbServer::Config sc;
    sc.queryPort = cfg.queryPort;
    sc.dbPort = cfg.dbPort;
    sc.deviceNumber = cfg.deviceNumber;
    sc.logDir = cfg.logDir;
    QString serr;
    if (!server_->start(sc, &serr)) return fail(serr);

    startNfs();

    running_ = true;
    emit usbSlotChanged(quint8(usbState_), mediaPresent_);
    emit mountChanged(cfg.folder, displayName(), true);
    emit logLine(QStringLiteral("LINK export: %1 tracks from %2 as player %3, USB slot 3")
                     .arg(lib_.trackCount()).arg(cfg.folder).arg(cfg.deviceNumber));
    return true;
}

void LinkExport::stop()
{
    stopNfs();
    pendingSwitch_ = false;
    transitioning_ = false;
    if (server_) { server_->stop(); server_.reset(); }
    reply_.close();
    running_ = false;
}

void LinkExport::onStatusPortPacket(const QByteArray& packet)
{
    // called on the library's update thread: answer on ours
    QMetaObject::invokeMethod(this, "handlePacket", Qt::QueuedConnection, Q_ARG(QByteArray, packet));
}

void LinkExport::handlePacket(const QByteArray& packet)
{
    if (!running_) return;
    const auto* p = reinterpret_cast<const uint8_t*>(packet.constData());
    db::MediaQuery q;
    if (!db::parseMediaQuery(p, size_t(packet.size()), &q)) return;
    const bool ours = q.targetDevice == cfg_.deviceNumber && q.slot == 3;
    const QString ip = QHostAddress(quint32(q.askerIp[0] << 24 | q.askerIp[1] << 16 | q.askerIp[2] << 8 | q.askerIp[3])).toString();
    emit logLine(QStringLiteral("media query from %1 (player %2) for player %3 slot %4: %5")
                     .arg(ip).arg(q.askerDevice).arg(q.targetDevice).arg(q.slot).arg(ours ? "answering" : "not ours"));
    emit mediaQueried(ip, q.slot, ours);
    if (!ours) return;
    if (usbState_ != UsbState::Ready) {          // GUESS: a real deck's answer with the slot empty or unloading was not captured
        emit logLine(QStringLiteral("media query ignored: the slot is not ready"));
        return;
    }

    db::Identity id = db::Identity::deck();
    id.device = cfg_.deviceNumber;
    id.name = cfg_.modelName.toStdString();
    std::memcpy(id.mac, cfg_.mac, 6);
    std::memcpy(id.ip, cfg_.ip, 4);
    const auto resp = db::mediaResponse(id, mediaInfo());
    reply_.writeDatagram(reinterpret_cast<const char*>(resp.data()), qint64(resp.size()), QHostAddress(ip), cfg_.replyToPort);
}

QString LinkExport::displayName() const
{
    if (!name_.isEmpty()) return name_;
    const QStorageInfo st(cfg_.folder);
    const QString vol = st.isValid() ? st.name() : QString();
    if (!vol.isEmpty()) return vol;
    const QString base = QFileInfo(cfg_.folder).fileName();
    return base.isEmpty() ? QStringLiteral("VDJ-USB") : base;
}

void LinkExport::setUsb(UsbState s, bool media)
{
    usbState_ = s;
    mediaPresent_ = media;
    emit usbSlotChanged(quint8(s), media);
}

QString LinkExport::busyReason()
{
    if (nfsd_) {
        const quint64 now = nfsBytesServed();
        const qint64 t = QDateTime::currentMSecsSinceEpoch();
        if (now != lastNfsBytes_) { lastNfsBytes_ = now; lastNfsMs_ = t; }
        if (lastNfsMs_ && t - lastNfsMs_ < cfg_.busyWindowMs) return QStringLiteral("a player is reading files from it");
    }
    if (usageProbe_) {
        const QString why = usageProbe_();
        if (!why.isEmpty()) return why;
    }
    return QString();
}

QString LinkExport::requestUnmount()
{
    if (!running_) return QStringLiteral("the LINK export is not running");
    if (transitioning_ || usbState_ != UsbState::Ready) return QStringLiteral("the pendrive is already being unmounted or is empty");
    const QString why = busyReason();
    transitioning_ = true;
    setUsb(UsbState::Unloading, true);
    if (!why.isEmpty()) {                                           // refused: 02 for about a second, then back to 00, nothing else changes
        emit logLine(QStringLiteral("USB STOP refused: %1").arg(why));
        QTimer::singleShot(cfg_.refusedMs, this, [this]() {
            if (!running_) return;
            transitioning_ = false;
            setUsb(UsbState::Ready, true);
        });
        emit switchRefused(why);
        return why;
    }
    emit logLine(QStringLiteral("USB STOP: unmounting %1").arg(displayName()));
    QTimer::singleShot(cfg_.unloadingMs, this, [this]() {
        if (!running_) return;
        setUsb(UsbState::Removable, true);
        QTimer::singleShot(cfg_.removableMs, this, [this]() { finishUnmount(); });
    });
    return QString();
}

void LinkExport::finishUnmount()
{
    if (!running_) return;
    stopNfs();                                                       // GUESS: the dbserver sessions are left to the decks to drop
    setUsb(UsbState::Empty, false);
    transitioning_ = false;
    emit mountChanged(cfg_.folder, displayName(), false);
    emit logLine(QStringLiteral("USB slot empty"));
    if (pendingSwitch_) QTimer::singleShot(cfg_.swapGapMs, this, [this]() { insertPending(); });
}

void LinkExport::insertPending()
{
    if (!running_ || !pendingSwitch_) return;
    pendingSwitch_ = false;
    std::string err;
    if (!lib_.load(pendingFolder_.toStdString(), &err)) {
        emit logLine(QStringLiteral("USB insert failed: %1; the slot stays empty").arg(QString::fromStdString(err)));
        emit switchRefused(QString::fromStdString(err));
        return;
    }
    cfg_.folder = pendingFolder_;
    name_ = pendingName_;
    transitioning_ = true;
    setUsb(UsbState::Empty, true);                                   // media present first, ready about a second later
    emit logLine(QStringLiteral("USB inserted: %1 (%2 tracks)").arg(displayName()).arg(lib_.trackCount()));
    QTimer::singleShot(cfg_.insertMs, this, [this]() {
        if (!running_) return;
        startNfs();
        transitioning_ = false;
        setUsb(UsbState::Ready, true);
        emit mountChanged(cfg_.folder, displayName(), true);
    });
}

QString LinkExport::switchTo(const QString& folder, const QString& displayName)
{
    if (!running_) return QStringLiteral("the LINK export is not running");
    if (!QFileInfo(folder + QStringLiteral("/PIONEER/rekordbox/export.pdb")).isFile())
        return QStringLiteral("%1 has no PIONEER/rekordbox/export.pdb").arg(folder);
    if (pendingSwitch_ || transitioning_) return QStringLiteral("a switch is already under way");
    pendingFolder_ = folder;
    pendingName_ = displayName;
    if (usbState_ == UsbState::Empty) {
        pendingSwitch_ = true;
        insertPending();
        return QString();
    }
    pendingSwitch_ = true;
    const QString why = requestUnmount();
    if (!why.isEmpty()) pendingSwitch_ = false;
    return why;
}

void LinkExport::forceEmpty(const QString& why)
{
    if (!running_) return;
    pendingSwitch_ = false;
    transitioning_ = false;
    stopNfs();
    setUsb(UsbState::Empty, false);
    emit mountChanged(cfg_.folder, displayName(), false);
    emit logLine(QStringLiteral("USB slot emptied: %1").arg(why));
}

} // namespace vdj
