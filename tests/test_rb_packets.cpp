#include <QtTest>

#include "dbserver/rb_packets.h"

using namespace vdj::db;

namespace {

std::vector<uint8_t> fixture(const char* name)
{
    QFile f(QStringLiteral(VDJ_DBSERVER_FIXTURES "/udp/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray b = f.readAll();
    return std::vector<uint8_t>(b.begin(), b.end());
}

Identity rekordbox()
{
    Identity id;
    const uint8_t mac[6] = {0x00, 0x23, 0x24, 0xf8, 0x27, 0x06};
    const uint8_t ip[4] = {169, 254, 51, 135};
    memcpy(id.mac, mac, 6);
    memcpy(id.ip, ip, 4);
    id.computerName = "DIRTBOX";
    return id;
}

Identity xdj700(uint8_t device, const uint8_t mac[6], const uint8_t ip[4])
{
    Identity id = Identity::deck();
    id.device = device;
    memcpy(id.mac, mac, 6);
    memcpy(id.ip, ip, 4);
    return id;
}

QString hex(const std::vector<uint8_t>& v) { return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(v.data()), int(v.size())).toHex(' ')); }

#define COMPARE_PKT(ours, theirs) \
    QVERIFY2((ours) == (theirs), qPrintable("ours:  " + hex(ours) + "\ndeck:  " + hex(theirs)))

} // namespace

class TestRbPackets : public QObject {
    Q_OBJECT

private slots:
    void fixturesPresent()
    {
        QCOMPARE(int(fixture("rb_stage1.bin").size()), 44);
        QCOMPARE(int(fixture("rb_name.bin").size()), 296);
        QCOMPARE(int(fixture("rb_media_response.bin").size()), 192);
    }

    void rekordboxClaimsMatchTheCapture()
    {
        const Identity id = rekordbox();
        COMPARE_PKT(claimStage1(id, 1), fixture("rb_stage1.bin"));
        COMPARE_PKT(claimStage2(id, 1), fixture("rb_stage2.bin"));
    }

    void rekordboxKeepAliveMatchesTheCapture()
    {
        COMPARE_PKT(keepAlive(rekordbox()), fixture("rb_keepalive.bin"));
    }

    void namePacketMatchesTheCapture()
    {
        COMPARE_PKT(namePacket(rekordbox()), fixture("rb_name.bin"));
    }

    void mediaResponseMatchesTheCapture()
    {
        MediaInfo m;
        m.slot = 4;
        m.name = "rekordbox";
        m.trackCount = 0x098a;
        m.playlistCount = 0x78;
        COMPARE_PKT(mediaResponse(rekordbox(), m), fixture("rb_media_response.bin"));
    }

    // The same builders, given a deck's identity, rebuild the deck's own packets (so the layout is not just rekordbox's)
    void deckPacketsMatchTheCapture()
    {
        const uint8_t mac[6] = {0xc8, 0x3d, 0xfc, 0x0c, 0xb1, 0xfd};
        const uint8_t ip[4] = {169, 254, 177, 253};
        const Identity d = xdj700(4, mac, ip);
        COMPARE_PKT(claimStage1(d, 1), fixture("deck_stage1.bin"));
        COMPARE_PKT(claimStage2(d, 1), fixture("deck_stage2.bin"));
        COMPARE_PKT(claimStage3(d, 1), fixture("deck_stage3.bin"));

        const uint8_t mac2[6] = {0xc8, 0x3d, 0xfc, 0x0c, 0xe7, 0xd2};
        const uint8_t ip2[4] = {169, 254, 231, 210};
        COMPARE_PKT(keepAlive(xdj700(1, mac2, ip2)), fixture("deck_keepalive_xdj700.bin"));
    }

    void changingIpAndMacChangesOnlyThoseBytes()
    {
        Identity a = rekordbox(), b = rekordbox();
        const uint8_t ip[4] = {10, 11, 12, 13};
        const uint8_t mac[6] = {1, 2, 3, 4, 5, 7};
        memcpy(b.ip, ip, 4);
        memcpy(b.mac, mac, 6);
        auto x = keepAlive(a), y = keepAlive(b);
        QCOMPARE(x.size(), y.size());
        for (size_t i = 0; i < x.size(); ++i) {
            const bool macByte = i >= 0x26 && i < 0x2c, ipByte = i >= 0x2c && i < 0x30;
            QCOMPARE(x[i] != y[i], macByte || ipByte);
        }
    }

    void mediaResponseCountsAreEncodedAtTheDocumentedOffsets()
    {
        MediaInfo m;
        m.trackCount = 1234;
        m.playlistCount = 56;
        const auto p = mediaResponse(rekordbox(), m);
        QCOMPARE(int(p.size()), 192);
        QCOMPARE(int(p[0xa6]) * 256 + p[0xa7], 1234);
        QCOMPARE(int(p[0xae]) * 256 + p[0xaf], 56);
    }

    void namesAreTruncatedNotOverflowed()
    {
        Identity id = rekordbox();
        id.name = "a-very-long-device-name-that-does-not-fit";
        id.computerName = std::string(300, 'x');
        QCOMPARE(int(keepAlive(id).size()), 54);
        QCOMPARE(int(namePacket(id).size()), 296);
    }

    void parseMediaQueryFromTheDeck()
    {
        const auto q = fixture("deck_media_query.bin");
        MediaQuery m;
        QVERIFY(parseMediaQuery(q.data(), q.size(), &m));
        QCOMPARE(QString::fromStdString(m.name), QStringLiteral("XDJ-700"));
        QCOMPARE(int(m.askerDevice), 1);
        QCOMPARE(QString::fromStdString(ipString(m.askerIp)), QStringLiteral("169.254.231.210"));
        QCOMPARE(m.targetDevice, 17u);
        QCOMPARE(m.slot, 4u);
        QVERIFY(!parseMediaQuery(q.data(), 20, &m));
    }

    // A player (the captured XDJ-700, device 1) describing the stick in slot 3
    void deckMediaResponseMatchesTheCapture()
    {
        Identity d = Identity::deck();
        d.device = 1;
        MediaInfo m;
        m.slot = 3; m.name = ""; m.trackCount = 0x0896; m.playlistCount = 0x78;
        m.fromPlayer = true; m.date = "2025-04-11"; m.version = "1000";
        m.totalBytes = 0x0e66fe8000ULL; m.freeBytes = 0x06c8d98000ULL;
        COMPARE_PKT(mediaResponse(d, m), fixture("deck_media_response.bin"));
    }

    void pcStatusMatchesTheCapture()
    {
        COMPARE_PKT(pcStatus(rekordbox(), 0x33d2), fixture("rb_status.bin"));
        const auto m = pcStatus(rekordbox(), 12000, true, 2);
        QCOMPARE(int(m.size()), 56);
        QCOMPARE(int(m[0x27]), 0xe0);                // master: flags e0 and byte 0x34 = 1, as at 66.3 s of the capture
        QCOMPARE(int(m[0x34]), 1);
        QCOMPARE(int(m[0x37]), 2);
    }

    void pollReplyMatchesTheCapture()
    {
        COMPARE_PKT(pollReply(rekordbox()), fixture("rb_poll_reply.bin"));
        const auto q = fixture("deck_poll.bin");
        PollInfo p;
        QVERIFY(parsePoll(q.data(), q.size(), &p));
        QCOMPARE(QString::fromStdString(p.name), QStringLiteral("XDJ-700"));
        QCOMPARE(int(p.device), 1);
        QVERIFY(!parsePoll(q.data(), 30, &p));
    }

    void parseKeepAliveAndClaims()
    {
        KeepAliveInfo k;
        auto a = fixture("rb_keepalive.bin");
        QVERIFY(parseKeepAlive(a.data(), a.size(), &k));
        QCOMPARE(QString::fromStdString(k.name), QStringLiteral("rekordbox"));
        QCOMPARE(int(k.device), 17);
        QCOMPARE(int(k.deviceType), 1);
        QCOMPARE(QString::fromStdString(ipString(k.ip)), QStringLiteral("169.254.51.135"));
        a = fixture("deck_keepalive_xdj700.bin");
        QVERIFY(parseKeepAlive(a.data(), a.size(), &k));
        QCOMPARE(int(k.deviceType), 2);

        ClaimInfo c;
        a = fixture("deck_stage2.bin");
        QVERIFY(parseClaim(a.data(), a.size(), &c));
        QCOMPARE(int(c.stage), 2);
        QCOMPARE(int(c.device), 4);
        QCOMPARE(int(c.counter), 1);
        a = fixture("deck_stage3.bin");
        QVERIFY(parseClaim(a.data(), a.size(), &c));
        QCOMPARE(int(c.stage), 3);
        QCOMPARE(int(c.device), 4);
        a = fixture("rb_stage1.bin");
        QVERIFY(parseClaim(a.data(), a.size(), &c));
        QCOMPARE(int(c.stage), 1);
    }

    void garbageIsRejected()
    {
        KeepAliveInfo k;
        ClaimInfo c;
        MediaQuery m;
        std::vector<uint8_t> junk(100, 0x42);
        QVERIFY(!parseKeepAlive(junk.data(), junk.size(), &k));
        QVERIFY(!parseClaim(junk.data(), junk.size(), &c));
        QVERIFY(!parseMediaQuery(junk.data(), junk.size(), &m));
        QVERIFY(!parseKeepAlive(junk.data(), 0, &k));
        auto good = fixture("rb_keepalive.bin");
        for (size_t cut = 0; cut < good.size(); ++cut) parseKeepAlive(good.data(), cut, &k);     // never reads out of bounds
    }
};

QTEST_APPLESS_MAIN(TestRbPackets)
#include "test_rb_packets.moc"
