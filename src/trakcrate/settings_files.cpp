#include "settings_files.h"

#include "fsutil.h"

#include <algorithm>

namespace trakcrate {

uint16_t crc16Xmodem(const void* data, size_t len)
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= uint16_t(p[i]) << 8;
        for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    }
    return crc;
}

static uint32_t le32(const std::string& s, size_t at) { return uint32_t(uint8_t(s[at])) | uint32_t(uint8_t(s[at + 1])) << 8 | uint32_t(uint8_t(s[at + 2])) << 16 | uint32_t(uint8_t(s[at + 3])) << 24; }

// The layout is: 0x00 u32 header length (0x60), header text to 0x60, 0x64 u32 payload length, 0x68 magic, payload, u16 CRC, u16 0.
// Returns the offset of the CRC, or 0 when the shape is wrong.
static size_t crcOffset(const std::string& b, std::string* err)
{
    auto bad = [&](const char* m) -> size_t { if (err) *err = m; return 0; };
    if (b.size() < 0x70) return bad("too short");
    if (le32(b, 0) != 0x60) return bad("unexpected header length");
    if (le32(b, 0x68) != 0x12345678) return bad("bad magic");
    const uint32_t payload = le32(b, 0x64);                 // counts the magic and the payload bytes, 0x20 in a 140 byte file
    const size_t crcAt = b.size() - 4;
    if (0x68 + size_t(payload) != crcAt) return bad("payload length does not match the file size");
    if (b[b.size() - 2] != 0 || b[b.size() - 1] != 0) return bad("missing zero padding");
    return crcAt;
}

bool verifySettingsFile(const std::string& bytes, std::string* err)
{
    const size_t at = crcOffset(bytes, err);
    if (!at) return false;
    const uint16_t want = uint16_t(uint8_t(bytes[at]) | uint8_t(bytes[at + 1]) << 8);
    const uint16_t got = crc16Xmodem(bytes.data() + 0x68, at - 0x68);
    if (want != got) {
        if (err) *err = "CRC mismatch";
        return false;
    }
    return true;
}

// Shared by the two newer files: header length, payload length and size agree, the CRC (over [crcFrom, crcAt)) and the zero padding are right.
static bool verifyNewSettings(const std::string& b, size_t payloadLen, size_t crcFrom, std::string* err)
{
    auto bad = [&](const char* m) { if (err) *err = m; return false; };
    if (b.size() != 0x68 + payloadLen + 4) return bad("unexpected file size");
    if (le32(b, 0) != 0x60) return bad("unexpected header length");
    if (le32(b, 0x64) != payloadLen) return bad("unexpected payload length");
    if (b[b.size() - 2] != 0 || b[b.size() - 1] != 0) return bad("missing zero padding");
    const size_t at = b.size() - 4;
    const uint16_t want = uint16_t(uint8_t(b[at]) | uint8_t(b[at + 1]) << 8);
    if (want != crc16Xmodem(b.data() + crcFrom, at - crcFrom)) return bad("CRC mismatch");
    return true;
}

bool verifyMySetting2File(const std::string& bytes, std::string* err) { return verifyNewSettings(bytes, 0x28, 0x68, err); }

bool verifyDjmMySettingFile(const std::string& bytes, std::string* err)
{
    if (!verifyNewSettings(bytes, 0x34, 0, err)) return false;
    if (le32(bytes, 0x68) != 0x12345678) {
        if (err) *err = "bad magic";
        return false;
    }
    return true;
}

bool fixSettingsCrc(std::string* bytes)
{
    const size_t at = crcOffset(*bytes, nullptr);
    if (!at) return false;
    const uint16_t crc = crc16Xmodem(bytes->data() + 0x68, at - 0x68);
    (*bytes)[at] = char(crc & 0xFF);
    (*bytes)[at + 1] = char(crc >> 8);
    return true;
}

bool loadSettingsFile(const std::string& path, std::string* out, std::string* err)
{
    std::string bytes;
    if (!readFile(path, &bytes, err)) return false;
    if (!verifySettingsFile(bytes, err)) {
        if (err) *err = path + ": " + *err;
        return false;
    }
    *out = bytes;
    return true;
}

bool parseDjprofile(const std::string& bytes, DjProfile* out, std::string* err)
{
    if (bytes.size() != kDjprofileSize) {
        if (err) *err = "djprofile.nxs is " + std::to_string(bytes.size()) + " bytes, expected " + std::to_string(kDjprofileSize);
        return false;
    }
    uint64_t ts = 0;
    for (int i = 0; i < 8; ++i) ts = ts << 8 | uint8_t(bytes[4 + i]);
    const size_t end = bytes.find('\0', kDjprofileNameAt);
    if (end == std::string::npos) {
        if (err) *err = "djprofile.nxs: the user name is not terminated";
        return false;
    }
    out->timestampMs = ts;
    out->userName = bytes.substr(kDjprofileNameAt, end - kDjprofileNameAt);
    out->unknownAt1c = uint32_t(uint8_t(bytes[0x1c])) << 24 | uint32_t(uint8_t(bytes[0x1d])) << 16 | uint32_t(uint8_t(bytes[0x1e])) << 8 | uint8_t(bytes[0x1f]);
    return true;
}

bool setDjprofileUserName(std::string* bytes, const std::string& name, std::string* err)
{
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    DjProfile cur;
    if (!parseDjprofile(*bytes, &cur, err)) return false;
    if (name.empty()) return fail("the user name is empty");
    if (name.size() > kDjprofileNameMax) return fail("the user name is longer than " + std::to_string(kDjprofileNameMax) + " bytes");
    for (unsigned char c : name)
        if (c < 0x20 || c == 0x7f) return fail("the user name contains a control character");
    // valid UTF-8 only (a name cut in the middle of a character would show as rubbish on the deck)
    for (size_t i = 0; i < name.size();) {
        unsigned char c = name[i];
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > name.size()) return fail("the user name is not valid UTF-8");
        for (size_t k = 1; k < n; ++k)
            if ((uint8_t(name[i + k]) & 0xc0) != 0x80) return fail("the user name is not valid UTF-8");
        i += n;
    }
    std::fill(bytes->begin() + kDjprofileNameAt, bytes->end(), '\0');
    std::copy(name.begin(), name.end(), bytes->begin() + kDjprofileNameAt);
    return true;
}

} // namespace trakcrate
