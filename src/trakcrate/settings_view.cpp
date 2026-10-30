#include "settings_view.h"

#include "settings_files.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <map>

namespace trakcrate {

bool settingsKindForName(const std::string& n, SettingsKind* out)
{
    static const std::pair<const char*, SettingsKind> k[] = {{"DEVSETTING.DAT", SettingsKind::DevSetting}, {"MYSETTING.DAT", SettingsKind::MySetting},
                                                              {"djprofile.nxs", SettingsKind::Djprofile}, {"RBFLTR.DAT", SettingsKind::Rbfltr},
                                                              {"exportExt.pdb", SettingsKind::ExportExt}};
    for (const auto& e : k)
        if (n == e.first) { *out = e.second; return true; }
    return false;
}

const char* settingsKindName(SettingsKind k)
{
    switch (k) {
    case SettingsKind::DevSetting: return "DEVSETTING.DAT";
    case SettingsKind::MySetting: return "MYSETTING.DAT";
    case SettingsKind::Djprofile: return "djprofile.nxs";
    case SettingsKind::Rbfltr: return "RBFLTR.DAT";
    case SettingsKind::ExportExt: return "exportExt.pdb";
    }
    return "";
}

static std::string hex(unsigned v, int digits)
{
    char b[16];
    snprintf(b, sizeof b, "0x%0*x", digits, v);
    return b;
}

static std::string hexBytes(const std::string& s, size_t at, size_t n)
{
    std::string o;
    for (size_t i = at; i < at + n && i < s.size(); ++i) {
        char b[4];
        snprintf(b, sizeof b, "%02x", (unsigned char)s[i]);
        if (!o.empty()) o += ' ';
        o += b;
    }
    return o;
}

static uint32_t le32(const std::string& s, size_t at) { return uint32_t((unsigned char)s[at]) | uint32_t((unsigned char)s[at + 1]) << 8 | uint32_t((unsigned char)s[at + 2]) << 16 | uint32_t((unsigned char)s[at + 3]) << 24; }
static uint32_t be32(const std::string& s, size_t at) { return uint32_t((unsigned char)s[at]) << 24 | uint32_t((unsigned char)s[at + 1]) << 16 | uint32_t((unsigned char)s[at + 2]) << 8 | uint32_t((unsigned char)s[at + 3]); }

// A NUL or space padded text field.
static std::string textField(const std::string& s, size_t at, size_t n)
{
    std::string o = s.substr(at, n);
    size_t e = o.find('\0');
    if (e != std::string::npos) o.resize(e);
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}

static void describeSetting(SettingsKind kind, const std::string& b, std::vector<SettingsRow>* rows)
{
    std::string why;
    const bool ok = verifySettingsFile(b, &why);
    rows->push_back({"file check", ok ? "ok" : "damaged", ok ? "size, header, magic and CRC agree" : why, true});
    if (b.size() < 0x70) return;
    rows->push_back({"header length", hex(le32(b, 0), 2), "", true});
    rows->push_back({"brand", textField(b, 0x04, 32), "", true});
    rows->push_back({"program", textField(b, 0x24, 32), "", true});
    rows->push_back({"version", textField(b, 0x44, 32), "", true});
    rows->push_back({"payload length", hex(le32(b, 0x64), 2), "", true});
    rows->push_back({"magic", hex(le32(b, 0x68), 8), "0x12345678", true});
    const size_t plen = le32(b, 0x64);
    if (plen < 8 || 0x6c + plen > b.size()) return;               // the payload length counts the CRC and its two bytes of padding
    const size_t crcAt = 0x6c + plen - 4;
    const uint16_t stored = uint16_t((unsigned char)b[crcAt] | (unsigned char)b[crcAt + 1] << 8);
    const uint16_t calc = crc16Xmodem(b.data() + 0x68, crcAt - 0x68);
    rows->push_back({"crc", hex(stored, 4), stored == calc ? "matches CRC-16/XMODEM" : "does not match, computed " + hex(calc, 4), true});
    if (plen >= 4) rows->push_back({"block id", std::to_string(le32(b, 0x6c)), "", true});
    for (size_t i = 4; i < plen - 4; ++i) {
        const unsigned char v = (unsigned char)b[0x6c + i];
        std::string note = "not decoded";
        if (kind == SettingsKind::MySetting && (v & 0x80)) note = "option code 0x80 + " + std::to_string(v & 0x7f) + ", which setting is not decoded";
        rows->push_back({"payload " + hex(unsigned(i), 2), hex(v, 2), note, false});
    }
}

static void describeDjprofile(const std::string& b, std::vector<SettingsRow>* rows)
{
    DjProfile p;
    std::string why;
    if (!parseDjprofile(b, &p, &why)) {
        rows->push_back({"problem", why, "", true});
        return;
    }
    time_t t = time_t(p.timestampMs / 1000);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char buf[40];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S UTC", &tmv);
    rows->push_back({"name on the players", p.userName, "the only editable value", true});
    rows->push_back({"date", buf, "milliseconds at 0x04; probably when the profile was made", true});
    rows->push_back({"bytes 0x00-0x03", hexBytes(b, 0, 4), "not decoded", false});
    rows->push_back({"bytes 0x1c-0x1f", hexBytes(b, 0x1c, 4), "not decoded; not a checksum of anything tried", false});
}

static void describeRbfltr(const std::string& b, std::vector<SettingsRow>* rows)
{
    if (b.size() < 0x74 || b.compare(0, 4, "FMAI") != 0) {
        rows->push_back({"problem", "not an FMAI file", "", true});
        return;
    }
    rows->push_back({"header length", hex(be32(b, 4), 2), "", true});
    rows->push_back({"file length", std::to_string(be32(b, 8)), be32(b, 8) == b.size() ? "matches the file" : "does not match the file size " + std::to_string(b.size()), true});
    rows->push_back({"brand", textField(b, 0x10, 32), "", true});
    rows->push_back({"player", textField(b, 0x30, 32), "the player this file was written for", true});
    rows->push_back({"firmware", textField(b, 0x50, 32), "", true});
    rows->push_back({"records", std::to_string(be32(b, 0x70)), "FCND records", true});
    size_t at = 0x74;
    for (int n = 1; at + 12 <= b.size() && b.compare(at, 4, "FCND") == 0; ++n) {
        const uint32_t hl = be32(b, at + 4), rl = be32(b, at + 8);
        if (rl < hl || rl == 0 || at + rl > b.size()) break;
        rows->push_back({"record " + std::to_string(n), hexBytes(b, at + hl, rl - hl), "length " + std::to_string(rl) + ", numbers not decoded", false});
        at += rl;
    }
}

static void describePdb(const std::string& b, std::vector<SettingsRow>* rows)
{
    rows->push_back({"size", std::to_string(b.size()) + " bytes", "", true});
    if (b.size() < 16) {
        rows->push_back({"problem", "too short for a database", "", true});
        return;
    }
    const uint32_t page = le32(b, 4);
    rows->push_back({"page size", std::to_string(page), "", true});
    rows->push_back({"tables", std::to_string(le32(b, 8)), "", true});
    if (page) rows->push_back({"pages", std::to_string(b.size() / page), "", true});
    rows->push_back({"contents", "not decoded", "the extended export database; copied as it is", false});
}

std::vector<SettingsRow> describeSettingsFile(SettingsKind kind, const std::string& bytes)
{
    std::vector<SettingsRow> rows;
    rows.push_back({"size", std::to_string(bytes.size()) + " bytes", "", true});
    switch (kind) {
    case SettingsKind::DevSetting:
    case SettingsKind::MySetting: describeSetting(kind, bytes, &rows); break;
    case SettingsKind::Djprofile: describeDjprofile(bytes, &rows); break;
    case SettingsKind::Rbfltr: describeRbfltr(bytes, &rows); break;
    case SettingsKind::ExportExt: describePdb(bytes, &rows); break;
    }
    return rows;
}

std::vector<SettingsDiffRow> diffSettingsFiles(SettingsKind kind, const std::string& a, const std::string& b)
{
    std::vector<SettingsDiffRow> out;
    const std::vector<SettingsRow> ra = describeSettingsFile(kind, a), rb = describeSettingsFile(kind, b);
    std::map<std::string, std::string> inB, inA;
    for (const SettingsRow& r : rb) inB[r.field] = r.value;
    for (const SettingsRow& r : ra) inA[r.field] = r.value;
    for (const SettingsRow& r : ra) {
        auto it = inB.find(r.field);
        if (it == inB.end()) out.push_back({r.field, r.value, ""});
        else if (it->second != r.value) out.push_back({r.field, r.value, it->second});
    }
    for (const SettingsRow& r : rb)
        if (!inA.count(r.field)) out.push_back({r.field, "", r.value});
    if (a != b) {
        size_t n = 0, first = std::string::npos;
        for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
            if (a[i] != b[i]) { ++n; if (first == std::string::npos) first = i; }
        n += std::max(a.size(), b.size()) - std::min(a.size(), b.size());
        if (first == std::string::npos) first = std::min(a.size(), b.size());
        out.push_back({"raw bytes", std::to_string(n) + " differ", "first at " + hex(unsigned(first), 4)});
    }
    return out;
}

std::string hexDump(const std::string& bytes, size_t maxBytes)
{
    std::string o;
    const size_t n = std::min(bytes.size(), maxBytes);
    for (size_t i = 0; i < n; i += 16) {
        char line[24];
        snprintf(line, sizeof line, "%08zx ", i);
        o += line;
        std::string text;
        for (size_t k = 0; k < 16; ++k) {
            if (i + k < n) {
                char b[4];
                snprintf(b, sizeof b, " %02x", (unsigned char)bytes[i + k]);
                o += b;
                text += (bytes[i + k] >= 0x20 && bytes[i + k] < 0x7f) ? bytes[i + k] : '.';
            } else {
                o += "   ";
            }
        }
        o += "  |" + text + "|\n";
    }
    if (bytes.size() > n) o += "... " + std::to_string(bytes.size() - n) + " more bytes\n";
    return o;
}

} // namespace trakcrate
