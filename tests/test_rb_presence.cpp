#include <QtTest>
#include <algorithm>

#include <QSignalSpy>
#include <QUdpSocket>

#include "dbserver/rb_presence.h"

using namespace vdj;
using namespace vdj::db;

namespace {

std::vector<uint8_t> fixture(const char* name)
{
    QFile f(QStringLiteral(VDJ_DBSERVER_FIXTURES "/udp/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray b = f.readAll();
    return std::vector<uint8_t>(b.begin(), b.end());
}

// Collects datagrams sent to a loopback port
struct Listener {
    QUdpSocket sock;
    std::vector<std::vector<uint8_t>> packets;
    Listener()
    {
        sock.bind(QHostAddress::LocalHost, 0);
        QObject::connect(&sock, &QUdpSocket::readyRead, &sock, [this]() {
            while (sock.hasPendingDatagrams()) {
                QByteArray d;
                d.resize(int(sock.pendingDatagramSize()));
                sock.readDatagram(d.data(), d.size());
                packets.emplace_back(d.begin(), d.end());
            }
        });
    }
    quint16 port() const { return sock.localPort(); }
    int count(uint8_t type) const
    {
        int n = 0;
        for (const auto& p : packets) n += hasHeader(p.data(), p.size()) && p[10] == type;
        return n;
    }
};

template <typename Fn>
bool waitFor(Fn cond, int ms = 3000)
{
    for (int i = 0; i < ms / 10 && !cond(); ++i) QTest::qWait(10);
    return cond();
}

void sendTo(quint16 port, const std::vector<uint8_t>& p)
{
    QUdpSocket s;
    s.writeDatagram(reinterpret_cast<const char*>(p.data()), qint64(p.size()), QHostAddress::LocalHost, port);
    s.flush();
}

} // namespace

class TestRbPresence : public QObject {
    Q_OBJECT

private:
    Listener announceOut, statusOut;       // where the presence sends

    RbPresence::Config config()
    {
        RbPresence::Config c;
        const uint8_t mac[6] = {0x00, 0x23, 0x24, 0xf8, 0x27, 0x06};
        const uint8_t ip[4] = {127, 0, 0, 2};          // "our" address; the test sends from 127.0.0.1 and so looks like another device
        std::memcpy(c.identity.mac, mac, 6);
        std::memcpy(c.identity.ip, ip, 4);
        c.identity.computerName = "DIRTBOX";
        c.broadcast = QHostAddress::LocalHost;
        c.bindAddress = QHostAddress::LocalHost;
        c.bindAnnounce = c.bindBeat = c.bindStatus = 0;
        c.destAnnounce = announceOut.port();
        c.destStatus = statusOut.port();
        c.claimIntervalMs = 10;
        c.keepAliveMs = 40;
        c.deckTimeoutMs = 300;
        c.media.trackCount = 7;
        c.media.playlistCount = 1;
        return c;
    }

    void reset()
    {
        announceOut.packets.clear();
        statusOut.packets.clear();
    }

private slots:
    void claimSequenceThenKeepAlives()
    {
        reset();
        RbPresence p;
        QSignalSpy claimed(&p, &RbPresence::claimedDevice);
        QString err;
        QVERIFY2(p.start(config(), &err), qPrintable(err));
        QVERIFY(waitFor([&] { return claimed.count() == 1 && announceOut.count(PktKeepAlive) >= 3; }));

        // what the real rekordbox PC sent (prolink-w-pc-rekordbox.pcap): stage 1 x3, stage 2 x36, no stage 3, then keep-alives
        std::vector<int> types;
        for (const auto& pk : announceOut.packets) types.push_back(pk[10]);
        std::vector<int> expect(3, 0);
        expect.insert(expect.end(), 36, 2);
        expect.push_back(6);
        QVERIFY(types.size() >= expect.size());
        QVERIFY(std::equal(expect.begin(), expect.end(), types.begin()));
        QCOMPARE(int(std::count(types.begin(), types.end(), 4)), 0);
        // stage 2 cycles 17,18,41,42,43,44 with the round number as counter
        const int cand[6] = {17, 18, 41, 42, 43, 44};
        ClaimInfo c;
        for (int i = 0; i < 36; ++i) {
            const auto& pk = announceOut.packets[size_t(3 + i)];
            QVERIFY(parseClaim(pk.data(), pk.size(), &c));
            QCOMPARE(int(c.stage), 2);
            QCOMPARE(int(c.device), cand[i % 6]);
            QCOMPARE(int(c.counter), i / 6 + 1);
        }
        KeepAliveInfo k;
        QVERIFY(parseKeepAlive(announceOut.packets[39].data(), announceOut.packets[39].size(), &k));
        QCOMPARE(int(k.device), 17);
        QCOMPARE(QString::fromStdString(k.name), QStringLiteral("rekordbox"));
        // the computer name goes out once the number is claimed
        QVERIFY(waitFor([&] { return statusOut.count(PktName) >= 1; }));
        QVERIFY(p.claimed());
    }

    void deckStyleClaimIsNineClaimPackets()
    {
        reset();
        RbPresence p;
        auto cfg = config();
        cfg.deckStyleClaim = true;
        QVERIFY(p.start(cfg));
        QVERIFY(waitFor([&] { return announceOut.count(PktKeepAlive) >= 1; }));
        std::vector<int> types;
        for (const auto& pk : announceOut.packets) types.push_back(pk[10]);
        const std::vector<int> expect = {0, 0, 0, 2, 2, 2, 4, 4, 4, 6};
        QVERIFY(types.size() >= expect.size());
        QVERIFY(std::equal(expect.begin(), expect.end(), types.begin()));
    }

    void keepAliveCountsTheDevicesOnTheNetwork()
    {
        reset();
        RbPresence p;
        QVERIFY(p.start(config()));
        QVERIFY(waitFor([&] { return announceOut.count(PktKeepAlive) >= 1; }));
        auto lastCount = [&] { const auto& pk = announceOut.packets.back(); return int(pk[0x30]); };
        QCOMPARE(lastCount(), 1);                                    // alone
        sendTo(p.localAnnouncePort(), fixture("deck_keepalive_xdj700.bin"));
        QVERIFY(waitFor([&] { return announceOut.packets.back()[10] == PktKeepAlive && lastCount() == 2; }));
    }

    void answersTheDecksPoll()
    {
        reset();
        RbPresence p;
        QVERIFY(p.start(config()));
        QVERIFY(waitFor([&] { return p.claimed(); }));
        statusOut.packets.clear();
        sendTo(p.localStatusPort(), fixture("deck_poll.bin"));
        QVERIFY(waitFor([&] { return statusOut.count(PktPollReply) >= 1; }));
    }

    void answersMediaQueryForOurSlotOnly()
    {
        reset();
        RbPresence p;
        QVERIFY(p.start(config()));
        QVERIFY(waitFor([&] { return p.claimed(); }));
        QSignalSpy queried(&p, &RbPresence::mediaQueried);

        const auto q = fixture("deck_media_query.bin");        // device 1 asks device 17 about slot 4
        sendTo(p.localStatusPort(), q);
        QVERIFY(waitFor([&] { return statusOut.count(PktMediaResponse) >= 1; }));
        const auto& resp = [&]() -> const std::vector<uint8_t>& {
            for (const auto& pk : statusOut.packets) if (pk[10] == PktMediaResponse) return pk;
            return statusOut.packets[0];
        }();
        QCOMPARE(int(resp.size()), 192);
        QCOMPARE(int(resp[0xa6]) * 256 + resp[0xa7], 7);
        QCOMPARE(queried.count(), 1);
        QVERIFY(queried[0][2].toBool());

        // a query for slot 3 (USB) or another device is not ours
        const int before = statusOut.count(PktMediaResponse);
        auto other = q;
        other[0x2f] = 3;
        sendTo(p.localStatusPort(), other);
        other = q;
        other[0x2b] = 5;
        sendTo(p.localStatusPort(), other);
        QVERIFY(waitFor([&] { return queried.count() == 3; }));
        QTest::qWait(50);
        QCOMPARE(statusOut.count(PktMediaResponse), before);
        QVERIFY(!queried[1][2].toBool());
        QVERIFY(!queried[2][2].toBool());
    }

    void tracksDecksAndReportsLoss()
    {
        reset();
        RbPresence p;
        QSignalSpy seen(&p, &RbPresence::deckSeen), lost(&p, &RbPresence::deckLost);
        QVERIFY(p.start(config()));
        QVERIFY(waitFor([&] { return p.claimed(); }));
        const int names = statusOut.count(PktName);

        sendTo(p.localAnnouncePort(), fixture("deck_keepalive_xdj700.bin"));
        QVERIFY(waitFor([&] { return seen.count() == 1; }));
        QCOMPARE(seen[0][1].toString(), QStringLiteral("XDJ-700"));
        QCOMPARE(seen[0][2].toInt(), 1);
        QCOMPARE(int(p.decks().size()), 1);
        QVERIFY(waitFor([&] { return statusOut.count(PktName) > names; }));     // the new deck is told our name
        QVERIFY(waitFor([&] { return lost.count() == 1; }, 2000));               // no more keep-alives: lost after 300 ms
        QVERIFY(p.decks()[0].lost);

        sendTo(p.localAnnouncePort(), fixture("deck_keepalive_xdj700.bin"));    // it comes back
        QVERIFY(waitFor([&] { return seen.count() == 2; }));
        QVERIFY(!p.decks()[0].lost);
        QVERIFY(!p.packetCounts().isEmpty());
    }

    void renumbersOnCollision()
    {
        reset();
        RbPresence p;
        QSignalSpy collision(&p, &RbPresence::collision);
        QVERIFY(p.start(config()));
        QVERIFY(waitFor([&] { return p.claimed(); }));
        announceOut.packets.clear();

        sendTo(p.localAnnouncePort(), fixture("rb_stage2.bin"));       // another rekordbox claims 17
        QVERIFY(waitFor([&] { return collision.count() == 1; }));
        QCOMPARE(p.deviceNumber(), 18);
        QVERIFY(waitFor([&] { return p.claimed() && announceOut.count(PktClaim2) >= 3; }));
        ClaimInfo c;
        for (const auto& pk : announceOut.packets)
            if (pk[10] == PktClaim2) { QVERIFY(parseClaim(pk.data(), pk.size(), &c)); QCOMPARE(int(c.device), 18); break; }
    }

    void ignoresGarbageAndOwnPackets()
    {
        reset();
        RbPresence p;
        QVERIFY(p.start(config()));
        QVERIFY(waitFor([&] { return p.claimed(); }));
        sendTo(p.localAnnouncePort(), std::vector<uint8_t>(5, 0x42));
        sendTo(p.localStatusPort(), std::vector<uint8_t>(200, 0x00));
        sendTo(p.localAnnouncePort(), {});
        QTest::qWait(50);
        QCOMPARE(int(p.decks().size()), 0);
        QVERIFY(p.isRunning());
    }

    void startFailsWhenPortsAreTaken()
    {
        RbPresence a;
        QVERIFY(a.start(config()));
        QUdpSocket blocker;
        QVERIFY(blocker.bind(QHostAddress::LocalHost, 0, QUdpSocket::DontShareAddress));
        RbPresence b;
        auto c = config();
        c.bindAnnounce = blocker.localPort();
        QString err;
        QVERIFY(!b.start(c, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!b.isRunning());
    }
};

QTEST_GUILESS_MAIN(TestRbPresence)
#include "test_rb_presence.moc"
