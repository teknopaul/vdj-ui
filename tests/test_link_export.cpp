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
};

QTEST_MAIN(TestLinkExport)
#include "test_link_export.moc"
