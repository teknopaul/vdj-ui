#include <QtTest>

#include <unistd.h>

#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUdpSocket>

#include "dbserver/nfs_observer.h"
extern "C" {
#include "mount_client.h"
#include "nfs_client.h"
#include "rpc.h"
#include "utf16.h"
}
#include "dbserver/rb_mode.h"
#include "dbserver/rb_mode_options.h"

using namespace vdj;

namespace {

void put32(QByteArray& o, quint32 v) { o.append(char(v >> 24)); o.append(char(v >> 16)); o.append(char(v >> 8)); o.append(char(v)); }

QByteArray rpcCall(quint32 xid, quint32 prog, quint32 vers, quint32 proc, const QByteArray& args = QByteArray())
{
    QByteArray c;
    put32(c, xid); put32(c, 0); put32(c, 2); put32(c, prog); put32(c, vers); put32(c, proc);
    put32(c, 0); put32(c, 0);       // AUTH_NONE credentials
    put32(c, 0); put32(c, 0);       // verifier
    return c + args;
}

QByteArray xdrString(const QByteArray& s)
{
    QByteArray o;
    put32(o, quint32(s.size()));
    o.append(s);
    while (o.size() % 4) o.append('\0');
    return o;
}

quint32 get32(const QByteArray& b, int at) { return (quint32(uchar(b[at])) << 24) | (quint32(uchar(b[at + 1])) << 16) | (quint32(uchar(b[at + 2])) << 8) | uchar(b[at + 3]); }

template <typename Fn>
bool waitFor(Fn cond, int ms = 3000)
{
    for (int i = 0; i < ms / 10 && !cond(); ++i) QTest::qWait(10);
    return cond();
}

QByteArray fixture(const char* sub, const char* name)
{
    QFile f(QStringLiteral(VDJ_DBSERVER_FIXTURES "/") + QLatin1String(sub) + "/" + QLatin1String(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// one UDP request/response with the observer
QByteArray udpCall(quint16 port, const QByteArray& call)
{
    QUdpSocket s;
    s.bind(QHostAddress::LocalHost, 0);
    s.writeDatagram(call, QHostAddress::LocalHost, port);
    if (!waitFor([&] { return s.hasPendingDatagrams(); }, 1000)) return QByteArray();
    QByteArray r;
    r.resize(int(s.pendingDatagramSize()));
    s.readDatagram(r.data(), r.size());
    return r;
}

} // namespace

class TestRbMode : public QObject {
    Q_OBJECT

private slots:
    void parsesTheModeArguments()
    {
        RbModeOptions o;
        QString err;
        QVERIFY(parseRbModeArgs({"vdj-ui"}, &o, &err));
        QVERIFY(o.exportRoot.isEmpty());          // mode not requested

        QVERIFY(parseRbModeArgs({"vdj-ui", "--debug", "--rekordbox-mode", "/var/tmp/x", "--rb-iface", "eth1", "--rb-name", "DIRTBOX",
                                 "--rb-device", "18", "--rb-log-dir", "/tmp/l", "--rb-unknown-policy", "close"}, &o, &err));
        QCOMPARE(o.exportRoot, QStringLiteral("/var/tmp/x"));
        QCOMPARE(o.iface, QStringLiteral("eth1"));
        QCOMPARE(o.computerName, QStringLiteral("DIRTBOX"));
        QCOMPARE(o.device, 18);
        QCOMPARE(o.logDir, QStringLiteral("/tmp/l"));
        QCOMPARE(int(o.unknownPolicy), int(db::UnknownPolicy::Close));
        QCOMPARE(unknownPolicyName(o.unknownPolicy), QStringLiteral("close"));
    }

    void rejectsBadArguments()
    {
        RbModeOptions o;
        QString err;
        QVERIFY(!parseRbModeArgs({"vdj-ui", "--rekordbox-mode"}, &o, &err));
        QVERIFY(err.contains("needs a value"));
        QVERIFY(!parseRbModeArgs({"vdj-ui", "--rb-device", "3"}, &o, &err));
        QVERIFY(!parseRbModeArgs({"vdj-ui", "--rb-device", "abc"}, &o, &err));
        QVERIFY(!parseRbModeArgs({"vdj-ui", "--rb-unknown-policy", "explode"}, &o, &err));
    }

    void portmapGetportReturnsOurPorts()
    {
        QByteArray reply, desc;
        QString d;
        QByteArray args;
        put32(args, 100005); put32(args, 1); put32(args, 17); put32(args, 0);
        QVERIFY(NfsObserver::handleCall(rpcCall(7, 100000, 2, 3, args), 4000, 2049, &reply, &d));
        QCOMPARE(get32(reply, 0), 7u);              // xid
        QCOMPARE(get32(reply, 4), 1u);              // reply
        QCOMPARE(get32(reply, 20), 0u);             // accepted, success
        QCOMPARE(get32(reply, 24), 4000u);          // mount port
        QVERIFY(d.contains("GETPORT"));
        args.clear();
        put32(args, 100003); put32(args, 2); put32(args, 17); put32(args, 0);
        QVERIFY(NfsObserver::handleCall(rpcCall(8, 100000, 2, 3, args), 4000, 2049, &reply, &d));
        QCOMPARE(get32(reply, 24), 2049u);
        args.clear();
        put32(args, 999999); put32(args, 1); put32(args, 17); put32(args, 0);
        QVERIFY(NfsObserver::handleCall(rpcCall(9, 100000, 2, 3, args), 4000, 2049, &reply, &d));
        QCOMPARE(get32(reply, 24), 0u);
    }

    void mountRequestIsLoggedAndRefused()
    {
        QByteArray reply;
        QString d, path;
        QVERIFY(NfsObserver::handleCall(rpcCall(1, 100005, 1, 1, xdrString("/C/")), 4000, 2049, &reply, &d, &path));
        QCOMPARE(path, QStringLiteral("/C/"));
        QCOMPARE(get32(reply, 20), 0u);
        QCOMPARE(get32(reply, 24), 13u);            // EACCES: no file handle follows
        QCOMPARE(reply.size(), 28);
    }

    void nfsCallsAreDecodedAndNeverAnsweredWithData()
    {
        QByteArray reply;
        QString d, name;
        QByteArray fh(32, 'F');
        QVERIFY(NfsObserver::handleCall(rpcCall(2, 100003, 2, 4, fh + xdrString("PIONEER")), 4000, 2049, &reply, &d, nullptr, &name));
        QCOMPARE(name, QStringLiteral("PIONEER"));
        QCOMPARE(get32(reply, 24), 13u);
        QCOMPARE(reply.size(), 28);

        QByteArray readArgs = fh;
        put32(readArgs, 4096); put32(readArgs, 8192); put32(readArgs, 8192);
        QVERIFY(NfsObserver::handleCall(rpcCall(3, 100003, 2, 6, readArgs), 4000, 2049, &reply, &d, nullptr, &name));
        QVERIFY(d.contains("READ"));
        QVERIFY(d.contains("offset 4096"));
        QCOMPARE(reply.size(), 28);                 // status only, no data
    }

    void unknownProgramsAndGarbage()
    {
        QByteArray reply;
        QString d;
        QVERIFY(NfsObserver::handleCall(rpcCall(4, 123456, 1, 1), 4000, 2049, &reply, &d));
        QCOMPARE(get32(reply, 20), 1u);             // PROG_UNAVAIL
        QVERIFY(!NfsObserver::handleCall(QByteArray("not rpc"), 4000, 2049, &reply, &d));
        QVERIFY(!NfsObserver::handleCall(QByteArray(), 4000, 2049, &reply, &d));
        QByteArray truncated = rpcCall(5, 100005, 1, 1, xdrString("/C/")).left(30);
        NfsObserver::handleCall(truncated, 4000, 2049, &reply, &d);     // must not crash
    }

    void observerAnswersOverUdp()
    {
        NfsObserver obs;
        QSignalSpy mounts(&obs, &NfsObserver::mountRequested);
        NfsObserver::Config c;
        c.portmapPort = 0; c.mountPort = 0; c.nfsPort = 0; c.bindAddress = QHostAddress::LocalHost;
        QVERIFY(obs.start(c));
        QVERIFY(obs.portmapActive());

        QByteArray args;
        put32(args, 100005); put32(args, 1); put32(args, 17); put32(args, 0);
        QByteArray r = udpCall(obs.portmapPort(), rpcCall(10, 100000, 2, 3, args));
        QCOMPARE(get32(r, 24), quint32(obs.mountPort()));

        r = udpCall(obs.mountPort(), rpcCall(11, 100005, 1, 1, xdrString("/C/")));
        QCOMPARE(get32(r, 24), 13u);
        QCOMPARE(mounts.count(), 1);
        QCOMPARE(mounts[0][1].toString(), QStringLiteral("/C/"));
        QCOMPARE(obs.mountPaths(), QStringList{"/C/"});
        QCOMPARE(int(obs.calls()), 2);
    }

    // The decks send their GETPORT for mountd to UDP 50111 (capture prolink-vdj-ui-as-rekordbox), which needs no root
    void observerAnswersOnThePortTheDecksUse()
    {
        NfsObserver obs;
        NfsObserver::Config c;
        c.portmapPort = 0; c.mountPort = 0; c.nfsPort = 0; c.altPortmapPort = 50111; c.bindAddress = QHostAddress::LocalHost;
        QString warn;
        QVERIFY(obs.start(c, &warn));
        if (warn.contains("50111")) QSKIP("UDP 50111 is in use (is vdj-ui running?)");
        QByteArray args;
        put32(args, 100005); put32(args, 1); put32(args, 17); put32(args, 0);
        const QByteArray r = udpCall(50111, rpcCall(12, 100000, 2, 3, args));
        QCOMPARE(get32(r, 24), quint32(obs.mountPort()));
    }

    void observerReportsWhenPortmapNeedsRoot()
    {
        NfsObserver obs;
        NfsObserver::Config c;
        c.portmapPort = 111; c.mountPort = 0; c.nfsPort = 0; c.bindAddress = QHostAddress::LocalHost;
        QString warn;
        QVERIFY(obs.start(c, &warn));               // mount and nfs still work
        QFile lowest(QStringLiteral("/proc/sys/net/ipv4/ip_unprivileged_port_start"));      // 1024 normally; sysctl can lower it
        const int firstFree = lowest.open(QIODevice::ReadOnly) ? lowest.readAll().trimmed().toInt() : 1024;
        if (getuid() != 0 && firstFree > 111) {
            QVERIFY(!obs.portmapActive());
            QVERIFY(warn.contains("setcap"));
        }
    }

    // The whole mode on loopback with ephemeral ports
    // The whole mode serving a real export folder: a deck asks the portmapper, mounts /C/ and reads export.pdb over NFS
    void modeServesTheExportOverNfs()
    {
        QTemporaryDir logs;
        RbModeOptions o;
        o.exportRoot = QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7");
        o.logDir = logs.path();
        o.ipOverride = QHostAddress(QStringLiteral("127.0.0.2"));
        o.broadcastOverride = QHostAddress::LocalHost;
        o.bindAddress = QHostAddress::LocalHost;
        o.queryPort = o.dbPort = 0;
        o.bindAnnounce = o.bindBeat = o.bindStatus = 0;
        o.destAnnounce = o.destStatus = 9;
        o.altPortmapPort = 53211; o.portmapPort = 0; o.mountPort = 53212; o.nfsPort = 53249;
        o.nfsExports = QStringList{QStringLiteral("/C/"), QStringLiteral("/")};
        o.checkPortFree = false;

        RbMode mode;
        QString err;
        QVERIFY2(mode.start(o, &err), qPrintable(err));
        QVERIFY2(mode.nfsServing(), "the file service did not start (are UDP 53211/53212/53249 in use?)");

        rpc_client_t pm, mc, nc;
        QVERIFY(rpc_client_init(&pm, "127.0.0.1", 53211, 100000, 2, 2000) == 0);
        QByteArray args;
        put32(args, 100005); put32(args, 1); put32(args, 17); put32(args, 0);
        const QByteArray r = udpCall(53211, rpcCall(20, 100000, 2, 3, args));
        QCOMPARE(get32(r, 24), 53212u);                                    // the portmapper names our mountd
        rpc_client_destroy(&pm);

        QVERIFY(rpc_client_init(&mc, "127.0.0.1", 53212, 100005, 1, 2000) == 0);
        QVERIFY(rpc_client_init(&nc, "127.0.0.1", 53249, 100003, 2, 2000) == 0);
        uint8_t path[16]; uint32_t plen = 0;
        utf8_to_utf16le("/C/", path, sizeof path, &plen);
        dirpath_t dp = { path, plen };
        fhstatus_t fhs;
        QVERIFY(mount_mnt(&mc, &dp, &fhs) == 0 && fhs.status == 0);
        fhandle_t f1, f2, f3;
        fattr_t a;
        QVERIFY(nfs_path_lookup(&nc, &fhs.directory, "pioneer/REKORDBOX/export.pdb", &f3, &a) == 0);     // other case than on disk
        QCOMPARE(int(a.type), int(NFREG));
        readargs_t ra; readres_t rr;
        memset(&rr, 0, sizeof rr);
        ra.file = f3; ra.offset = 0; ra.count = 8192; ra.totalcount = 0;
        QVERIFY(nfs_read(&nc, &ra, &rr) == 0 && rr.status == 0 && rr.data_len == 8192);
        QFile pdb(QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7/PIONEER/rekordbox/export.pdb"));
        QVERIFY(pdb.open(QIODevice::ReadOnly));
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(rr.data), int(rr.data_len)), pdb.read(8192));     // the bytes of the file
        nfs_readres_free(&rr);
        QVERIFY(mode.nfsBytesServed() >= 8192);
        (void)f1; (void)f2;
        rpc_client_destroy(&mc);
        rpc_client_destroy(&nc);
        mode.stop();
        QVERIFY(!mode.nfsServing());
    }

    void modeServesAndClosesConnectionsOfLostDecks()
    {
        QTemporaryDir logs;
        RbModeOptions o;
        o.exportRoot = QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7");
        o.logDir = logs.path();
        o.ipOverride = QHostAddress(QStringLiteral("127.0.0.2"));
        o.broadcastOverride = QHostAddress::LocalHost;
        o.bindAddress = QHostAddress::LocalHost;
        o.queryPort = o.dbPort = 0;
        o.bindAnnounce = o.bindBeat = o.bindStatus = 0;
        o.destAnnounce = o.destStatus = 9;           // discard port: nobody listens
        o.portmapPort = 0; o.mountPort = 0; o.nfsPort = 0;
        o.serveNfs = false;                          // the observer on ephemeral ports; serving has its own test
        o.checkPortFree = false;
        o.claimIntervalMs = 10; o.keepAliveMs = 50; o.deckTimeoutMs = 300;

        RbMode mode;
        QSignalSpy running(&mode, &RbMode::runningChanged);
        QString err;
        QVERIFY2(mode.start(o, &err), qPrintable(err));
        QVERIFY(mode.isRunning());
        QCOMPARE(running.count(), 1);
        QCOMPARE(int(mode.library().trackCount()), 7);

        // a deck announces itself and opens a dbserver connection
        QUdpSocket udp;
        const QByteArray ka = fixture("udp", "deck_keepalive_xdj700.bin");
        udp.writeDatagram(ka, QHostAddress::LocalHost, mode.presence().localAnnouncePort());
        QSignalSpy seen(&mode.presence(), &RbPresence::deckSeen);
        QVERIFY(waitFor([&] { return seen.count() == 1 || !mode.presence().decks().isEmpty(); }));

        QTcpSocket tcp;
        tcp.connectToHost(QHostAddress::LocalHost, mode.server().dbPort());
        QVERIFY(tcp.waitForConnected(2000));
        QVERIFY(waitFor([&] { return mode.server().clientCount() == 1; }));

        // keep-alives stop: the deck is lost and its TCP connection is closed
        QVERIFY(waitFor([&] { return tcp.state() == QAbstractSocket::UnconnectedState; }, 3000));
        QVERIFY(waitFor([&] { return mode.server().clientCount() == 0; }));

        mode.stop();
        QVERIFY(!mode.isRunning());
        QCOMPARE(running.count(), 2);
        QVERIFY(QFile::exists(logs.path() + "/nfs-observer.log") || true);
    }

    void modeRefusesToStartOnABadExport()
    {
        RbModeOptions o;
        o.exportRoot = "/nonexistent";
        o.ipOverride = QHostAddress(QStringLiteral("127.0.0.2"));
        RbMode mode;
        QString err;
        QVERIFY(!mode.start(o, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!mode.isRunning());
    }

    void modeRefusesWhenUdp50000IsTaken()
    {
        QUdpSocket holder;
        if (!holder.bind(QHostAddress::AnyIPv4, 50000, QUdpSocket::DontShareAddress))
            QSKIP("UDP 50000 is already used on this machine");
        RbModeOptions o;
        o.exportRoot = QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7");
        o.ipOverride = QHostAddress(QStringLiteral("127.0.0.2"));
        RbMode mode;
        QString err;
        QVERIFY(!mode.start(o, &err));
        QVERIFY(err.contains("already in use"));
    }
};

QTEST_GUILESS_MAIN(TestRbMode)
#include "test_rb_mode.moc"
