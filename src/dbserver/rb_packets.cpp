#include "rb_packets.h"

#include <cstdio>
#include <cstring>

#include "db_codec.h"

namespace vdj {
namespace db {

namespace {

const uint8_t kHeader[10] = {0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c};     // "Qspt1WmJOL"

void putName(std::vector<uint8_t>& p, const std::string& name)
{
    for (size_t i = 0; i < 20; ++i) p.push_back(i < name.size() ? uint8_t(name[i]) : 0);
}

std::vector<uint8_t> start(uint8_t type, bool withSubtype, const std::string& name)
{
    std::vector<uint8_t> p(kHeader, kHeader + 10);
    p.push_back(type);
    if (withSubtype) p.push_back(0x00);         // claims and keep-alives carry an extra 00 before the name
    putName(p, name);
    return p;
}

void put16(std::vector<uint8_t>& p, uint16_t v) { p.push_back(uint8_t(v >> 8)); p.push_back(uint8_t(v)); }
void put32(std::vector<uint8_t>& p, uint32_t v) { put16(p, uint16_t(v >> 16)); put16(p, uint16_t(v)); }

} // namespace

Identity Identity::deck()
{
    Identity d;
    d.name = "XDJ-700";
    d.computerName = "XDJ-700";
    d.variant = 0x02;
    d.deviceType = 0x02;
    d.claimKind = 0x01;
    const uint8_t tail[6] = {0x01, 0x00, 0x00, 0x00, 0x01, 0x00};
    std::memcpy(d.keepAliveTail, tail, 6);
    return d;
}

std::vector<uint8_t> claimStage1(const Identity& id, uint8_t counter)
{
    auto p = start(PktClaim1, true, id.name);
    p.push_back(0x01); p.push_back(id.variant); put16(p, 0x002c);
    p.push_back(counter); p.push_back(id.claimKind);
    p.insert(p.end(), id.mac, id.mac + 6);
    return p;
}

std::vector<uint8_t> claimStage2(const Identity& id, uint8_t counter)
{
    auto p = start(PktClaim2, true, id.name);
    p.push_back(0x01); p.push_back(id.variant); put16(p, 0x0032);
    p.insert(p.end(), id.ip, id.ip + 4);
    p.insert(p.end(), id.mac, id.mac + 6);
    p.push_back(id.device); p.push_back(counter); p.push_back(id.claimKind); p.push_back(0x01);   // last: auto-assign
    return p;
}

std::vector<uint8_t> claimStage3(const Identity& id, uint8_t counter)
{
    auto p = start(PktClaim3, true, id.name);
    p.push_back(0x01); p.push_back(id.variant); put16(p, 0x0026);
    p.push_back(id.device); p.push_back(counter);
    return p;
}

std::vector<uint8_t> keepAlive(const Identity& id)
{
    auto p = start(PktKeepAlive, true, id.name);
    p.push_back(0x01); p.push_back(id.variant); put16(p, 0x0036);
    p.push_back(id.device); p.push_back(id.deviceType);
    p.insert(p.end(), id.mac, id.mac + 6);
    p.insert(p.end(), id.ip, id.ip + 4);
    p.insert(p.end(), id.keepAliveTail, id.keepAliveTail + 6);
    return p;
}

std::vector<uint8_t> namePacket(const Identity& id)
{
    auto p = start(PktName, false, id.name);
    p.push_back(0x01); p.push_back(0x01); p.push_back(id.device); put16(p, 0x0104);
    p.push_back(id.device); p.push_back(0x01); p.push_back(0x00); p.push_back(0x00);
    const std::u16string u = utf8ToUtf16(id.computerName);
    for (size_t i = 0; i < 128; ++i) put16(p, i < u.size() && i < 127 ? uint16_t(u[i]) : 0);
    return p;
}

std::vector<uint8_t> pollReply(const Identity& id)
{
    auto p = start(PktPollReply, false, id.name);
    p.push_back(0x01); p.push_back(0x01); p.push_back(id.device);
    p.resize(48, 0x00);
    return p;
}

std::vector<uint8_t> mediaResponse(const Identity& id, const MediaInfo& m)
{
    auto p = start(PktMediaResponse, false, id.name);
    p.push_back(0x01); p.push_back(0x01); p.push_back(id.device); put16(p, 0x009c);
    put32(p, id.device);
    put32(p, m.slot);
    const std::u16string u = utf8ToUtf16(m.name);
    for (size_t i = 0; i < 32; ++i) put16(p, i < u.size() && i < 31 ? uint16_t(u[i]) : 0);      // 64 bytes
    p.resize(0xa6, 0);              // creation date / free space etc: zeros (GUESS: the capture's were zero)
    put16(p, m.trackCount);
    put16(p, 0);
    p.push_back(0x01); p.push_back(0x01);
    p.push_back(0x00); p.push_back(0x00);
    put16(p, m.playlistCount);      // 0xae
    p.resize(0xc0, 0);
    return p;
}

// ---- parsing -------------------------------------------------------------------------------------------------------

bool hasHeader(const uint8_t* p, size_t len)
{
    return len >= 11 && std::memcmp(p, kHeader, 10) == 0;
}

namespace {
std::string cstr(const uint8_t* p, size_t n)
{
    size_t l = 0;
    while (l < n && p[l]) ++l;
    return std::string(reinterpret_cast<const char*>(p), l);
}
}

bool parseKeepAlive(const uint8_t* p, size_t len, KeepAliveInfo* out)
{
    if (!hasHeader(p, len) || p[10] != PktKeepAlive || len < 0x30) return false;
    out->name = cstr(p + 0x0c, 20);
    out->device = p[0x24];
    out->deviceType = p[0x25];
    std::memcpy(out->mac, p + 0x26, 6);
    std::memcpy(out->ip, p + 0x2c, 4);
    return true;
}

bool parseClaim(const uint8_t* p, size_t len, ClaimInfo* out)
{
    if (!hasHeader(p, len) || len < 0x26) return false;
    switch (p[10]) {
    case PktClaim1: out->stage = 1; break;
    case PktClaim2: out->stage = 2; break;
    case PktClaim3: out->stage = 3; break;
    default: return false;
    }
    out->name = cstr(p + 0x0c, 20);
    out->device = 0;
    out->counter = 0;
    if (out->stage == 1) out->counter = p[0x24];
    if (out->stage == 2 && len >= 0x32) { out->device = p[0x2e]; out->counter = p[0x2f]; }
    if (out->stage == 3) { out->device = p[0x24]; out->counter = p[0x25]; }
    return true;
}

bool parseMediaQuery(const uint8_t* p, size_t len, MediaQuery* out)
{
    if (!hasHeader(p, len) || p[10] != PktMediaQuery || len < 0x30) return false;
    out->name = cstr(p + 0x0b, 20);
    out->askerDevice = p[0x21];
    std::memcpy(out->askerIp, p + 0x24, 4);
    out->targetDevice = (uint32_t(p[0x28]) << 24) | (uint32_t(p[0x29]) << 16) | (uint32_t(p[0x2a]) << 8) | p[0x2b];
    out->slot = (uint32_t(p[0x2c]) << 24) | (uint32_t(p[0x2d]) << 16) | (uint32_t(p[0x2e]) << 8) | p[0x2f];
    return true;
}

bool parsePoll(const uint8_t* p, size_t len, PollInfo* out)
{
    if (!hasHeader(p, len) || p[10] != PktPoll || len < 0x24) return false;
    out->name = cstr(p + 0x0b, 20);
    out->device = p[0x21];
    return true;
}

std::string ipString(const uint8_t ip[4])
{
    char b[24];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return b;
}

} // namespace db
} // namespace vdj
