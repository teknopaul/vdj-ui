#include <QtTest>

#include <algorithm>
#include <map>
#include <set>

#include "dbserver/db_handler.h"

using namespace vdj::db;

namespace {

std::vector<uint8_t> readFixture(const QString& name)
{
    QFile f(QStringLiteral(VDJ_DBSERVER_FIXTURES "/") + name);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray b = f.readAll();
    return std::vector<uint8_t>(b.begin(), b.end());
}

std::vector<Message> parseStream(const std::vector<uint8_t>& s)
{
    std::vector<Message> out;
    StreamParser p;
    p.feed(s.data(), s.size());
    for (;;) {
        Message m;
        auto st = p.next(m);
        if (st == StreamParser::MessageReady) out.push_back(std::move(m));
        else if (st != StreamParser::Greeting) break;
    }
    return out;
}

Message req(uint16_t type, std::vector<Arg> args, uint32_t txid = 1)
{
    Message m;
    m.txid = txid;
    m.type = type;
    m.args = std::move(args);
    return m;
}

Arg N(uint32_t n) { return Arg::number(n); }

// Compare our reply with the deck's, ignoring bytes whose meaning is unknown and that we do not reproduce (see db_blobs.cpp)
bool sameReply(const Message& ours, const Message& deck)
{
    if (ours.txid != deck.txid || ours.type != deck.type || ours.args.size() != deck.args.size()) return false;
    for (size_t i = 0; i < ours.args.size(); ++i) {
        const Arg& a = ours.args[i];
        const Arg& b = deck.args[i];
        if (ours.type == 0x4602 && i == 4) continue;          // the deck sends junk here (0 and 0xffffff5a seen)
        if (a.kind != Arg::Blob || b.kind != Arg::Blob) {
            if (!(a == b)) return false;
            continue;
        }
        if (a.blob.size() != b.blob.size()) return false;
        if (ours.type == 0x4502) continue;                                   // undocumented, content unknown
        std::vector<uint8_t> x = a.blob, y = b.blob;
        if (ours.type == 0x4602 || ours.type == 0x4a02) {
            for (int k = 16; k < 20; ++k) x[k] = y[k] = 0;                  // heap pointer in the deck's head
        } else if (ours.type == 0x4e02) {
            for (size_t e = 0; e + 76 <= x.size(); e += 76)
                for (size_t k : {size_t(8), size_t(9), size_t(20), size_t(21), size_t(22), size_t(23), size_t(28), size_t(29), size_t(30), size_t(31)})
                    x[e + k] = y[e + k] = 0;
            for (size_t e = 0; e + 76 <= x.size(); e += 76)                  // unknown fill 0x20..0x33 and the tail
                for (size_t k = 32; k < 52; ++k) x[e + k] = y[e + k] = 0;
        }
        if (x != y) return false;
    }
    return true;
}

uint32_t dmst(uint8_t player, uint8_t menu, uint8_t slot, uint8_t type) { return (player << 24) | (menu << 16) | (slot << 8) | type; }

} // namespace

class TestDbHandler : public QObject {
    Q_OBJECT

private:
    RbLibrary lib7_;       // the 7 track pendrive the captured deck served
    RbLibrary libBig_;     // the bigger corpus export

    std::vector<Message> render(const DbHandler& h, Session& s, uint32_t d, uint32_t offset, uint32_t limit, uint32_t total)
    {
        return h.handle(req(ReqRender, {N(d), N(offset), N(limit), N(0), N(total), N(0)}), s).replies;
    }

private slots:
    void initTestCase()
    {
        std::string err;
        QVERIFY2(lib7_.load(QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7").toStdString(), &err), err.c_str());
        QVERIFY2(libBig_.load(QStringLiteral(VDJ_DBSERVER_FIXTURES "/export").toStdString(), &err), err.c_str());
        QCOMPARE(int(lib7_.trackCount()), 7);
    }

    void setupReplyHasOurDeviceNumber()
    {
        DbHandler h(lib7_, 17);
        Session s;
        auto r = h.handle(req(ReqSetup, {N(4)}, kSetupTxid), s);
        QCOMPARE(int(r.replies.size()), 1);
        QCOMPARE(r.replies[0].type, uint16_t(RepOk));
        QCOMPARE(r.replies[0].txid, kSetupTxid);
        QCOMPARE(r.replies[0].args[0].num, uint32_t(0));
        QCOMPARE(r.replies[0].args[1].num, uint32_t(17));
        QCOMPARE(s.requester, uint32_t(4));
        QVERIFY(h.handle(req(ReqDisconnect, {}, kSetupTxid), s).close);
    }

    void rootMenuShapeMatchesDeck()
    {
        DbHandler h(lib7_, 1);
        Session s;
        auto r = h.handle(req(ReqRootMenu, {N(dmst(4, 1, 3, 1)), N(0), N(0xffffff)}), s);
        QCOMPARE(r.replies[0].args[1].num, uint32_t(8));
        auto items = render(h, s, dmst(4, 1, 3, 1), 0, 7, 8);
        QCOMPARE(int(items.size()), 9);         // header + 7 + footer
        QCOMPARE(items[1].args[6].num, uint32_t(0x81));
        QCOMPARE(items[7].args[6].num, uint32_t(0x91));
    }

    // Replay the captured client stream against the library that the captured server stream was served from. For every
    // request we handle, the reply must equal the deck's, message for message, except the documented divergences.
    void replayCapturedSessionMatchesDeckReplies()
    {
        const auto client = parseStream(readFixture("client_stream.bin"));
        const auto server = parseStream(readFixture("server_stream.bin"));
        QCOMPARE(int(client.size()), 61);

        DbHandler h(lib7_, 1);
        Session s;
        int exact = 0, setCompared = 0, skipped = 0;
        QStringList skippedTypes;
        for (const Message& rq : client) {
            const HandlerResult hr = h.handle(rq, s);
            std::vector<Message> expect;
            for (const Message& m : server)
                if (m.txid == rq.txid) expect.push_back(m);

            static const std::vector<uint16_t> phase4 = {0x3100};     // seen from decks, meaning unknown
            if (std::find(phase4.begin(), phase4.end(), rq.type) != phase4.end()) {
                ++skipped;
                skippedTypes << QString::number(rq.type, 16);
                continue;
            }
            // the deck's track order for sort 0 is unexplained: compare as a set of item ids
            const bool trackListOrder = rq.type == ReqRender && s.lastType == ReqRender && expect.size() > 3 &&
                                        expect[1].type == RepMenuItem && expect[1].args[6].num == ItemTitle &&
                                        expect[1].args[9].num == 0 && allPlainTitles(expect);
            QString what = QStringLiteral("request type %1 tx %2").arg(rq.type, 4, 16, QLatin1Char('0')).arg(rq.txid, 0, 16);
            if (trackListOrder && hr.replies.size() == expect.size()) {
                std::multiset<uint32_t> a, b;
                for (size_t i = 1; i + 1 < expect.size(); ++i) { a.insert(expect[i].args[1].num); b.insert(hr.replies[i].args[1].num); }
                QVERIFY2(a == b, qPrintable(what));
                ++setCompared;
                continue;
            }
            QVERIFY2(hr.replies.size() == expect.size(), qPrintable(what + QStringLiteral(": reply count %1 vs %2").arg(hr.replies.size()).arg(expect.size())));
            for (size_t i = 0; i < expect.size(); ++i)
                QVERIFY2(sameReply(hr.replies[i], expect[i]), qPrintable(what + QStringLiteral(" reply %1\n ours: %2\n deck: %3").arg(i)
                                                       .arg(QString::fromStdString(describe(hr.replies[i])), QString::fromStdString(describe(expect[i])))));
            ++exact;
        }
        qInfo() << "exact" << exact << "compared as sets" << setCompared << "skipped (phase 4 / unknown)" << skipped << skippedTypes;
        QVERIFY(exact >= 58);
        QVERIFY(setCompared <= 2);       // only the sort 0 track list (the deck's order is unexplained)
    }

    void binaryRepliesForAnalysedAndMissingTracks()
    {
        DbHandler h(libBig_, 17);
        Session s;
        const uint32_t d = dmst(4, 8, 3, 1);

        auto grid = h.handle(req(ReqBeatGrid, {N(d), N(1935)}), s).replies;       // 716 beats
        QCOMPARE(grid[0].type, uint16_t(RepBeatGrid));
        QCOMPARE(grid[0].args[2].num, uint32_t(20 + 716 * 16));
        QCOMPARE(grid[0].args[3].blob.size(), size_t(20 + 716 * 16));
        QCOMPARE(grid[0].args[4].num, uint32_t(0));

        auto wf = h.handle(req(ReqWaveformPreview, {N(d), N(3), N(1935), N(0), Arg::blobArg({})}), s).replies;
        QCOMPARE(wf[0].args[2].num, uint32_t(900));
        auto det = h.handle(req(ReqWaveformDetail, {N(dmst(4, 1, 3, 1)), N(1935), N(0)}), s).replies;
        QVERIFY(det[0].args[2].num > 20);
        QCOMPARE(det[0].args[3].blob.size(), size_t(det[0].args[2].num));
        auto cues = h.handle(req(ReqExtendedCues, {N(d), N(1935), N(0)}), s).replies;
        QVERIFY(cues[0].args[4].num >= 1);
        QCOMPARE(cues[0].args[3].blob.size(), size_t(76 * cues[0].args[4].num));

        // track the rekordbox did not analyse, a track without files, an unknown track: empty blobs, never errors
        for (uint32_t id : {1372u, 2u, 0xfffffff0u}) {
            auto g = h.handle(req(ReqBeatGrid, {N(d), N(id)}), s).replies;
            QCOMPARE(g[0].args[2].num, uint32_t(0));
            QVERIFY(g[0].args[3].blob.empty());
            auto c = h.handle(req(ReqExtendedCues, {N(d), N(id), N(0)}), s).replies;
            QCOMPARE(c[0].args[4].num, uint32_t(0));
        }
        // the encoded empty replies parse back
        auto bytes = encode(h.handle(req(ReqBeatGrid, {N(d), N(2)}), s).replies[0]);
        StreamParser p;
        p.feed(bytes.data(), bytes.size());
        Message back;
        QCOMPARE(p.next(back), StreamParser::MessageReady);
    }

    void artworkReply()
    {
        DbHandler h(libBig_, 17);
        Session s;
        auto r = h.handle(req(ReqArt, {N(dmst(4, 8, 3, 1)), N(34)}), s).replies;
        QCOMPARE(r[0].type, uint16_t(RepArt));
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(r[0].args[3].blob.data()), int(r[0].args[3].blob.size())), QByteArray("FAKE-ARTWORK-34\n"));
        r = h.handle(req(ReqArt, {N(dmst(4, 8, 3, 1)), N(999999)}), s).replies;
        QCOMPARE(r[0].args[2].num, uint32_t(0));
    }

    void metadataOfUnknownTrack()
    {
        DbHandler h(lib7_, 1);
        Session s;
        auto r = h.handle(req(ReqMetadata, {N(dmst(4, 1, 3, 1)), N(9999)}), s);
        QCOMPARE(r.replies[0].args[1].num, uint32_t(0xffffffff));
        r = h.handle(req(ReqTrackInfo, {N(dmst(4, 8, 3, 1)), N(9999)}), s);
        QCOMPARE(r.replies[0].args[1].num, uint32_t(0xffffffff));
    }

    void trackInfoCarriesAudioPathAndSize()
    {
        DbHandler h(lib7_, 1);
        Session s;
        h.handle(req(ReqTrackInfo, {N(dmst(4, 8, 3, 1)), N(3)}), s);
        auto items = render(h, s, dmst(4, 8, 3, 1), 0, 6, 6);
        const Message& file = items[5];
        QCOMPARE(file.args[0].num, uint32_t(7682216));
        QCOMPARE(QString::fromStdString(utf16ToUtf8(file.args[3].str)),
                 QStringLiteral("/Contents/Intapol/UK Dembow/uk-dembo-whistling-diesel-track-06.mp3"));
    }

    void pagingConcatenatesToTheSameItems()
    {
        DbHandler h(libBig_, 17);
        Session s;
        auto r = h.handle(req(ReqTrackMenu, {N(dmst(4, 2, 3, 1)), N(0)}), s);
        const uint32_t n = r.replies[0].args[1].num;
        QCOMPARE(size_t(n), libBig_.trackCount());
        auto all = render(h, s, dmst(4, 2, 3, 1), 0, n, n);
        QCOMPARE(size_t(all.size()), size_t(n) + 2);
        std::vector<Message> paged;
        for (uint32_t off = 0; off < n; off += 64) {
            auto page = render(h, s, dmst(4, 2, 3, 1), off, 64, n);
            QCOMPARE(page.front().args[1].num, off);
            paged.insert(paged.end(), page.begin() + 1, page.end() - 1);
        }
        QCOMPARE(paged.size(), size_t(n));
        for (size_t i = 0; i < paged.size(); ++i) QVERIFY(paged[i] == all[i + 1]);
        // offset beyond the end: header and footer only
        auto beyond = render(h, s, dmst(4, 2, 3, 1), n + 10, 64, n);
        QCOMPARE(int(beyond.size()), 2);
    }

    void placeholderLabelsForMenuByte3()
    {
        DbHandler h(lib7_, 1);
        Session s;
        h.handle(req(ReqPlaylist, {N(dmst(4, 3, 3, 1)), N(0), N(1), N(0)}), s);
        auto items = render(h, s, dmst(4, 3, 3, 1), 2, 1, 7);
        QCOMPARE(int(items.size()), 3);
        QCOMPARE(items[0].args[1].num, uint32_t(2));            // header echoes the offset
        QCOMPARE(items[1].args[1].num, uint32_t(3));
        QCOMPARE(int(items[1].args[3].term), 9);
    }

    void metadataValuesComeFromThePdb()
    {
        DbHandler h(libBig_, 17);
        Session s;
        const RbTrack* t = libBig_.track(1935);
        auto r = h.handle(req(ReqMetadata, {N(dmst(4, 1, 3, 1)), N(1935)}), s);
        QCOMPARE(r.replies[0].args[1].num, uint32_t(11));
        auto items = render(h, s, dmst(4, 1, 3, 1), 0, 11, 11);
        QCOMPARE(QString::fromStdString(utf16ToUtf8(items[1].args[3].str)), QString::fromStdString(t->title));
        QCOMPARE(items[4].args[1].num, uint32_t(t->durationSecs));
        QCOMPARE(items[5].args[1].num, t->bpmX100);
        QCOMPARE(items[8].args[1].num, uint32_t(t->rating));
    }

    void filteredMenus()
    {
        DbHandler h(libBig_, 17);
        Session s;
        const RbTrack* t = libBig_.track(1935);
        auto r = h.handle(req(ReqAlbumsForArtist, {N(dmst(4, 2, 3, 1)), N(0), N(t->artistId)}), s);
        QVERIFY(r.replies[0].args[1].num >= 1);
        r = h.handle(req(ReqTracksForAlbum, {N(dmst(4, 2, 3, 1)), N(0), N(t->albumId)}), s);
        auto items = render(h, s, dmst(4, 2, 3, 1), 0, 64, r.replies[0].args[1].num);
        bool found = false;
        for (size_t i = 1; i + 1 < items.size(); ++i) found = found || items[i].args[1].num == 1935;
        QVERIFY(found);
        r = h.handle(req(ReqSearch, {N(dmst(4, 2, 3, 1)), N(0), N(8), Arg::stringUtf8("BABY IT'S ME"), N(0)}), s);
        QVERIFY(r.replies[0].args[1].num >= 1);
        QCOMPARE(h.handle(req(0x1006, {N(dmst(4, 2, 3, 1)), N(0)}), s).kind, HandlerResult::Unimplemented);
    }

    void unknownRequestFollowsPolicy()
    {
        DbHandler h(lib7_, 1);
        Session s;
        auto r = h.handle(req(0x7777, {N(1), N(2)}), s);
        QCOMPARE(r.kind, HandlerResult::Unknown);
        QCOMPARE(int(r.replies.size()), 1);
        QCOMPARE(r.replies[0].args[0].num, uint32_t(0x7777));
        h.setUnknownPolicy(UnknownPolicy::Ignore);
        QVERIFY(h.handle(req(0x7777, {}), s).replies.empty());
        h.setUnknownPolicy(UnknownPolicy::Close);
        QVERIFY(h.handle(req(0x7777, {}), s).close);
    }

    void everyReplyEncodes()
    {
        // every reply message produced for the captured session can be serialised and parsed back
        DbHandler h(lib7_, 1);
        Session s;
        for (const Message& rq : parseStream(readFixture("client_stream.bin")))
            for (const Message& m : h.handle(rq, s).replies) {
                auto bytes = encode(m);
                StreamParser p;
                p.feed(bytes.data(), bytes.size());
                Message back;
                QCOMPARE(p.next(back), StreamParser::MessageReady);
                QVERIFY(back == m);
            }
    }

private:
    static bool allPlainTitles(const std::vector<Message>& v)
    {
        for (size_t i = 1; i + 1 < v.size(); ++i)
            if (v[i].type != RepMenuItem || v[i].args[9].num != 0 || v[i].args[6].num != ItemTitle) return false;
        return true;
    }
};

QTEST_APPLESS_MAIN(TestDbHandler)
#include "test_db_handler.moc"
