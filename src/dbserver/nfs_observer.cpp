#include "nfs_observer.h"

#include <QDateTime>
#include <QFile>
#include <QTcpSocket>

namespace vdj {

namespace {

constexpr quint32 kProgPortmap = 100000, kProgNfs = 100003, kProgMount = 100005;

struct Xdr {
    const QByteArray& b;
    int pos = 0;
    bool bad = false;
    quint32 u32()
    {
        if (pos + 4 > b.size()) { bad = true; return 0; }
        const uchar* p = reinterpret_cast<const uchar*>(b.constData()) + pos;
        pos += 4;
        return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3];
    }
    QByteArray opaque(int n)       // fixed-length, padded to 4
    {
        const int padded = (n + 3) & ~3;
        if (n < 0 || pos + padded > b.size()) { bad = true; return {}; }
        QByteArray r = b.mid(pos, n);
        pos += padded;
        return r;
    }
    QByteArray var()
    {
        const quint32 n = u32();
        if (bad || n > 1u << 20) { bad = true; return {}; }
        return opaque(int(n));
    }
};

void put32(QByteArray& o, quint32 v)
{
    o.append(char(v >> 24)); o.append(char(v >> 16)); o.append(char(v >> 8)); o.append(char(v));
}

QByteArray replyHeader(quint32 xid, quint32 acceptStat)
{
    QByteArray r;
    put32(r, xid); put32(r, 1); put32(r, 0);        // REPLY, MSG_ACCEPTED
    put32(r, 0); put32(r, 0);                       // verifier AUTH_NONE
    put32(r, acceptStat);
    return r;
}

QString progName(quint32 p)
{
    switch (p) {
    case kProgPortmap: return QStringLiteral("portmap");
    case kProgMount:   return QStringLiteral("mount");
    case kProgNfs:     return QStringLiteral("nfs");
    default:           return QStringLiteral("prog %1").arg(p);
    }
}

QString fhText(const QByteArray& fh) { return QString::fromLatin1(fh.left(8).toHex()) + (fh.size() > 8 ? "..." : ""); }

} // namespace

NfsObserver::NfsObserver(QObject* parent) : QObject(parent)
{
    connect(&portmap_, &QUdpSocket::readyRead, this, [this]() { onDatagram(&portmap_, "portmap"); });
    connect(&altPortmap_, &QUdpSocket::readyRead, this, [this]() { onDatagram(&altPortmap_, "portmap"); });
    connect(&mount_, &QUdpSocket::readyRead, this, [this]() { onDatagram(&mount_, "mount"); });
    connect(&nfs_, &QUdpSocket::readyRead, this, [this]() { onDatagram(&nfs_, "nfs"); });
    auto tcp = [this](QTcpServer* srv, const char* what) {
        connect(srv, &QTcpServer::newConnection, this, [this, srv, what]() {
            while (QTcpSocket* s = srv->nextPendingConnection()) {
                say(QStringLiteral("TCP connection to the %1 port from %2 (closed; TCP is not served)")
                        .arg(QLatin1String(what), QHostAddress(s->peerAddress().toIPv4Address()).toString()));
                s->abort();
                s->deleteLater();
            }
        });
    };
    tcp(&tcpPortmap_, "portmap");
    tcp(&tcpNfs_, "nfs");
}

NfsObserver::~NfsObserver()
{
    stop();
}

bool NfsObserver::start(const Config& cfg, QString* warnings)
{
    stop();
    cfg_ = cfg;
    QStringList warn;
    const auto mode = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
    bool any = false;
    if (portmap_.bind(cfg.bindAddress, cfg.portmapPort, mode)) {
        any = true;
        tcpPortmap_.listen(cfg.bindAddress, portmap_.localPort());
    } else {
        warn << QStringLiteral("portmap UDP %1 not bound (%2); needs root or: sudo setcap cap_net_bind_service=+ep <vdj-ui>")
                    .arg(cfg.portmapPort).arg(portmap_.errorString());
    }
    if (cfg.altPortmapPort) {
        if (altPortmap_.bind(cfg.bindAddress, cfg.altPortmapPort, mode)) any = true;
        else warn << QStringLiteral("portmap UDP %1 not bound (%2)").arg(cfg.altPortmapPort).arg(altPortmap_.errorString());
    }
    if (mount_.bind(cfg.bindAddress, cfg.mountPort, mode)) {
        any = true;
    } else if (cfg.mountPort && mount_.bind(cfg.bindAddress, 0, mode)) {      // taken: any free port, the deck is told through GETPORT
        any = true;
        warn << QStringLiteral("mount UDP %1 is in use, using %2 instead").arg(cfg.mountPort).arg(mount_.localPort());
    } else {
        warn << QStringLiteral("mount UDP %1 not bound (%2)").arg(cfg.mountPort).arg(mount_.errorString());
    }
    if (nfs_.bind(cfg.bindAddress, cfg.nfsPort, mode)) {
        any = true;
        tcpNfs_.listen(cfg.bindAddress, nfs_.localPort());
    } else {
        warn << QStringLiteral("nfs UDP %1 not bound (%2)").arg(cfg.nfsPort).arg(nfs_.errorString());
    }
    if (warnings) *warnings = warn.join("; ");
    for (const QString& w : warn) say("NFS observer: " + w);
    if (any) say(QStringLiteral("NFS observer: portmap %1 and %2, mount %3, nfs %4 (logging only, nothing is served)")
                     .arg(portmapActive() ? QString::number(portmap_.localPort()) : QStringLiteral("off"))
                     .arg(altPortmap_.state() == QAbstractSocket::BoundState ? QString::number(altPortmap_.localPort()) : QStringLiteral("off"))
                     .arg(mount_.localPort()).arg(nfs_.localPort()));
    return any;
}

void NfsObserver::stop()
{
    portmap_.close(); altPortmap_.close(); mount_.close(); nfs_.close();
    tcpPortmap_.close(); tcpNfs_.close();
}

void NfsObserver::say(const QString& line)
{
    if (!cfg_.logFile.isEmpty()) {
        QFile f(cfg_.logFile);
        if (f.open(QIODevice::WriteOnly | QIODevice::Append))
            f.write((QDateTime::currentDateTime().toString("hh:mm:ss.zzz") + ' ' + line + '\n').toUtf8());
    }
    emit event(line);
}

bool NfsObserver::handleCall(const QByteArray& call, quint16 mountPort, quint16 nfsPort, QByteArray* reply, QString* description,
                             QString* mntPath, QString* nfsName)
{
    Xdr x{call};
    const quint32 xid = x.u32();
    const quint32 mtype = x.u32();
    const quint32 rpcvers = x.u32();
    const quint32 prog = x.u32();
    const quint32 vers = x.u32();
    const quint32 proc = x.u32();
    x.u32(); x.var();           // credentials
    x.u32(); x.var();           // verifier
    if (x.bad || mtype != 0 || rpcvers != 2) return false;

    QString d = QStringLiteral("%1 v%2 proc %3").arg(progName(prog)).arg(vers).arg(proc);
    *reply = replyHeader(xid, 0);

    if (prog == kProgPortmap) {
        if (proc == 0) d += " NULL";
        else if (proc == 3) {
            const quint32 qprog = x.u32(), qvers = x.u32(), qproto = x.u32();
            quint32 port = 0;
            if (qprog == kProgMount) port = mountPort;
            else if (qprog == kProgNfs) port = nfsPort;
            d += QStringLiteral(" GETPORT %1 v%2 proto %3 -> %4").arg(progName(qprog)).arg(qvers).arg(qproto == 17 ? QStringLiteral("udp") : QStringLiteral("tcp")).arg(port);
            put32(*reply, port);
        } else if (proc == 4) { d += " DUMP -> empty"; put32(*reply, 0); }
        else { *reply = replyHeader(xid, 3); d += " (proc unavailable)"; }
    } else if (prog == kProgMount) {
        if (proc == 0) d += " NULL";
        else if (proc == 1) {
            const QString path = QString::fromUtf8(x.var());
            d += QStringLiteral(" MNT \"%1\" -> access denied").arg(path);
            if (mntPath) *mntPath = path;
            put32(*reply, 13);
        } else { *reply = replyHeader(xid, 3); d += " (proc unavailable)"; }
    } else if (prog == kProgNfs) {
        if (proc == 0) d += " NULL";
        else {
            QString what;
            switch (proc) {
            case 1: what = "GETATTR fh " + fhText(x.opaque(32)); break;
            case 4: { const QByteArray fh = x.opaque(32); const QString n = QString::fromUtf8(x.var()); what = QStringLiteral("LOOKUP \"%1\" in fh %2").arg(n, fhText(fh)); if (nfsName) *nfsName = n; break; }
            case 6: { const QByteArray fh = x.opaque(32); const quint32 off = x.u32(), cnt = x.u32(); what = QStringLiteral("READ fh %1 offset %2 count %3").arg(fhText(fh)).arg(off).arg(cnt); if (nfsName) *nfsName = what; break; }
            case 16: what = "READDIR fh " + fhText(x.opaque(32)); break;
            default: what = QStringLiteral("proc %1").arg(proc);
            }
            d += " " + what + " -> NFSERR_ACCES";
            put32(*reply, 13);
        }
    } else {
        *reply = replyHeader(xid, 1);       // PROG_UNAVAIL
        d += " (program unavailable)";
    }
    *description = d;
    return true;
}

void NfsObserver::onDatagram(QUdpSocket* s, const char* what)
{
    while (s->hasPendingDatagrams()) {
        QByteArray d;
        d.resize(int(s->pendingDatagramSize()));
        QHostAddress from;
        quint16 port = 0;
        s->readDatagram(d.data(), d.size(), &from, &port);
        ++calls_;
        QByteArray reply;
        QString desc, mnt, name;
        const QString ip = QHostAddress(from.toIPv4Address()).toString();
        if (!handleCall(d, mount_.localPort(), nfs_.localPort(), &reply, &desc, &mnt, &name)) {
            say(QStringLiteral("%1 socket: not an RPC call from %2:%3 (%4 bytes: %5)").arg(QLatin1String(what), ip).arg(port).arg(d.size()).arg(QString::fromLatin1(d.left(24).toHex(' '))));
            continue;
        }
        say(QStringLiteral("RPC from %1:%2  %3").arg(ip).arg(port).arg(desc));
        if (!mnt.isEmpty()) { mountPaths_ << mnt; emit mountRequested(ip, mnt); }
        if (!name.isEmpty()) { nfsFiles_ << name; emit nfsRequested(ip, name); }
        s->writeDatagram(reply, from, port);
    }
}

} // namespace vdj
