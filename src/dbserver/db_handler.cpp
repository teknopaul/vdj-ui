#include "db_handler.h"

#include "db_blobs.h"

#include <algorithm>

namespace vdj {
namespace db {

namespace {

constexpr uint32_t kTitleFlags = 0x01000000;    // arg 8 of every track title item in the captures
constexpr uint32_t kTitleA11 = 0x100;           // arg 11 of every track title item in the captures
constexpr char16_t kPlaceholder = 0x0009;       // label of every item when the menu byte of the DMST is 03
constexpr uint32_t kNoSuchItem = 0xffffffff;

uint32_t menuByte(uint32_t dmst) { return (dmst >> 16) & 0xff; }

uint32_t num(const Message& m, size_t i) { return i < m.args.size() && m.args[i].kind == Arg::Number ? m.args[i].num : 0; }

Message reply(const Message& req, uint16_t type, std::vector<Arg> args)
{
    Message m;
    m.txid = req.txid;
    m.type = type;
    m.args = std::move(args);
    return m;
}

Message binaryReply(const Message& req, uint16_t type, const std::vector<uint8_t>& blob, std::vector<Arg> extra = {})
{
    // (request type, 0, blob length, blob [omitted when empty], extras...)
    std::vector<Arg> a = {Arg::number(req.type), Arg::number(0), Arg::number(uint32_t(blob.size())), Arg::blobArg(blob)};
    a.insert(a.end(), extra.begin(), extra.end());
    return reply(req, type, std::move(a));
}

MenuItem plain(uint32_t a1, uint32_t a2, const std::string& label, uint32_t type)
{
    MenuItem it;
    it.a1 = a1;
    it.a2 = a2;
    it.label1 = makeLabel(label);
    it.type = type;
    return it;
}

Label menuName(const char* name)
{
    Label l;
    l.s = u"￺" + utf8ToUtf16(name) + u"￻";
    return l;
}

} // namespace

Label makeLabel(const std::string& utf8)
{
    Label l;
    l.s = utf8ToUtf16(utf8);
    return l;
}

Message itemMessage(uint32_t txid, const MenuItem& it)
{
    Message m;
    m.txid = txid;
    m.type = RepMenuItem;
    m.args = {Arg::number(it.a1), Arg::number(it.a2), Arg::number(uint32_t((it.label1.s.size() + 1) * 2)),
              Arg::string(it.label1.s), Arg::number(uint32_t((it.label2.s.size() + 1) * 2)), Arg::string(it.label2.s),
              Arg::number(it.type), Arg::number(it.flags), Arg::number(it.artwork), Arg::number(it.position),
              Arg::number(it.a11), Arg::number(it.a12)};
    m.args[3].term = it.label1.term;
    m.args[5].term = it.label2.term;
    return m;
}

// ---------------------------------------------------------------------------------------------------------------------

HandlerResult DbHandler::ok(const Message& req, uint32_t count) const
{
    HandlerResult r;
    r.replies.push_back(reply(req, RepOk, {Arg::number(req.type), Arg::number(count)}));
    return r;
}

HandlerResult DbHandler::unknown(const Message& req) const
{
    HandlerResult r;
    r.kind = HandlerResult::Unknown;
    switch (policy_) {
    case UnknownPolicy::Ignore: break;
    case UnknownPolicy::Close:  r.close = true; break;
    case UnknownPolicy::EmptyOk: r.replies.push_back(reply(req, RepOk, {Arg::number(req.type), Arg::number(0)})); break;
    }
    return r;
}

MenuItem DbHandler::titleItem(const RbTrack& t, uint32_t position) const
{
    MenuItem it = plain(0, t.id, t.title, ItemTitle);
    it.flags = kTitleFlags;
    it.artwork = t.artworkId;
    it.position = position;
    it.a11 = kTitleA11;
    return it;
}

std::vector<MenuItem> DbHandler::trackItems(const std::vector<uint32_t>& ids, bool withPosition) const
{
    std::vector<MenuItem> v;
    uint32_t pos = 0;
    for (uint32_t id : ids) {
        const RbTrack* t = lib_.track(id);
        if (!t) continue;
        v.push_back(titleItem(*t, withPosition ? ++pos : 0));
    }
    return v;
}

std::vector<MenuItem> DbHandler::rootItems() const
{
    // Seven entries, copied from a real deck's reply. The deck announced a count of 8 (GUESS: an eighth entry that it
    // did not render).
    struct Row { uint32_t id; const char* name; uint32_t type; };
    static const Row rows[] = {{2, "ARTIST", ItemMenuArtist}, {3, "ALBUM", ItemMenuAlbum}, {4, "TRACK", ItemMenuTrack},
                               {0xc, "KEY", ItemMenuKey}, {5, "PLAYLIST", ItemMenuPlaylist}, {0x16, "HISTORY", ItemMenuHistory},
                               {0x12, "SEARCH", ItemMenuSearch}};
    std::vector<MenuItem> v;
    for (const Row& r : rows) {
        MenuItem it;
        it.a2 = r.id;
        it.label1 = menuName(r.name);
        it.type = r.type;
        v.push_back(it);
    }
    return v;
}

std::vector<MenuItem> DbHandler::metadataItems(const RbTrack& t) const
{
    std::vector<MenuItem> v;
    MenuItem title = titleItem(t);
    title.a1 = t.artistId;
    v.push_back(title);
    v.push_back(plain(1, t.artistId, lib_.artistName(t.artistId), ItemArtist));
    v.push_back(plain(1, t.albumId, lib_.albumName(t.albumId), ItemAlbum));
    v.push_back(plain(0, t.durationSecs, "", ItemDuration));
    v.push_back(plain(0, t.bpmX100, "", ItemTempo));
    v.push_back(plain(0, t.id, t.comment, ItemComment));
    v.push_back(plain(1, t.keyId, lib_.keyName(t.keyId), ItemKey));
    v.push_back(plain(0, t.rating, "", ItemRating));
    v.push_back(plain(0, t.colorId, t.colorId ? lib_.colorName(t.colorId) : "", ItemColorNone + t.colorId));
    v.push_back(plain(0, t.genreId, lib_.genreName(t.genreId), ItemGenre));
    v.push_back(plain(0, t.id, t.dateAdded, ItemDateAdded));
    return v;
}

std::vector<MenuItem> DbHandler::infoItems(const RbTrack& t) const
{
    // 0x2102: what a player asks before loading a track. Item 5 carries the audio file's path and size.
    std::vector<MenuItem> v;
    v.push_back(plain(0, 1, "", ItemTitle));                 // constant 1 in every capture (GUESS: meaning)
    v.push_back(plain(0, t.durationSecs, "", ItemDuration));
    v.push_back(plain(0, t.bpmX100, "", ItemTempo));
    v.push_back(plain(0, t.id, t.comment, ItemComment));
    v.push_back(plain(t.fileSize, t.id, t.filePath, ItemFile));
    v.push_back(plain(0, 1, "", ItemUnknown2f));             // constant 1 in every capture (GUESS: file type)
    return v;
}

HandlerResult DbHandler::render(const Message& req, Session& s) const
{
    const uint32_t dmst = num(req, 0);
    const uint32_t offset = num(req, 1);
    const uint32_t limit = num(req, 2);
    const bool placeholders = menuByte(dmst) == 3;     // the deck only wants ids; every label is U+0009

    HandlerResult r;
    r.replies.push_back(reply(req, RepMenuHeader, {Arg::number(1), Arg::number(offset)}));
    const std::vector<MenuItem>& items = s.sets[menuByte(dmst)];
    const size_t total = items.size();
    for (size_t i = offset; i < total && i - offset < limit; ++i) {
        MenuItem it = items[i];
        if (placeholders) {
            it.label1.s.clear(); it.label1.term = kPlaceholder;
            it.label2.s.clear(); it.label2.term = kPlaceholder;
            it.position = 0;        // the captured M=03 items carry no playlist position
        }
        r.replies.push_back(itemMessage(req.txid, it));
    }
    r.replies.push_back(reply(req, RepMenuFooter, {}));
    return r;
}

HandlerResult DbHandler::handle(const Message& req, Session& s) const
{
    ++s.requests;
    const uint32_t dmst = num(req, 0);
    const int sort = int(num(req, 1));
    HandlerResult r;
    std::vector<MenuItem>& cur = s.sets[menuByte(dmst)];

    switch (req.type) {
    case ReqSetup:
        s.requester = num(req, 0);
        r.replies.push_back(reply(req, RepOk, {Arg::number(ReqSetup), Arg::number(device_)}));
        break;
    case ReqDisconnect:
        r.close = true;
        break;
    case ReqUnknown3e03:        // undocumented; the reply is constant in the capture
        r.replies.push_back(reply(req, RepUnknown3e03, {Arg::number(ReqUnknown3e03), Arg::number(0), Arg::number(2), Arg::string(u"")}));
        break;
    case ReqUnknown3d03:        // undocumented; always answered with a count of 0 in the capture
        return ok(req, 0);

    case ReqRootMenu:
        cur = rootItems();
        r = ok(req, 8);         // see rootItems()
        break;
    case ReqArtistMenu: {
        cur.clear();
        for (const RbNamed& a : lib_.artists()) cur.push_back(plain(0, a.id, a.name, ItemArtist));
        r = ok(req, uint32_t(cur.size()));
        break;
    }
    case ReqAlbumMenu: {
        cur.clear();
        for (const RbAlbum& a : lib_.albums()) cur.push_back(plain(0, a.id, a.name, ItemAlbum));
        r = ok(req, uint32_t(cur.size()));
        break;
    }
    case ReqGenreMenu: {
        cur.clear();
        for (const RbNamed& g : lib_.genres()) cur.push_back(plain(0, g.id, g.name, ItemGenre));
        r = ok(req, uint32_t(cur.size()));
        break;
    }
    case ReqLabelMenu: {
        cur.clear();
        for (const RbNamed& g : lib_.labels()) cur.push_back(plain(0, g.id, g.name, ItemLabel));
        r = ok(req, uint32_t(cur.size()));
        break;
    }
    case ReqColorMenu: {
        cur.clear();
        for (const RbNamed& c : lib_.colors()) cur.push_back(plain(0, c.id, c.name, ItemColorNone + c.id));
        r = ok(req, uint32_t(cur.size()));
        break;
    }
    case ReqKeyMenu: {
        // The deck listed 24 keys in id order (the Camelot wheel, alternating minor / major) although the pdb key table
        // of that export was empty, so the list is built in. GUESS: names after the first seven (seen) follow the wheel.
        cur.clear();
        std::vector<RbNamed> keys = lib_.keys();
        if (keys.empty()) {
            static const char* wheel[24] = {"Abm", "B", "Ebm", "F#", "Bbm", "Db", "Fm", "Ab", "Cm", "Eb", "Gm", "Bb",
                                            "Dm", "F", "Am", "C", "Em", "G", "Bm", "D", "F#m", "A", "Dbm", "E"};
            for (uint32_t i = 0; i < 24; ++i) keys.push_back({i + 1, wheel[i]});
        }
        std::sort(keys.begin(), keys.end(), [](const RbNamed& a, const RbNamed& b) { return a.id < b.id; });
        for (const RbNamed& k : keys) cur.push_back(plain(0, k.id, k.name, ItemKey));
        r = ok(req, uint32_t(cur.size()));
        break;
    }
    case ReqTrackMenu:
        // GUESS: sort order. A real deck's order for sort 0 was not title order and is unexplained; we sort as documented.
        cur = trackItems(lib_.allTracks(sort));
        r = ok(req, uint32_t(cur.size()));
        break;
    case ReqHistoryMenu:
    case ReqTracksForHistory:
    case ReqFolderMenu:
        cur.clear();
        r = ok(req, 0);
        break;

    case ReqArtistsForGenre: {
        cur.clear();
        for (const RbNamed& a : lib_.artistsForGenre(num(req, 2))) cur.push_back(plain(0, a.id, a.name, ItemArtist));
        r = ok(req, uint32_t(cur.size()));
        break;
    }
    case ReqAlbumsForArtist: {
        cur.clear();
        for (const RbAlbum& a : lib_.albumsForArtist(num(req, 2))) cur.push_back(plain(0, a.id, a.name, ItemAlbum));
        r = ok(req, uint32_t(cur.size()));
        break;
    }
    case ReqAlbumsForGenreArtist: {
        cur.clear();
        for (const RbAlbum& a : lib_.albumsForGenreArtist(num(req, 2), num(req, 3))) cur.push_back(plain(0, a.id, a.name, ItemAlbum));
        r = ok(req, uint32_t(cur.size()));
        break;
    }
    case ReqTracksForAlbum:
        cur = trackItems(lib_.tracksForAlbum(num(req, 2), sort));
        r = ok(req, uint32_t(cur.size()));
        break;
    case ReqTracksForRating:
        cur = trackItems(lib_.tracksForRating(num(req, 2), sort));
        r = ok(req, uint32_t(cur.size()));
        break;
    case ReqTracksForColor:
        cur = trackItems(lib_.tracksForColor(num(req, 2), sort));
        r = ok(req, uint32_t(cur.size()));
        break;
    case ReqTracksForArtistAlbum:
        cur = trackItems(lib_.tracksForArtistAlbum(num(req, 2), num(req, 3), sort));
        r = ok(req, uint32_t(cur.size()));
        break;
    case ReqArtistsForLabel:
        cur.clear();      // GUESS: no label to artist index in the model yet
        r = ok(req, 0);
        r.kind = HandlerResult::Unimplemented;
        break;

    case ReqPlaylist: {
        const uint32_t id = num(req, 2);
        const bool folder = num(req, 3) != 0;
        cur.clear();
        if (folder) {
            for (const RbPlaylistNode& n : lib_.playlistChildren(id))
                cur.push_back(plain(id, n.id, n.name, n.isFolder ? ItemFolder : ItemPlaylist));
        } else {
            cur = trackItems(lib_.playlistTracks(id), true);
        }
        r = ok(req, uint32_t(cur.size()));
        break;
    }
    case ReqSearch: {
        // args: DMST, sort, byte length of the search string, search string (upper case), 0
        std::string needle;
        for (const Arg& a : req.args)
            if (a.kind == Arg::String) needle = utf16ToUtf8(a.str);
        cur.clear();
        if (!needle.empty()) {
            for (const RbNamed& a : lib_.searchArtists(needle)) cur.push_back(plain(0, a.id, a.name, ItemArtist));
            for (const RbAlbum& a : lib_.searchAlbums(needle)) cur.push_back(plain(0, a.id, a.name, ItemAlbum));
            for (const MenuItem& it : trackItems(lib_.searchTracks(needle, SortTitle))) cur.push_back(it);
        }
        r = ok(req, uint32_t(cur.size()));      // GUESS: result item layout for search
        break;
    }

    case ReqMetadata: {
        const RbTrack* t = lib_.track(num(req, 1));
        cur.clear();
        if (!t) {
            r = ok(req, kNoSuchItem);
            break;
        }
        cur = metadataItems(*t);
        r = ok(req, uint32_t(cur.size()));
        break;
    }
    case ReqTrackInfo: {
        const RbTrack* t = lib_.track(num(req, 1));
        cur.clear();
        if (!t) {
            r = ok(req, kNoSuchItem);
            break;
        }
        cur = infoItems(*t);
        r = ok(req, uint32_t(cur.size()));
        break;
    }
    case ReqRender:
        r = render(req, s);
        break;

    case ReqArt:                // (DMST, artwork id [, size])
        r.replies.push_back(binaryReply(req, RepArt, lib_.artwork(num(req, 1))));
        break;
    case ReqBeatGrid: {         // (DMST, track id)
        std::vector<uint8_t> blob;
        if (auto a = lib_.analysis(num(req, 1))) {
            anlz_beatgrid_t g{};
            if (a->beatGrid(&g) && g.count > 0) blob = beatGridBlob(g);
        }
        r.replies.push_back(binaryReply(req, RepBeatGrid, blob, {Arg::number(0)}));
        break;
    }
    case ReqWaveformPreview: {  // (DMST, 3 or 4, track id, 0, blob omitted)
        std::vector<uint8_t> blob;
        if (auto a = lib_.analysis(num(req, 2))) blob = waveformPreviewBlob(*a);
        r.replies.push_back(binaryReply(req, RepWaveformPreview, blob));
        break;
    }
    case ReqWaveformDetail: {   // (DMST, track id, 0)
        std::vector<uint8_t> blob;
        if (auto a = lib_.analysis(num(req, 1))) blob = waveformDetailBlob(*a);
        r.replies.push_back(binaryReply(req, RepWaveformDetail, blob));
        break;
    }
    case ReqExtendedCues: {     // (DMST, track id, 0)
        std::vector<uint8_t> blob;
        uint32_t count = 0;
        if (auto a = lib_.analysis(num(req, 1))) {
            std::vector<anlz_cue_t> cues;
            if (a->cues(ANLZ_CUES_ALL, &cues)) {
                blob = extendedCuesBlob(cues);
                count = uint32_t(cues.size());
            }
        }
        r.replies.push_back(binaryReply(req, RepExtendedCues, blob, {Arg::number(count)}));
        break;
    }
    case ReqUnknown2504:        // undocumented; the deck answered 1604 bytes, nearly all zero. GUESS: send zeros
        r.replies.push_back(binaryReply(req, RepUnknown2504, std::vector<uint8_t>(1604, 0)));
        break;

    // documented binary requests we cannot answer yet: well-formed empty replies
    case ReqBasicCues:
        r.kind = HandlerResult::Unimplemented;      // GUESS: shape of the empty reply
        r.replies.push_back(binaryReply(req, RepBasicCues, {}, {Arg::number(0x24), Arg::number(0), Arg::number(0), Arg::number(0), Arg::blobArg({})}));
        break;
    case ReqAnalysisTag:
        r.kind = HandlerResult::Unimplemented;
        r.replies.push_back(binaryReply(req, RepAnalysisTag, {}, {Arg::number(0)}));
        break;

    // documented menus the model cannot answer yet: empty list, reported as Unimplemented so it is logged
    case 0x1006: case 0x1007: case 0x1008: case 0x1010: case 0x1011: case 0x1013: case 0x1114: case 0x1206:
    case 0x1208: case 0x120a: case 0x1214: case 0x1301: case 0x130a: case 0x1302: case 0x1402: case 0x1502:
    case 0x1602: case 0x1702: case 0x1802:
        cur.clear();
        r = ok(req, 0);
        r.kind = HandlerResult::Unimplemented;
        break;

    default:                    // includes 0x3100 (seen from decks, meaning unknown) and the binary requests of phase 4
        r = unknown(req);
        break;
    }
    s.lastType = req.type;
    (void)dmst;
    return r;
}

} // namespace db
} // namespace vdj
