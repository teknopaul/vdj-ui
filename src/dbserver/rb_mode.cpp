#include "rb_mode.h"

#include <QDir>
#include <QHostInfo>
#include <QUdpSocket>

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
    pc.claimIntervalMs = o.claimIntervalMs; pc.keepAliveMs = o.keepAliveMs; pc.deckTimeoutMs = o.deckTimeoutMs;
    pc.media.slot = 4;
    pc.media.name = "rekordbox";
    pc.media.trackCount = quint16(qMin<size_t>(lib_.trackCount(), 65535));
    pc.media.playlistCount = quint16(qMin<size_t>(lib_.playlistCount(), 65535));
    QString perr;
    if (!presence_.start(pc, &perr)) return fail(perr);

    if (o.observeNfs) {
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
    nfs_.stop();
    presence_.stop();
    server_.stop();
    running_ = false;
    if (was) {
        emit logLine(QStringLiteral("rekordbox mode stopped"));
        emit runningChanged(false);
    }
}

} // namespace vdj
