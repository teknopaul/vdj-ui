#ifndef DB_HANDLER_H
#define DB_HANDLER_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "db_codec.h"
#include "rb_library.h"

// Answers dbserver requests from an RbLibrary. Qt-free. One Session per TCP connection holds the result of the last
// menu / metadata request, which the client then pages through with "render menu" (type 0x3000) requests.
//
// Item layouts and constants come from the captured replies of a real deck, see tests/fixtures/dbserver/REPLY_LAYOUTS.md.
// Anything not seen from a real device is marked "GUESS" in db_handler.cpp.

namespace vdj {
namespace db {

// Request types (see menus.html / track_metadata.html and REPLY_LAYOUTS.md).
enum RequestType : uint16_t {
    ReqSetup = 0x0000, ReqDisconnect = 0x0100,
    ReqRootMenu = 0x1000, ReqGenreMenu = 0x1001, ReqArtistMenu = 0x1002, ReqAlbumMenu = 0x1003, ReqTrackMenu = 0x1004,
    ReqLabelMenu = 0x100a, ReqColorMenu = 0x100d, ReqHistoryMenu = 0x1012, ReqKeyMenu = 0x1014,
    ReqArtistsForGenre = 0x1101, ReqAlbumsForArtist = 0x1102, ReqTracksForAlbum = 0x1103, ReqTracksForRating = 0x1107,
    ReqArtistsForLabel = 0x110a, ReqTracksForColor = 0x110d, ReqTracksForHistory = 0x1112, ReqPlaylist = 0x1105,
    ReqAlbumsForGenreArtist = 0x1201, ReqTracksForArtistAlbum = 0x1202, ReqSearch = 0x1300, ReqFolderMenu = 0x2006,
    ReqMetadata = 0x2002, ReqTrackInfo = 0x2102,      // 0x2102: undocumented, carries the audio file path
    ReqRender = 0x3000,
    ReqUnknown3e03 = 0x3e03, ReqUnknown3d03 = 0x3d03, ReqUnknown3100 = 0x3100,
    ReqArt = 0x2003, ReqWaveformPreview = 0x2004, ReqBeatGrid = 0x2204, ReqWaveformDetail = 0x2904,
    ReqAnalysisTag = 0x2c04, ReqBasicCues = 0x2104, ReqExtendedCues = 0x2b04, ReqUnknown2504 = 0x2504
};

// Reply types
enum ReplyType : uint16_t {
    RepOk = 0x4000, RepMenuHeader = 0x4001, RepMenuItem = 0x4101, RepMenuFooter = 0x4201, RepArt = 0x4002,
    RepWaveformPreview = 0x4402, RepWaveformDetail = 0x4a02, RepBeatGrid = 0x4602, RepBasicCues = 0x4702,
    RepExtendedCues = 0x4e02, RepAnalysisTag = 0x4f02, RepUnknown3e03 = 0x4b02, RepUnknown2504 = 0x4502
};

// Item type codes of 4101 messages (arg 7)
enum ItemType : uint32_t {
    ItemFile = 0x00, ItemFolder = 0x01, ItemAlbum = 0x02, ItemTitle = 0x04, ItemGenre = 0x06, ItemArtist = 0x07,
    ItemPlaylist = 0x08, ItemRating = 0x0a, ItemDuration = 0x0b, ItemTempo = 0x0d, ItemLabel = 0x0e, ItemKey = 0x0f,
    ItemColorNone = 0x13, ItemComment = 0x23, ItemDateAdded = 0x2e, ItemUnknown2f = 0x2f,
    ItemMenuArtist = 0x81, ItemMenuAlbum = 0x82, ItemMenuTrack = 0x83, ItemMenuPlaylist = 0x84, ItemMenuKey = 0x8b,
    ItemMenuSearch = 0x91, ItemMenuHistory = 0x95
};

struct Label {
    std::u16string s;
    char16_t       term = 0;        // the final character on the wire
};

struct MenuItem {
    uint32_t a1 = 0, a2 = 0;
    Label    label1, label2;
    uint32_t type = 0, flags = 0, artwork = 0, position = 0, a11 = 0, a12 = 0;
};

struct Session {
    uint32_t              requester = 0;
    // result of the last menu / metadata request, per menu byte of the DMST: the deck renders a playlist (M=03) after
    // other requests (M=08, M=01) in between, so one result set is not enough (seen in the capture)
    std::map<uint32_t, std::vector<MenuItem>> sets;
    uint16_t              lastType = 0;
    uint64_t              requests = 0;
};

enum class UnknownPolicy { Ignore, EmptyOk, Close };

struct HandlerResult {
    enum Kind { Handled, Unimplemented, Unknown } kind = Handled;
    std::vector<Message> replies;
    bool                 close = false;
};

class DbHandler {
public:
    explicit DbHandler(const RbLibrary& lib, uint32_t deviceNumber = 17) : lib_(lib), device_(deviceNumber) {}

    void setUnknownPolicy(UnknownPolicy p) { policy_ = p; }
    uint32_t deviceNumber() const { return device_; }

    HandlerResult handle(const Message& req, Session& s) const;

private:
    HandlerResult ok(const Message& req, uint32_t count) const;
    HandlerResult render(const Message& req, Session& s) const;
    HandlerResult unknown(const Message& req) const;

    // item factories
    MenuItem titleItem(const RbTrack& t, uint32_t position = 0) const;
    std::vector<MenuItem> trackItems(const std::vector<uint32_t>& ids, bool withPosition = false) const;
    std::vector<MenuItem> metadataItems(const RbTrack& t) const;
    std::vector<MenuItem> infoItems(const RbTrack& t) const;
    std::vector<MenuItem> rootItems() const;

    const RbLibrary& lib_;
    uint32_t         device_;
    UnknownPolicy    policy_ = UnknownPolicy::EmptyOk;
};

// "<id> <name>" style helpers shared with tests
Label makeLabel(const std::string& utf8);
Message itemMessage(uint32_t txid, const MenuItem& it);

} // namespace db
} // namespace vdj

#endif // DB_HANDLER_H
