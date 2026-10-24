#include "rb_mode.h"

#include <QDateTime>
#include <QFile>

#include <QDir>
#include <QHostInfo>
#include <QUdpSocket>

#include <vector>

#include "rb_netif.h"

namespace vdj {

RbMode::RbMode(QObject* parent) : QObject(parent), server_(&lib_)
{
    connect(&server_, &DbServer::logLine, this, &RbMode::forward);
    connect(&presence_, &RbPresence::logLine, this, &RbMode::forward);
    connect(&nfs_, &NfsObserver::event, this, &RbMode::forward);
    // a deck whose keep-alives stopped is gone: close its TCP connections, as a real deck does ~10 s after the last UDP
    connect(&presence_, &RbPresence::deckLost, this, [this](const QString& ip) { server_.closeClientsFor(QHostAddress(ip)); });
}

RbMode::~RbMode()
{
    stop();
}

bool RbMode::start(const RbModeOptions& o, QString* error)
{
    stop();
    auto fail = [&](const QString& m) {
        if (error) *error = m;
        stop();
        return false;
    };

    std::string err;
    if (!lib_.load(o.exportRoot.toStdString(), &err)) return fail(QString::fromStdString(err));

    RbNetIf nif;
    QString nerr;
    if (o.ipOverride.isNull()) {
        if (!pickInterface(o.iface, &nif, &nerr)) return fail(nerr);
    } else {
        nif.name = o.iface;
        nif.ip = o.ipOverride;
        nif.broadcast = o.broadcastOverride.isNull() ? QHostAddress::Broadcast : o.broadcastOverride;
        const quint8 mac[6] = {0x02, 0, 0, 0, 0, 0x11};
        std::memcpy(nif.mac, mac, 6);
    }
    ip_ = nif.ip.toString();
    iface_ = nif.name;

    if (o.checkPortFree) {
        QUdpSocket probe;
        if (!probe.bind(QHostAddress::AnyIPv4, o.bindAnnounce, QUdpSocket::DontShareAddress))
            return fail(QStringLiteral("UDP port %1 is already in use (ADJ, vdj-ui's normal connection or another program): %2")
                            .arg(o.bindAnnounce).arg(probe.errorString()));
    }

    if (!o.logDir.isEmpty()) QDir().mkpath(o.logDir);

    DbServer::Config sc;
    sc.queryPort = o.queryPort;
    sc.dbPort = o.dbPort;
    sc.deviceNumber = quint32(o.device);
    sc.unknownPolicy = o.unknownPolicy;
    sc.logDir = o.logDir;
    sc.bindAddress = o.bindAddress;
    QString serr;
    if (!server_.start(sc, &serr)) return fail(serr);

    RbPresence::Config pc;
    db::Identity& id = pc.identity;
    std::memcpy(id.mac, nif.mac, 6);
    const quint32 ip4 = nif.ip.toIPv4Address();
    id.ip[0] = quint8(ip4 >> 24); id.ip[1] = quint8(ip4 >> 16); id.ip[2] = quint8(ip4 >> 8); id.ip[3] = quint8(ip4);
    id.device = quint8(o.device);
    id.name = o.name.toStdString();
    id.computerName = (o.computerName.isEmpty() ? QHostInfo::localHostName() : o.computerName).toStdString();
    pc.broadcast = nif.broadcast;
    pc.bindAddress = o.bindAddress == QHostAddress::Any ? QHostAddress(QHostAddress::AnyIPv4) : o.bindAddress;
    pc.bindAnnounce = o.bindAnnounce; pc.bindBeat = o.bindBeat; pc.bindStatus = o.bindStatus;
    pc.destAnnounce = o.destAnnounce; pc.destStatus = o.destStatus;
    pc.announceMs = o.announceMs;
    pc.claimIntervalMs = o.claimIntervalMs; pc.keepAliveMs = o.keepAliveMs; pc.deckTimeoutMs = o.deckTimeoutMs;
    pc.media.slot = 4;
    pc.media.name = "rekordbox";
    pc.media.trackCount = quint16(qMin<size_t>(lib_.trackCount(), 65535));
    pc.media.playlistCount = quint16(qMin<size_t>(lib_.playlistCount(), 65535));
    QString perr;
    if (!presence_.start(pc, &perr)) return fail(perr);

    if (o.serveNfs) {
        // The decks read the audio over NFS from the machine they got the metadata from: serve the export folder read-only.
        // They look for a rekordbox PC's portmapper at UDP 50111 and for a player's at 111 (root); the one that cannot be bound is skipped.
        nfsLog_ = o.logDir.isEmpty() ? QString() : o.logDir + "/nfs-server.log";
        const QByteArray root = o.exportRoot.toUtf8();
        nfsd_config_t nc;
        std::memset(&nc, 0, sizeof nc);
        nc.root = root.constData();
        std::vector<QByteArray> exportNames;
        std::vector<const char*> exportPtrs;
        for (const QString& e : o.nfsExports) exportNames.push_back(e.toUtf8());
        for (const QByteArray& e : exportNames) exportPtrs.push_back(e.constData());
        exportPtrs.push_back(nullptr);
        if (!o.nfsExports.isEmpty()) nc.exports = exportPtrs.data();
        nc.mount_port = o.mountPort;
        nc.nfs_port = o.nfsPort;
        nc.export_style = o.nfsExportStyle;
        nc.portmap_ports[0] = o.altPortmapPort;
        if (o.portmapPort) nc.portmap_ports[1] = o.portmapPort;
        nc.log = [](void* ctx, const char* line) {              // runs on the service thread
            auto* self = static_cast<RbMode*>(ctx);
            const QString text = QString::fromUtf8(line);
            if (!self->nfsLog_.isEmpty()) {
                QFile f(self->nfsLog_);
                if (f.open(QIODevice::WriteOnly | QIODevice::Append))
                    f.write((QDateTime::currentDateTime().toString("hh:mm:ss.zzz") + ' ' + text + '\n').toUtf8());
            }
            QMetaObject::invokeMethod(self, [self, text]() { emit self->logLine(QStringLiteral("NFS: ") + text); }, Qt::QueuedConnection);
        };
        nc.log_ctx = this;
        char nerr[256];
        nfsd_ = nfsd_start(&nc, nerr, sizeof nerr);
        if (!nfsd_) {
            emit logLine(QStringLiteral("NFS file service not started: %1").arg(QString::fromUtf8(nerr)));
        } else {
            uint16_t pm[4];
            const int n = nfsd_portmap_ports(nfsd_, pm, 4);
            QStringList ports;
            for (int i = 0; i < n; ++i) ports << QString::number(pm[i]);
            emit logLine(QStringLiteral("NFS file service: serving %1 read-only as %6, portmap UDP %2, mountd %3, nfs %4%5")
                             .arg(o.exportRoot, ports.join(" and "))
                             .arg(nfsd_mount_port(nfsd_)).arg(nfsd_nfs_port(nfsd_))
                             .arg(nerr[0] ? QStringLiteral(" (%1)").arg(QString::fromUtf8(nerr)) : QString())
                             .arg(o.nfsExports.join(QStringLiteral(", "))));
        }
    } else if (o.observeNfs) {
        NfsObserver::Config nc;
        nc.portmapPort = o.portmapPort;
        nc.mountPort = o.mountPort;
        nc.nfsPort = o.nfsPort;
        nc.bindAddress = o.bindAddress;
        if (!o.logDir.isEmpty()) nc.logFile = o.logDir + "/nfs-observer.log";
        QString warn;
        nfs_.start(nc, &warn);          // not fatal: the observer is optional
    }

    running_ = true;
    emit logLine(QStringLiteral("rekordbox mode: %1 tracks from %2, %3 (%4) as device %5 \"%6\"")
                     .arg(lib_.trackCount()).arg(o.exportRoot, ip_, iface_).arg(o.device).arg(o.name));
    emit runningChanged(true);
    return true;
}

void RbMode::stop()
{
    const bool was = running_;
    if (nfsd_) { nfsd_stop(nfsd_); nfsd_ = nullptr; }
    nfs_.stop();
    presence_.stop();
    server_.stop();
    running_ = false;
    if (was) {
        emit logLine(QStringLiteral("rekordbox mode stopped"));
        emit runningChanged(false);
    }
}

quint64 RbMode::nfsBytesServed() const
{
    return nfsd_ ? quint64(nfsd_bytes_read(nfsd_)) : 0;
}

} // namespace vdj
