#include "settings_files.h"

#include "fsutil.h"

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

} // namespace trakcrate
