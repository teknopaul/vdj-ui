#include <QtTest>

#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <map>

#include "dbserver/db_server.h"

using namespace vdj;
using namespace vdj::db;

namespace {

QByteArray readFixture(const char* name)
{
    QFile f(QStringLiteral(VDJ_DBSERVER_FIXTURES "/") + QLatin1String(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// A client that collects what the server sends while the test spins the event loop
struct TestClient {
    QTcpSocket sock;
    QByteArray received;
    StreamParser parser;
    std::vector<Message> messages;
    int greetings = 0;

    TestClient()
    {
        QObject::connect(&sock, &QTcpSocket::readyRead, &sock, [this]() {
            const QByteArray b = sock.readAll();
            received.append(b);
            parser.feed(reinterpret_cast<const uint8_t*>(b.constData()), size_t(b.size()));
            for (;;) {
                Message m;
                auto st = parser.next(m);
                if (st == StreamParser::MessageReady) messages.push_back(std::move(m));
                else if (st == StreamParser::Greeting) ++greetings;
                else break;
            }
        });
    }
    bool connectTo(quint16 port)
    {
        sock.connectToHost(QHostAddress::LocalHost, port);
        return sock.waitForConnected(2000);
    }
    void send(const QByteArray& b) { sock.write(b); sock.flush(); }
    void send(const Message& m)
    {
        const auto e = encode(m);
        send(QByteArray(reinterpret_cast<const char*>(e.data()), int(e.size())));
    }
};

template <typename Fn>
bool waitFor(Fn cond, int ms = 3000)
{
    for (int i = 0; i < ms / 10 && !cond(); ++i) QTest::qWait(10);
    return cond();
}

Message req(uint16_t type, std::vector<Arg> args, uint32_t txid)
{
    Message m;
    m.txid = txid;
    m.type = type;
    m.args = std::move(args);
    return m;
}

} // namespace

class TestDbServer : public QObject {
    Q_OBJECT

private:
    RbLibrary lib_;
    QTemporaryDir logDir_;

    std::unique_ptr<DbServer> makeServer(UnknownPolicy policy = UnknownPolicy::EmptyOk, bool logs = true)
    {
        std::unique_ptr<DbServer> s(new DbServer(&lib_));
        DbServer::Config cfg;
        cfg.queryPort = 0;
        cfg.dbPort = 0;
        cfg.deviceNumber = 1;
        cfg.bindAddress = QHostAddress::LocalHost;
        cfg.unknownPolicy = policy;
        if (logs) cfg.logDir = logDir_.path();
        QString err;
        if (!s->start(cfg, &err)) qWarning() << err;
        return s;
    }

private slots:
    void initTestCase()
    {
        std::string err;
        QVERIFY2(lib_.load(QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7").toStdString(), &err), err.c_str());
        QVERIFY(logDir_.isValid());
    }

    void portQueryReturnsTheDbPort()
    {
        auto server = makeServer();
        QVERIFY(server->isRunning());
        TestClient c;
        QVERIFY(c.connectTo(server->queryPort()));
        c.send(readFixture("port_query_req.bin"));
        QVERIFY(waitFor([&] { return c.received.size() >= 2; }));
        QCOMPARE(int(uchar(c.received[0])) * 256 + uchar(c.received[1]), int(server->dbPort()));
        QVERIFY(waitFor([&] { return c.sock.state() == QAbstractSocket::UnconnectedState; }));
        QCOMPARE(int(server->stats().portQueries), 1);
    }

    void badPortQueryIsClosedWithoutReply()
    {
        auto server = makeServer();
        TestClient c;
        QVERIFY(c.connectTo(server->queryPort()));
        c.send(QByteArray(19, 'x'));
        QVERIFY(waitFor([&] { return c.sock.state() == QAbstractSocket::UnconnectedState; }));
        QVERIFY(c.received.isEmpty());
    }

    void capturedSessionGetsTheDecksReplyCount()
    {
        auto server = makeServer();
        QSignalSpy connected(server.get(), &DbServer::clientConnected);
        TestClient c;
        QVERIFY(c.connectTo(server->dbPort()));
        QVERIFY(waitFor([&] { return c.greetings == 1; }));          // the server greets on connect
        const QByteArray stream = readFixture("client_stream.bin");
        c.send(stream);
        // 209 reply messages in the captured session (3100 requests are answered with one 4000 each, as the deck did)
        QVERIFY2(waitFor([&] { return c.messages.size() >= 209; }), qPrintable(QString::number(c.messages.size())));
        QTest::qWait(50);
        QCOMPARE(int(c.messages.size()), 209);
        std::map<uint16_t, int> hist;
        for (const auto& m : c.messages) ++hist[m.type];
        QCOMPARE(hist[0x4101], 124);
        QCOMPARE(hist[0x4000], 27);
        QCOMPARE(hist[0x4402], 3);
        QCOMPARE(connected.count(), 1);
        QVERIFY(!c.parser.failed());
    }

    void sessionLogsAreWritten()
    {
        QTemporaryDir dir;
        std::unique_ptr<DbServer> s(new DbServer(&lib_));
        DbServer::Config cfg;
        cfg.queryPort = 0; cfg.dbPort = 0; cfg.bindAddress = QHostAddress::LocalHost; cfg.logDir = dir.path();
        QVERIFY(s->start(cfg));
        TestClient c;
        QVERIFY(c.connectTo(s->dbPort()));
        QVERIFY(waitFor([&] { return c.greetings == 1; }));
        const QByteArray stream = readFixture("client_stream.bin");
        c.send(stream);
        QVERIFY(waitFor([&] { return c.messages.size() >= 209; }));
        c.sock.disconnectFromHost();
        QVERIFY(waitFor([&] { return s->clientCount() == 0; }));

        const QStringList bins = QDir(dir.path()).entryList({"dbserver-127.0.0.1-*.bin"});
        const QStringList logs = QDir(dir.path()).entryList({"dbserver-127.0.0.1-*.log"});
        QCOMPARE(bins.size(), 1);
        QCOMPARE(logs.size(), 1);
        QFile bin(dir.filePath(bins[0]));
        QVERIFY(bin.open(QIODevice::ReadOnly));
        QCOMPARE(bin.readAll(), stream);                           // exact inbound stream
        QFile log(dir.filePath(logs[0]));
        QVERIFY(log.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(log.readAll());
        QVERIFY(text.contains("type=0x1000"));
        QVERIFY(text.contains("connected from"));
    }

    void unknownRequestIsLoggedAndAnsweredPerPolicy()
    {
        QTemporaryDir dir;
        std::unique_ptr<DbServer> s(new DbServer(&lib_));
        DbServer::Config cfg;
        cfg.queryPort = 0; cfg.dbPort = 0; cfg.bindAddress = QHostAddress::LocalHost; cfg.logDir = dir.path();
        QVERIFY(s->start(cfg));
        QSignalSpy unknown(s.get(), &DbServer::unknownRequest);

        TestClient c;
        QVERIFY(c.connectTo(s->dbPort()));
        c.send(encodeGreeting().empty() ? QByteArray() : QByteArray::fromHex("1100000001"));
        c.send(req(0x7777, {Arg::number(0x04010301), Arg::stringUtf8("hello")}, 5));
        QVERIFY(waitFor([&] { return !c.messages.empty(); }));
        QCOMPARE(c.messages[0].type, uint16_t(RepOk));
        QCOMPARE(c.messages[0].args[0].num, uint32_t(0x7777));
        QCOMPARE(c.messages[0].args[1].num, uint32_t(0));
        QCOMPARE(unknown.count(), 1);
        QCOMPARE(unknown[0][1].toUInt(), 0x7777u);
        QVERIFY(!unknown[0][3].toByteArray().isEmpty());
        QCOMPARE(int(s->stats().unknownRequests), 1);

        QFile f(dir.filePath("unknown-requests.log"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString t = QString::fromUtf8(f.readAll());
        QVERIFY(t.contains("type=0x7777"));
        QVERIFY(t.contains("hello"));
        QVERIFY(t.contains("hex=1187234"));       // the raw message bytes

        // policy Close
        s->setUnknownPolicy(UnknownPolicy::Close);
        TestClient c2;
        QVERIFY(c2.connectTo(s->dbPort()));
        c2.send(req(0x7778, {}, 6));
        QVERIFY(waitFor([&] { return c2.sock.state() == QAbstractSocket::UnconnectedState; }));
    }

    void twoClientsAtOnceAndDisconnectMessage()
    {
        auto server = makeServer();
        TestClient a, b;
        QVERIFY(a.connectTo(server->dbPort()));
        QVERIFY(b.connectTo(server->dbPort()));
        QVERIFY(waitFor([&] { return server->clientCount() == 2; }));
        a.send(req(ReqSetup, {Arg::number(4)}, kSetupTxid));
        b.send(req(ReqSetup, {Arg::number(3)}, kSetupTxid));
        QVERIFY(waitFor([&] { return !a.messages.empty() && !b.messages.empty(); }));
        QCOMPARE(a.messages[0].args[1].num, uint32_t(1));
        a.send(req(ReqDisconnect, {}, kSetupTxid));
        QVERIFY(waitFor([&] { return a.sock.state() == QAbstractSocket::UnconnectedState; }));
        QCOMPARE(server->clientCount(), 1);
        b.sock.disconnectFromHost();
        QVERIFY(waitFor([&] { return server->clientCount() == 0; }));
    }

    void halfMessageThenCloseDoesNotCrash()
    {
        auto server = makeServer();
        QSignalSpy closed(server.get(), &DbServer::clientClosed);
        TestClient c;
        QVERIFY(c.connectTo(server->dbPort()));
        const auto e = encode(req(ReqRootMenu, {Arg::number(0x04010301), Arg::number(0), Arg::number(0xffffff)}, 1));
        c.send(QByteArray(reinterpret_cast<const char*>(e.data()), int(e.size() / 2)));
        QTest::qWait(50);
        c.sock.disconnectFromHost();
        QVERIFY(waitFor([&] { return closed.count() == 1; }));
        QCOMPARE(server->clientCount(), 0);
    }

    void garbageClosesTheConnection()
    {
        auto server = makeServer();
        TestClient c;
        QVERIFY(c.connectTo(server->dbPort()));
        c.send(QByteArray("this is not the dbserver protocol at all"));
        QVERIFY(waitFor([&] { return c.sock.state() == QAbstractSocket::UnconnectedState; }));
        QCOMPARE(server->clientCount(), 0);
    }

    void hugeRenderLimitIsFine()
    {
        auto server = makeServer();
        TestClient c;
        QVERIFY(c.connectTo(server->dbPort()));
        c.send(req(ReqTrackMenu, {Arg::number(0x04020301), Arg::number(0)}, 1));
        c.send(req(ReqRender, {Arg::number(0x04020301), Arg::number(0), Arg::number(0xffffffff), Arg::number(0), Arg::number(7), Arg::number(0)}, 2));
        QVERIFY(waitFor([&] { return c.messages.size() >= 1 + 9; }));
        QCOMPARE(c.messages.back().type, uint16_t(RepMenuFooter));
    }

    void closeClientsForAHost()
    {
        auto server = makeServer();
        TestClient c;
        QVERIFY(c.connectTo(server->dbPort()));
        QVERIFY(waitFor([&] { return server->clientCount() == 1; }));
        server->closeClientsFor(QHostAddress::LocalHost);
        QVERIFY(waitFor([&] { return c.sock.state() == QAbstractSocket::UnconnectedState; }));
    }

    void startFailsWhenThePortIsTaken()
    {
        auto first = makeServer();
        DbServer second(&lib_);
        DbServer::Config cfg;
        cfg.dbPort = first->dbPort();
        cfg.queryPort = 0;
        cfg.bindAddress = QHostAddress::LocalHost;
        QString err;
        QVERIFY(!second.start(cfg, &err));
        QVERIFY(!err.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDbServer)
#include "test_db_server.moc"
