#include <QtTest>

#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUdpSocket>

#include "network/link_export.h"

extern "C" {
#include "mount_client.h"
#include "nfs_client.h"
#include "rpc.h"
#include "utf16.h"
}

using namespace vdj;

namespace {

// the media query a deck sends: asker device 4, at 127.0.0.1 here, target device N, slot S
QByteArray mediaQuery(int target, int slot)
{
    QByteArray q = QByteArray::fromHex("5173707431576d4a4f4c05") + QByteArray("XDJ-700").leftJustified(20, '\0');
    q += QByteArray::fromHex("01000400" "0c" "7f000001");
    q += QByteArray::fromHex("00000000").replace(3, 1, QByteArray(1, char(target)));
    q += QByteArray::fromHex("00000000").replace(3, 1, QByteArray(1, char(slot)));
    // the capture: ... 01 00 04 00 0c <ip> 00 00 00 <target> 00 00 00 <slot>  (header up to 0x1f is 31 bytes: type + name)
    return q;
}

} // namespace

class TestLinkExport : public QObject {
    Q_OBJECT

private slots:
    void answersTheMediaQueryServesTheDbserverAndTheFiles()
    {
        QUdpSocket asker;                                   // the deck: receives the media response on "its port 50002"
        QVERIFY(asker.bind(QHostAddress::LocalHost, 0));
        QTemporaryDir logs;

        LinkExport le;
        LinkExport::Config c;
        c.folder = QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7");
        c.deviceNumber = 2;
        c.logDir = logs.path();
        c.queryPort = 0; c.dbPort = 0;
        c.altPortmapPort = 53311; c.portmapPort = 0; c.mountPort = 53312; c.nfsPort = 53349;
        c.replyToPort = asker.localPort();
        QString err;
        QVERIFY2(le.start(c, &err), qPrintable(err));
        QVERIFY(le.running());
        QVERIFY(le.server() != nullptr);
        QVERIFY(le.server()->dbPort() != 0);                // the dbserver listens
        QCOMPARE(int(le.library().trackCount()), 7);

        // a media query for our USB slot is answered, one for another player or slot is not
        QSignalSpy queried(&le, &LinkExport::mediaQueried);
        le.onStatusPortPacket(mediaQuery(2, 3));
        QVERIFY(QTest::qWaitFor([&] { return asker.hasPendingDatagrams(); }, 2000));
        QByteArray resp(int(asker.pendingDatagramSize()), 0);
        asker.readDatagram(resp.data(), resp.size());
        QCOMPARE(resp.size(), 192);
        QCOMPARE(int(uint8_t(resp[10])), 0x06);
        QCOMPARE(int(uint8_t(resp[0x20])), 0);              // from a player
        QCOMPARE(int(uint8_t(resp[0x21])), 2);              // our number
        QCOMPARE(int(uint8_t(resp[0x2b])), 3);              // slot 3
        QCOMPARE(int(uint8_t(resp[0xa6])) * 256 + uint8_t(resp[0xa7]), 7);       // tracks
        le.onStatusPortPacket(mediaQuery(2, 4));
        le.onStatusPortPacket(mediaQuery(1, 3));
        QTest::qWait(150);
        QVERIFY(!asker.hasPendingDatagrams());
        QVERIFY(queried.count() >= 2);

        // the files: mount /C/, walk to the pdb (any case), read it
        rpc_client_t mc, nc;
        QVERIFY(rpc_client_init(&mc, "127.0.0.1", 53312, 100005, 1, 2000) == 0);
        QVERIFY(rpc_client_init(&nc, "127.0.0.1", 53349, 100003, 2, 2000) == 0);
        uint8_t path[16]; uint32_t plen = 0;
        utf8_to_utf16le("/C/", path, sizeof path, &plen);
        dirpath_t dp = { path, plen };
        fhstatus_t fhs;
        QVERIFY(mount_mnt(&mc, &dp, &fhs) == 0 && fhs.status == 0);
        fhandle_t f; fattr_t a;
        QVERIFY(nfs_path_lookup(&nc, &fhs.directory, "pioneer/rekordbox/EXPORT.PDB", &f, &a) == 0);
        readargs_t ra; readres_t rr;
        memset(&rr, 0, sizeof rr);
        ra.file = f; ra.offset = 0; ra.count = 4096; ra.totalcount = 0;
        QVERIFY(nfs_read(&nc, &ra, &rr) == 0 && rr.status == 0 && rr.data_len == 4096);
        QFile pdb(QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7/PIONEER/rekordbox/export.pdb"));
        QVERIFY(pdb.open(QIODevice::ReadOnly));
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(rr.data), int(rr.data_len)), pdb.read(4096));
        nfs_readres_free(&rr);
        rpc_client_destroy(&mc);
        rpc_client_destroy(&nc);
        QVERIFY(le.nfsBytesServed() >= 4096);

        le.stop();
        QVERIFY(!le.running());
    }

    void refusesWithoutAPlayerNumberOrAFolder()
    {
        LinkExport le;
        LinkExport::Config c;
        c.folder = QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7");
        QString err;
        QVERIFY(!le.start(c, &err));                         // deviceNumber 0: the join has not given us one yet
        c.deviceNumber = 2;
        c.folder = QStringLiteral("/nonexistent/folder");
        QVERIFY(!le.start(c, &err));
        QVERIFY(!le.running());
    }
    // the status bytes follow the sequence of a real deck (pcaps/prolink-usb-extracts.pcap): 02 -> 03 -> 04 (+ media gone), insert: media, ready
    void swapsThePendriveLikeADeck()
    {
        QTemporaryDir logs;
        LinkExport le;
        LinkExport::Config c;
        c.folder = QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7");
        c.deviceNumber = 2;
        c.logDir = logs.path();
        c.queryPort = 0; c.dbPort = 0;
        c.altPortmapPort = 53321; c.portmapPort = 0; c.mountPort = 53322; c.nfsPort = 53359;
        c.unloadingMs = 60; c.removableMs = 20; c.refusedMs = 40; c.swapGapMs = 30; c.insertMs = 30;
        QString err;
        QVERIFY2(le.start(c, &err), qPrintable(err));
        QCOMPARE(int(le.library().trackCount()), 7);

        QList<QPair<int, bool>> seen;
        QObject::connect(&le, &LinkExport::usbSlotChanged, &le, [&](quint8 st, bool m) { seen << qMakePair(int(st), m); });
        QSignalSpy mounts(&le, &LinkExport::mountChanged);

        QVERIFY(le.switchTo(QStringLiteral("/nonexistent"), QString()).contains("export.pdb"));   // validated before anything moves
        QVERIFY(seen.isEmpty());

        QVERIFY2(le.switchTo(QStringLiteral(VDJ_DBSERVER_FIXTURES "/export"), QStringLiteral("STICK-B")).isEmpty(), "idle: accepted");
        QVERIFY(QTest::qWaitFor([&] { return le.usbState() == LinkExport::UsbState::Ready && le.folder().endsWith("/export"); }, 3000));
        const QList<QPair<int, bool>> want = { {2, true}, {3, true}, {4, false}, {4, true}, {0, true} };
        QCOMPARE(seen, want);
        QCOMPARE(le.displayName(), QStringLiteral("STICK-B"));
        QVERIFY(le.library().trackCount() != 7);                       // the other export is loaded
        QCOMPARE(mounts.count(), 2);                                   // unmounted, mounted
        QVERIFY(!mounts.at(0).at(2).toBool() && mounts.at(1).at(2).toBool());
        le.stop();
    }

    void refusesToUnmountWhileInUse()
    {
        QTemporaryDir logs;
        LinkExport le;
        LinkExport::Config c;
        c.folder = QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7");
        c.deviceNumber = 2;
        c.logDir = logs.path();
        c.queryPort = 0; c.dbPort = 0;
        c.altPortmapPort = 53331; c.portmapPort = 0; c.mountPort = 53332; c.nfsPort = 53369;
        c.unloadingMs = 60; c.removableMs = 20; c.refusedMs = 40;
        QString err;
        QVERIFY2(le.start(c, &err), qPrintable(err));
        bool busy = true;
        le.setUsageProbe([&]() { return busy ? QStringLiteral("player 4 has a track loaded from it") : QString(); });

        QList<QPair<int, bool>> seen;
        QObject::connect(&le, &LinkExport::usbSlotChanged, &le, [&](quint8 st, bool m) { seen << qMakePair(int(st), m); });
        QSignalSpy refused(&le, &LinkExport::switchRefused);
        QVERIFY(le.requestUnmount().contains("player 4"));
        QVERIFY(QTest::qWaitFor([&] { return le.usbState() == LinkExport::UsbState::Ready && seen.size() == 2; }, 2000));
        const QList<QPair<int, bool>> want = { {2, true}, {0, true} };        // 00 -> 02 -> 00, nothing else changes
        QCOMPARE(seen, want);
        QCOMPARE(refused.count(), 1);
        QVERIFY(le.mediaPresent());
        QVERIFY(le.switchTo(QStringLiteral(VDJ_DBSERVER_FIXTURES "/export"), QString()).contains("player 4"));
        QVERIFY(QTest::qWaitFor([&] { return le.usbState() == LinkExport::UsbState::Ready && seen.size() == 4; }, 2000));
        QVERIFY(le.folder().endsWith("/export7"));                      // still the old stick
        busy = false;
        QVERIFY(le.requestUnmount().isEmpty());
        QVERIFY(QTest::qWaitFor([&] { return le.usbState() == LinkExport::UsbState::Empty; }, 2000));
        QVERIFY(!le.mediaPresent());
        le.forceEmpty("test");                                          // idempotent
        QCOMPARE(int(le.usbState()), 4);
        le.stop();
    }
};

QTEST_MAIN(TestLinkExport)
#include "test_link_export.moc"
