#include <QtTest>

#include <cstdlib>
#include <map>

#include "dbserver/db_codec.h"

using namespace vdj::db;

namespace {

std::vector<uint8_t> readFixture(const char* name)
{
    QFile f(QStringLiteral(VDJ_DBSERVER_FIXTURES "/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray b = f.readAll();
    return std::vector<uint8_t>(b.begin(), b.end());
}

struct Parsed {
    std::vector<Message>              messages;
    std::vector<std::vector<uint8_t>> raw;      // exact wire bytes of each message
    int                               greetings = 0;
    bool                              failed = false;
};

// Parse a whole stream, feeding it in chunks produced by nextChunk (returns the next chunk size).
template <typename ChunkFn>
Parsed parseAll(const std::vector<uint8_t>& stream, ChunkFn nextChunk)
{
    Parsed       out;
    StreamParser p;
    size_t       pos = 0;
    while (pos < stream.size()) {
        size_t n = std::min(nextChunk(), stream.size() - pos);
        size_t before = p.consumed();
        p.feed(stream.data() + pos, n);
        pos += n;
        for (;;) {
            Message m;
            StreamParser::Status st = p.next(m);
            if (st == StreamParser::None) break;
            if (st == StreamParser::Error) { out.failed = true; return out; }
            if (st == StreamParser::Greeting) { ++out.greetings; before = p.consumed(); continue; }
            out.raw.emplace_back(stream.begin() + before, stream.begin() + p.consumed());
            before = p.consumed();
            out.messages.push_back(std::move(m));
        }
    }
    return out;
}

Parsed parseWhole(const std::vector<uint8_t>& s)
{
    return parseAll(s, [&] { return s.size(); });
}

} // namespace

class TestDbCodec : public QObject {
    Q_OBJECT

private slots:
    void fixturesPresent()
    {
        QVERIFY(readFixture("client_stream.bin").size() == 3152);
        QVERIFY(readFixture("server_stream.bin").size() == 76116);
    }

    void clientStreamParsesCompletely()
    {
        const auto s = readFixture("client_stream.bin");
        const Parsed p = parseWhole(s);
        QVERIFY(!p.failed);
        QCOMPARE(p.greetings, 1);
        QCOMPARE(int(p.messages.size()), 61);       // the plan said 27: that count came from the earlier broken parse
        size_t total = 5;
        for (const auto& r : p.raw) total += r.size();
        QCOMPARE(total, s.size());

        QCOMPARE(p.messages[0].type, uint16_t(0x0000));          // setup
        QCOMPARE(p.messages[0].txid, kSetupTxid);
        QCOMPARE(p.messages[0].args.size(), size_t(1));
        QCOMPARE(p.messages[0].args[0].num, uint32_t(4));        // requester device number
    }

    // Regression: the last request is a waveform preview (0x2004) whose 5th argument is a blob of length 0, which is
    // omitted from the wire. Earlier code read the next message's magic as that argument and gave up at byte 1377.
    void omittedBlobRegression()
    {
        const Parsed p = parseWhole(readFixture("client_stream.bin"));
        QVERIFY(!p.failed);
        const Message* wf = nullptr;
        for (const auto& m : p.messages)
            if (m.type == 0x2004) wf = &m;
        QVERIFY(wf);
        QCOMPARE(wf->args.size(), size_t(5));
        QCOMPARE(wf->args[0].num, uint32_t(0x04080301));
        QCOMPARE(wf->args[3].kind, Arg::Number);
        QCOMPARE(wf->args[3].num, uint32_t(0));
        QCOMPARE(wf->args[4].kind, Arg::Blob);
        QVERIFY(wf->args[4].blob.empty());
        // the message after it must still be intact
        QVERIFY(p.messages.size() > 20);
    }

    void serverStreamParsesCompletely()
    {
        const auto s = readFixture("server_stream.bin");
        const Parsed p = parseWhole(s);
        QVERIFY(!p.failed);
        QCOMPARE(p.greetings, 1);
        QCOMPARE(int(p.messages.size()), 209);
        size_t total = 5;
        for (const auto& r : p.raw) total += r.size();
        QCOMPARE(total, s.size());

        std::map<uint16_t, int> hist;
        for (const auto& m : p.messages) ++hist[m.type];
        QCOMPARE(hist[0x4000], 27);
        QCOMPARE(hist[0x4001], 24);
        QCOMPARE(hist[0x4101], 124);
        QCOMPARE(hist[0x4201], 24);
        QCOMPARE(hist[0x4402], 3);
        QCOMPARE(hist[0x4502], 2);
        QCOMPARE(hist[0x4602], 2);
        QCOMPARE(hist[0x4a02], 1);
        QCOMPARE(hist[0x4b02], 1);
        QCOMPARE(hist[0x4e02], 1);
    }

    void roundTripClientStream()
    {
        const Parsed p = parseWhole(readFixture("client_stream.bin"));
        QVERIFY(!p.failed);
        for (size_t i = 0; i < p.messages.size(); ++i) {
            const auto enc = encode(p.messages[i]);
            QVERIFY2(enc == p.raw[i], qPrintable(QStringLiteral("message %1: %2").arg(i).arg(QString::fromStdString(describe(p.messages[i])))));
        }
    }

    void roundTripServerStream()
    {
        const Parsed p = parseWhole(readFixture("server_stream.bin"));
        QVERIFY(!p.failed);
        for (size_t i = 0; i < p.messages.size(); ++i) {
            const auto enc = encode(p.messages[i]);
            QVERIFY2(enc == p.raw[i], qPrintable(QStringLiteral("message %1: %2").arg(i).arg(QString::fromStdString(describe(p.messages[i])))));
        }
    }

    void byteByByteGivesSameMessages()
    {
        for (const char* name : {"client_stream.bin", "server_stream.bin"}) {
            const auto   s = readFixture(name);
            const Parsed whole = parseWhole(s);
            const Parsed bytes = parseAll(s, [] { return size_t(1); });
            QVERIFY(!bytes.failed);
            QVERIFY(whole.messages == bytes.messages);
            QCOMPARE(bytes.greetings, 1);
        }
    }

    void randomChunksGiveSameMessages()
    {
        std::srand(12345);
        for (const char* name : {"client_stream.bin", "server_stream.bin"}) {
            const auto   s = readFixture(name);
            const Parsed whole = parseWhole(s);
            for (int round = 0; round < 20; ++round) {
                const Parsed chunked = parseAll(s, [] { return size_t(1 + std::rand() % 1500); });
                QVERIFY(!chunked.failed);
                QVERIFY(whole.messages == chunked.messages);
            }
        }
    }

    void truncatedStreamNeverFailsAndNeverYieldsPartial()
    {
        const auto s = readFixture("client_stream.bin");
        const Parsed whole = parseWhole(s);
        for (size_t cut : {size_t(6), size_t(20), size_t(100), size_t(1000), s.size() - 1}) {
            std::vector<uint8_t> t(s.begin(), s.begin() + cut);
            const Parsed p = parseWhole(t);
            QVERIFY(!p.failed);
            QVERIFY(p.messages.size() <= whole.messages.size());
            for (size_t i = 0; i < p.messages.size(); ++i) QVERIFY(p.messages[i] == whole.messages[i]);
        }
    }

    void garbageSetsFailed()
    {
        const std::vector<uint8_t> junk = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
        const Parsed p = parseWhole(junk);
        QVERIFY(p.failed);

        // good magic then a bad txid tag
        std::vector<uint8_t> bad = {0x11, 0x87, 0x23, 0x49, 0xae, 0x99, 0, 0, 0, 1};
        StreamParser sp;
        sp.feed(bad.data(), bad.size());
        Message m;
        QCOMPARE(sp.next(m), StreamParser::Error);
        QVERIFY(sp.failed());
        QCOMPARE(sp.errorOffset(), size_t(5));
        QCOMPARE(sp.next(m), StreamParser::Error);      // stays failed
    }

    void hugeLengthsAreRejected()
    {
        // message with one string argument claiming 0x7fffffff characters
        std::vector<uint8_t> b = {0x11, 0x87, 0x23, 0x49, 0xae, 0x11, 0, 0, 0, 1, 0x10, 0x30, 0x00, 0x0f, 0x01,
                                  0x14, 0, 0, 0, 0x0c, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0x26, 0x7f, 0xff, 0xff, 0xff};
        StreamParser sp;
        sp.feed(b.data(), b.size());
        Message m;
        QCOMPARE(sp.next(m), StreamParser::Error);

        // blob with a huge length
        std::vector<uint8_t> c = {0x11, 0x87, 0x23, 0x49, 0xae, 0x11, 0, 0, 0, 1, 0x10, 0x30, 0x00, 0x0f, 0x02,
                                  0x14, 0, 0, 0, 0x0c, 0x06, 0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0x11, 0x10, 0x00, 0x00, 0x00, 0x14, 0xff, 0xff, 0xff, 0xff};
        StreamParser sp2;
        sp2.feed(c.data(), c.size());
        QCOMPARE(sp2.next(m), StreamParser::Error);
    }

    void buildAndParseMessage()
    {
        Message m;
        m.txid = 7;
        m.type = 0x4101;
        m.args = {Arg::number(1), Arg::number(2), Arg::number(14), Arg::stringUtf8("Zoë Johnston"), Arg::number(0),
                  Arg::string(u""), Arg::number(4), Arg::blobArg({})};
        // blob after a nonzero length number is not valid here: blob arg is preceded by number 4 != 0 size
        QVERIFY_EXCEPTION_THROWN(encode(m), std::invalid_argument);
        m.args.back() = Arg::blobArg({1, 2, 3, 4});
        const auto enc = encode(m);
        StreamParser p;
        p.feed(enc.data(), enc.size());
        Message out;
        QCOMPARE(p.next(out), StreamParser::MessageReady);
        QVERIFY(out == m);
        QCOMPARE(QString::fromStdString(utf16ToUtf8(out.args[3].str)), QStringLiteral("Zoë Johnston"));
    }

    void tooManyArgsThrows()
    {
        Message m;
        for (int i = 0; i < 13; ++i) m.args.push_back(Arg::number(i));
        QVERIFY_EXCEPTION_THROWN(encode(m), std::invalid_argument);
    }

    void greetingBytes()
    {
        const auto g = encodeGreeting();
        QCOMPARE(int(g.size()), 5);
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(g.data()), 5), QByteArray::fromHex("1100000001"));
    }

    void portQuery()
    {
        QVERIFY(portQueryRequest() == readFixture("port_query_req.bin"));
        QVERIFY(portQueryReply(1051) == readFixture("port_query_resp.bin"));
        const auto q = readFixture("port_query_req.bin");
        QVERIFY(isPortQueryRequest(q.data(), q.size()));
        QVERIFY(!isPortQueryRequest(q.data(), q.size() - 1));
    }

    void utf16Conversions()
    {
        const std::string s = "Zoë \xF0\x9F\x8E\xA7 end";
        QCOMPARE(utf16ToUtf8(utf8ToUtf16(s)), s);
    }

    void describeIsReadable()
    {
        Message m;
        m.txid = 3;
        m.type = 0x3000;
        m.args = {Arg::number(0x04010301), Arg::stringUtf8("abc")};
        QCOMPARE(QString::fromStdString(describe(m)),
                 QStringLiteral("tx=0x00000003 type=0x3000 [#1 0x4010301 #2 \"abc\"]"));
    }
};

QTEST_APPLESS_MAIN(TestDbCodec)
#include "test_db_codec.moc"
