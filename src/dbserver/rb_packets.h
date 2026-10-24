#ifndef RB_PACKETS_H
#define RB_PACKETS_H

#include <cstdint>
#include <string>
#include <vector>

// Pro DJ Link UDP packets needed to look like a rekordbox PC (device 17) on the network. Qt-free.
// Layouts come from the captured rekordbox PC and deck packets (tests/fixtures/dbserver/udp/, extracted by
// extract_udp_fixtures.py); the tests rebuild every captured packet byte for byte. See
// references/libcdj/doc-internal/how-to-pose-as-rekordbox.md.

namespace vdj {
namespace db {

constexpr uint16_t kPortAnnounce = 50000;
constexpr uint16_t kPortBeat = 50001;
constexpr uint16_t kPortStatus = 50002;

enum PacketType : uint8_t {
    PktClaim1 = 0x00, PktClaim2 = 0x02, PktClaim3 = 0x04, PktKeepAlive = 0x06,
    PktMediaQuery = 0x05, PktMediaResponse = 0x06,      // on 50002 (the same number as the keep-alive on 50000)
    PktPcStatus = 0x29, PktPoll = 0x10, PktPollReply = 0x16, PktName = 0x11, PktCdjStatus = 0x0a, PktBeat = 0x28, PktMixerStatus = 0x29, PktMasterRequest = 0x26, PktMasterResponse = 0x27
};

// Who we pretend to be. The defaults are rekordbox's values from the capture; the deck() factory gives the values of a
// real deck (an XDJ-700 in the capture) so the same builders reproduce the deck's packets in tests.
struct Identity {
    uint8_t  mac[6] = {0, 0, 0, 0, 0, 0};
    uint8_t  ip[4] = {0, 0, 0, 0};
    uint8_t  device = 17;                 // 0x11
    std::string name = "rekordbox";       // at most 20 bytes
    std::string computerName = "rekordbox";   // shown on the deck; UTF-8, at most 128 characters
    uint8_t  variant = 0x03;              // byte 0x21 of claim and keep-alive packets: 03 rekordbox, 02 deck (meaning unknown)
    uint8_t  deviceType = 0x01;           // byte 0x25 of the keep-alive: 01 rekordbox, 02 deck (likely)
    uint8_t  claimKind = 0x04;            // 2nd byte after the counter of claim 1/2: 04 rekordbox, 01 deck (meaning unknown)
    uint8_t  keepAliveTail[6] = {0x01, 0x01, 0x00, 0x00, 0x04, 0x08};    // bytes 0x30..0x35 (unknown)

    static Identity deck();               // the XDJ-700 of the capture (name, variant, tail); mac/ip/device to be set
};

// Claim packets. counter is 1..3. Stage 3 of rekordbox was never captured (GUESS: same shape as the deck's).
std::vector<uint8_t> claimStage1(const Identity& id, uint8_t counter);     // 44 bytes
std::vector<uint8_t> claimStage2(const Identity& id, uint8_t counter);     // 50 bytes
std::vector<uint8_t> claimStage3(const Identity& id, uint8_t counter);     // 38 bytes
std::vector<uint8_t> keepAlive(const Identity& id);                        // 54 bytes
std::vector<uint8_t> namePacket(const Identity& id);
// The PC's own status, broadcast to UDP 50002 every 100 ms from the moment it has joined (56 bytes): its tempo, 100%
// pitch, the master flag (c0/00 -> e0/01 when the PC became master) and the beat in the bar (0 when stopped).
std::vector<uint8_t> pcStatus(const Identity& id, uint16_t bpmX100, bool master = false, uint8_t beatInBar = 0);
std::vector<uint8_t> pollReply(const Identity& id);                       // 48 bytes: the answer to a deck's type-0x10 poll on 50002                       // 296 bytes, UTF-16BE computer name

// A deck's poll of a new device (type 0x10 to UDP 50002, 36 bytes); the rekordbox PC answered it with type 0x16.
struct PollInfo { std::string name; uint8_t device = 0; };
bool parsePoll(const uint8_t* p, size_t len, PollInfo* out);

// The slot description a deck gets in answer to its media query (192 bytes).
struct MediaInfo {
    uint8_t     slot = 4;                 // 4 = the rekordbox collection (likely), 3 = USB
    std::string name = "rekordbox";       // the stick's label (empty on the captured deck)
    uint16_t    trackCount = 0;
    uint16_t    playlistCount = 0;
    // what a player adds for a stick (captured from an XDJ-700 with a 64 GB stick); all empty/zero for the PC
    bool        fromPlayer = false;       // byte 0x20: 00 from a player, 01 from the rekordbox PC
    std::string date;                     // UTF-16BE at 0x6c, e.g. "2025-04-11"
    std::string version;                  // UTF-16BE at 0x84, "1000" on the captured deck
    uint64_t    totalBytes = 0;           // u64 at 0xb0
    uint64_t    freeBytes = 0;            // u64 at 0xb8
};
std::vector<uint8_t> mediaResponse(const Identity& id, const MediaInfo& m);

// ---- parsing of what other devices send -----------------------------------------------------------------------------

bool hasHeader(const uint8_t* p, size_t len);        // "Qspt1WmJOL" + a type byte
inline uint8_t packetType(const uint8_t* p, size_t len) { return hasHeader(p, len) ? p[10] : 0xff; }

struct KeepAliveInfo {
    std::string name;
    uint8_t     device = 0;
    uint8_t     deviceType = 0;
    uint8_t     mac[6] = {0};
    uint8_t     ip[4] = {0};
};
bool parseKeepAlive(const uint8_t* p, size_t len, KeepAliveInfo* out);       // type 0x06 on 50000

struct ClaimInfo {
    uint8_t     stage = 0;               // 1, 2 or 3
    std::string name;
    uint8_t     device = 0;              // 0 for stage 1 (not carried)
    uint8_t     counter = 0;
};
bool parseClaim(const uint8_t* p, size_t len, ClaimInfo* out);               // types 0x00, 0x02, 0x04

struct MediaQuery {
    std::string name;
    uint8_t     askerDevice = 0;
    uint8_t     askerIp[4] = {0};
    uint32_t    targetDevice = 0;
    uint32_t    slot = 0;
};
bool parseMediaQuery(const uint8_t* p, size_t len, MediaQuery* out);         // type 0x05 on 50002

std::string ipString(const uint8_t ip[4]);

} // namespace db
} // namespace vdj

#endif // RB_PACKETS_H
