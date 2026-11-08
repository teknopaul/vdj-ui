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
                                                              {"exportExt.pdb", SettingsKind::ExportExt}, {"MYSETTING2.DAT", SettingsKind::MySetting2},
                                                              {"DJMMYSETTING.DAT", SettingsKind::DjmMySetting}};
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
    case SettingsKind::MySetting2: return "MYSETTING2.DAT";
    case SettingsKind::DjmMySetting: return "DJMMYSETTING.DAT";
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

// ---- DEVSETTING.DAT and MYSETTING.DAT: the player preferences -------------------------------------------------------------------------
// Offsets are absolute file offsets (the data section starts at 0x68: magic, block id, then the options from 0x70). The codes are those of
// rekordcrate's setting.rs. An option byte that is not in a field's table is shown as "unknown option 0xNN" and is grey, like the bytes
// nobody has named yet.

struct SettingOption {
    uint8_t code;
    const char* label;
};
struct SettingField {
    size_t at;
    const char* name;
    const SettingOption* options;
    size_t count;
    const char* zeroNote;           // what a 0x00 means for this field, if it is seen in practice (older files); null: nothing to add
};
struct UnknownSpan {
    size_t at, len;
    const char* note;
};

#define OPTIONS(a) a, sizeof(a) / sizeof((a)[0])

static const SettingOption kOffOn[] = {{0x80, "Off"}, {0x81, "On"}};
static const SettingOption kUnlockLock[] = {{0x80, "Unlock"}, {0x81, "Lock"}};
static const SettingOption kBrightness5[] = {{0x81, "1"}, {0x82, "2"}, {0x83, "3"}, {0x84, "4"}, {0x85, "5"}};
static const SettingOption kBrightnessOff12[] = {{0x80, "Off"}, {0x81, "1 (Dark)"}, {0x82, "2 (Bright)"}};
static const SettingOption kAutoCueLevel[] = {{0x80, "-36dB"}, {0x81, "-42dB"}, {0x82, "-48dB"}, {0x83, "-54dB"}, {0x84, "-60dB"},
                                              {0x85, "-66dB"}, {0x86, "-72dB"}, {0x87, "-78dB"}, {0x88, "Memory"}};
static const SettingOption kLanguage[] = {{0x81, "English"}, {0x82, "Fran\xc3\xa7" "ais"}, {0x83, "Deutsch"}, {0x84, "Italiano"}, {0x85, "Nederlands"},
                                          {0x86, "Espa\xc3\xb1ol"}, {0x87, "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9"},
                                          {0x88, "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4"}, {0x89, "\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87"},
                                          {0x8a, "\xe7\xb9\x81\xe9\xab\x94\xe4\xb8\xad\xe6\x96\x87"}, {0x8b, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"},
                                          {0x8c, "Portugu\xc3\xaa" "s"}, {0x8d, "Svenska"}, {0x8e, "\xc4\x8c" "e\xc5\xa1" "tina"}, {0x8f, "Magyar"},
                                          {0x90, "Dansk"}, {0x91, "\xce\x95\xce\xbb\xce\xbb\xce\xb7\xce\xbd\xce\xb9\xce\xba\xce\xac"}, {0x92, "T\xc3\xbc" "rk\xc3\xa7" "e"}};
static const SettingOption kQuantizeBeat[] = {{0x80, "1 Beat"}, {0x81, "1/2 Beat"}, {0x82, "1/4 Beat"}, {0x83, "1/8 Beat"}};
static const SettingOption kHotCueAutoLoad[] = {{0x80, "Off"}, {0x81, "On"}, {0x82, "rekordbox SETTING"}};
static const SettingOption kPlayMode[] = {{0x80, "Continue / On"}, {0x81, "Single / Off"}};
static const SettingOption kTimeMode[] = {{0x80, "Elapsed"}, {0x81, "Remain"}};
static const SettingOption kJogMode[] = {{0x80, "CDJ"}, {0x81, "Vinyl"}};
static const SettingOption kTempoRange[] = {{0x80, "\xc2\xb1" "6%"}, {0x81, "\xc2\xb1" "10%"}, {0x82, "\xc2\xb1" "16%"}, {0x83, "Wide"}};
static const SettingOption kPhaseMeter[] = {{0x80, "Type 1"}, {0x81, "Type 2"}};
static const SettingOption kOverviewWaveform[] = {{0x01, "Half Waveform"}, {0x02, "Full Waveform"}};
static const SettingOption kWaveformColor[] = {{0x01, "Blue"}, {0x03, "RGB"}, {0x04, "3Band"}};
static const SettingOption kKeyDisplay[] = {{0x01, "Classic"}, {0x02, "Alphanumeric"}};
static const SettingOption kWaveformPosition[] = {{0x01, "Center"}, {0x02, "Left"}};

static const char* const kOldDevNote = "0x00; rekordbox 6 writes 0x01 here";

static const SettingField kDevFields[] = {
    {0x71, "Overview Waveform Type", OPTIONS(kOverviewWaveform), nullptr},
    {0x72, "Waveform Color", OPTIONS(kWaveformColor), nullptr},
    {0x74, "Key Display Format", OPTIONS(kKeyDisplay), kOldDevNote},
    {0x75, "Waveform Current Position", OPTIONS(kWaveformPosition), kOldDevNote},
};
static const UnknownSpan kDevUnknown[] = {{0x70, 1, "0x01 in the samples"}, {0x73, 1, "0x01 in the samples"}, {0x76, 18, "zero in the samples"}};

static const SettingField kMyFields[] = {
    {0x70, "On Air Display", OPTIONS(kOffOn), nullptr},
    {0x71, "LCD Brightness", OPTIONS(kBrightness5), nullptr},
    {0x72, "Quantize", OPTIONS(kOffOn), nullptr},
    {0x73, "Auto Cue Level", OPTIONS(kAutoCueLevel), nullptr},
    {0x74, "Language", OPTIONS(kLanguage), nullptr},
    {0x76, "Jog Ring Brightness", OPTIONS(kBrightnessOff12), nullptr},
    {0x77, "Jog Ring Indicator", OPTIONS(kOffOn), nullptr},
    {0x78, "Slip Flashing", OPTIONS(kOffOn), nullptr},
    {0x7c, "Disc Slot Illumination", OPTIONS(kBrightnessOff12), nullptr},
    {0x7d, "Eject/Load Lock", OPTIONS(kUnlockLock), nullptr},
    {0x7e, "Sync", OPTIONS(kOffOn), nullptr},
    {0x7f, "Play Mode / Auto Play Mode", OPTIONS(kPlayMode), nullptr},
    {0x80, "Quantize Beat Value", OPTIONS(kQuantizeBeat), nullptr},
    {0x81, "Hot Cue Auto Load", OPTIONS(kHotCueAutoLoad), nullptr},
    {0x82, "Hot Cue Color", OPTIONS(kOffOn), nullptr},
    {0x85, "Needle Lock", OPTIONS(kUnlockLock), "0x00; the shipped (older) file has no value here, rekordbox 6 writes 0x80 or 0x81"},
    {0x88, "Time Mode", OPTIONS(kTimeMode), nullptr},
    {0x89, "Jog Mode", OPTIONS(kJogMode), nullptr},
    {0x8a, "Auto Cue", OPTIONS(kOffOn), nullptr},
    {0x8b, "Master Tempo", OPTIONS(kOffOn), nullptr},
    {0x8c, "Tempo Range", OPTIONS(kTempoRange), nullptr},
    {0x8d, "Phase Meter", OPTIONS(kPhaseMeter), nullptr},
};
static const UnknownSpan kMyUnknown[] = {{0x75, 1, "0x01 in the samples"}, {0x79, 3, "01 01 01 in the samples"}, {0x83, 2, "zero in the samples"},
                                         {0x86, 2, "zero in the samples"}, {0x8e, 2, "zero in the samples"}};

// The label of an option byte; "unknown option 0xNN" (and *known false) when the field has no such code.
static std::string optionLabel(const SettingField& f, uint8_t v, bool* known)
{
    for (size_t i = 0; i < f.count; ++i)
        if (f.options[i].code == v) { *known = true; return f.options[i].label; }
    *known = false;
    return "unknown option " + hex(v, 2);
}

static std::string spanName(size_t at, size_t len)
{
    char b[40];
    if (len <= 1) snprintf(b, sizeof b, "unknown 0x%02zX", at);
    else snprintf(b, sizeof b, "unknown 0x%02zX-0x%02zX", at, at + len - 1);
    return b;
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
    if (plen < 8 || 0x6c + plen > b.size()) {                     // the payload length counts the CRC and its two bytes of padding
        rows->push_back({"settings", "", "the payload length does not fit the file", false});
        return;
    }
    const size_t crcAt = 0x6c + plen - 4;
    const uint16_t stored = uint16_t((unsigned char)b[crcAt] | (unsigned char)b[crcAt + 1] << 8);
    const uint16_t calc = crc16Xmodem(b.data() + 0x68, crcAt - 0x68);
    rows->push_back({"crc", hex(stored, 4), stored == calc ? "matches CRC-16/XMODEM" : "does not match, computed " + hex(calc, 4), true});
    if (plen >= 4) rows->push_back({"block id", std::to_string(le32(b, 0x6c)), "", true});

    // the options are decoded whatever the CRC says, once the shape is right
    const bool dev = kind == SettingsKind::DevSetting;
    const size_t expected = dev ? 0x20 : 0x28;
    const char* shape = nullptr;
    if (le32(b, 0) != 0x60) shape = "the header length is not 0x60";
    else if (le32(b, 0x68) != 0x12345678) shape = "the magic is not 0x12345678";
    else if (plen != expected) shape = dev ? "the payload length is not 0x20, this is not a DEVSETTING.DAT" : "the payload length is not 0x28, this is not a MYSETTING.DAT";
    else if (b.size() != 0x68 + plen + 4) shape = "the file size does not match the payload length";
    if (shape) {
        rows->push_back({"settings", "", shape, false});
        return;
    }
    const SettingField* fields = dev ? kDevFields : kMyFields;
    const size_t nFields = dev ? sizeof kDevFields / sizeof kDevFields[0] : sizeof kMyFields / sizeof kMyFields[0];
    for (size_t i = 0; i < nFields; ++i) {
        const SettingField& f = fields[i];
        if (f.at >= b.size()) continue;
        const uint8_t v = (uint8_t)b[f.at];
        bool known = false;
        const std::string label = optionLabel(f, v, &known);
        rows->push_back({f.name, label, !known && v == 0 && f.zeroNote ? f.zeroNote : hex(v, 2), known});
    }
    const UnknownSpan* spans = dev ? kDevUnknown : kMyUnknown;
    const size_t nSpans = dev ? sizeof kDevUnknown / sizeof kDevUnknown[0] : sizeof kMyUnknown / sizeof kMyUnknown[0];
    for (size_t i = 0; i < nSpans; ++i)
        if (spans[i].at + spans[i].len <= b.size()) rows->push_back({spanName(spans[i].at, spans[i].len), hexBytes(b, spans[i].at, spans[i].len), spans[i].note, false});
}

// ---- MYSETTING2.DAT and DJMMYSETTING.DAT: newer player preferences and the mixer's ------------------------------------------------------
// Option codes from rekordcrate's setting.rs, offsets and checksums verified on its 37 one-option-per-file samples.

static const SettingOption kVinylSpeed[] = {{0x80, "Touch & Release"}, {0x81, "Touch"}, {0x82, "Release"}};
static const SettingOption kJogDisplay[] = {{0x80, "Auto"}, {0x81, "Info"}, {0x82, "Simple"}, {0x83, "Artwork"}};
static const SettingOption kPadBrightness[] = {{0x81, "1"}, {0x82, "2"}, {0x83, "3"}, {0x84, "4"}};
static const SettingOption kWaveformDivisions[] = {{0x80, "Time Scale"}, {0x81, "Phrase"}};
static const SettingOption kWaveformPhase[] = {{0x80, "Waveform"}, {0x81, "Phase Meter"}};
static const SettingOption kBeatJump[] = {{0x80, "1/2 Beat"}, {0x81, "1 Beat"}, {0x82, "2 Beat"}, {0x83, "4 Beat"},
                                          {0x84, "8 Beat"}, {0x85, "16 Beat"}, {0x86, "32 Beat"}, {0x87, "64 Beat"}};
static const SettingOption kChannelFader[] = {{0x80, "Steep Top"}, {0x81, "Linear"}, {0x82, "Steep Bottom"}};
static const SettingOption kCrossfader[] = {{0x80, "Constant Power"}, {0x81, "Slow Cut"}, {0x82, "Fast Cut"}};
static const SettingOption kPostPreEq[] = {{0x80, "Post EQ"}, {0x81, "Pre EQ"}};
static const SettingOption kStereoMono[] = {{0x80, "Stereo"}, {0x81, "Mono Split"}};
static const SettingOption kTalkOverMode[] = {{0x80, "Advanced"}, {0x81, "Normal"}};
static const SettingOption kTalkOverLevel[] = {{0x80, "-24dB"}, {0x81, "-18dB"}, {0x82, "-12dB"}, {0x83, "-6dB"}};
static const SettingOption kMidiChannel[] = {{0x80, "1"}, {0x81, "2"}, {0x82, "3"}, {0x83, "4"}, {0x84, "5"}, {0x85, "6"}, {0x86, "7"}, {0x87, "8"},
                                             {0x88, "9"}, {0x89, "10"}, {0x8a, "11"}, {0x8b, "12"}, {0x8c, "13"}, {0x8d, "14"}, {0x8e, "15"}, {0x8f, "16"}};
static const SettingOption kToggleTrigger[] = {{0x80, "Toggle"}, {0x81, "Trigger"}};
static const SettingOption kMixerDisplay[] = {{0x80, "White"}, {0x81, "1"}, {0x82, "2"}, {0x83, "3"}, {0x84, "4"}, {0x85, "5"}};
static const SettingOption kMixerIndicator[] = {{0x80, "1"}, {0x81, "2"}, {0x82, "3"}};
static const SettingOption kLongFader[] = {{0x80, "Exponential"}, {0x81, "Smooth"}, {0x82, "Linear"}};

static const SettingField kMy2Fields[] = {
    {0x68, "Vinyl Speed Adjust", OPTIONS(kVinylSpeed), nullptr},
    {0x69, "Jog Display Mode", OPTIONS(kJogDisplay), nullptr},
    {0x6a, "Pad/Button Brightness", OPTIONS(kPadBrightness), nullptr},
    {0x6b, "Jog LCD Brightness", OPTIONS(kBrightness5), nullptr},
    {0x6c, "Waveform Divisions", OPTIONS(kWaveformDivisions), nullptr},
    {0x72, "Waveform / Phase Meter", OPTIONS(kWaveformPhase), nullptr},
    {0x74, "Beat Jump Beat Value", OPTIONS(kBeatJump), nullptr},
};
static const UnknownSpan kMy2Unknown[] = {{0x6d, 5, "zero in the samples"}, {0x73, 1, "0x81 in the samples"}, {0x75, 27, "zero in the samples"}};

static const SettingField kDjmFields[] = {
    {0x74, "Channel Fader Curve", OPTIONS(kChannelFader), nullptr},
    {0x75, "Crossfader Curve", OPTIONS(kCrossfader), nullptr},
    {0x76, "Headphones Pre EQ", OPTIONS(kPostPreEq), nullptr},
    {0x77, "Headphones Mono Split", OPTIONS(kStereoMono), nullptr},
    {0x78, "Beat FX Quantize", OPTIONS(kOffOn), nullptr},
    {0x79, "Mic Low Cut", OPTIONS(kOffOn), nullptr},
    {0x7a, "Talk Over Mode", OPTIONS(kTalkOverMode), nullptr},
    {0x7b, "Talk Over Level", OPTIONS(kTalkOverLevel), nullptr},
    {0x7c, "MIDI Channel", OPTIONS(kMidiChannel), nullptr},
    {0x7d, "MIDI Button Type", OPTIONS(kToggleTrigger), nullptr},
    {0x7e, "Mixer Display Brightness", OPTIONS(kMixerDisplay), nullptr},
    {0x7f, "Mixer Indicator Brightness", OPTIONS(kMixerIndicator), nullptr},
    {0x80, "Channel Fader Curve (long fader)", OPTIONS(kLongFader), nullptr},
};
static const UnknownSpan kDjmUnknown[] = {{0x6c, 8, "block id 1 and 0x20 in the samples"}, {0x81, 27, "zero in the samples"}};

static void describeNewSetting(SettingsKind kind, const std::string& b, std::vector<SettingsRow>* rows)
{
    const bool djm = kind == SettingsKind::DjmMySetting;
    std::string why;
    const bool ok = djm ? verifyDjmMySettingFile(b, &why) : verifyMySetting2File(b, &why);
    rows->push_back({"file check", ok ? "ok" : "damaged", ok ? "size, header and CRC agree" : why, true});
    if (b.size() < 0x70) return;
    rows->push_back({"brand", textField(b, 0x04, 32), "", true});
    rows->push_back({"program", textField(b, 0x24, 32), "", true});
    rows->push_back({"version", textField(b, 0x44, 32), "", true});
    rows->push_back({"payload length", hex(le32(b, 0x64), 2), "", true});
    const size_t expected = djm ? 0xa0 : 0x94;
    if (b.size() != expected || le32(b, 0) != 0x60) {
        rows->push_back({"settings", "", "the file does not have the shape of " + std::string(settingsKindName(kind)), false});
        return;
    }
    const size_t crcAt = b.size() - 4;
    rows->push_back({"crc", hex(uint16_t(uint8_t(b[crcAt]) | uint8_t(b[crcAt + 1]) << 8), 4), djm ? "CRC-16/XMODEM over the whole file before it" : "CRC-16/XMODEM over 0x68 up to it", true});
    const SettingField* fields = djm ? kDjmFields : kMy2Fields;
    const size_t nFields = djm ? sizeof kDjmFields / sizeof kDjmFields[0] : sizeof kMy2Fields / sizeof kMy2Fields[0];
    for (size_t i = 0; i < nFields; ++i) {
        const uint8_t v = (uint8_t)b[fields[i].at];
        bool known = false;
        const std::string label = optionLabel(fields[i], v, &known);
        rows->push_back({fields[i].name, label, hex(v, 2), known});
    }
    const UnknownSpan* spans = djm ? kDjmUnknown : kMy2Unknown;
    const size_t nSpans = djm ? sizeof kDjmUnknown / sizeof kDjmUnknown[0] : sizeof kMy2Unknown / sizeof kMy2Unknown[0];
    for (size_t i = 0; i < nSpans; ++i)
        rows->push_back({spanName(spans[i].at, spans[i].len), hexBytes(b, spans[i].at, spans[i].len), spans[i].note, false});
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
    case SettingsKind::MySetting2:
    case SettingsKind::DjmMySetting: describeNewSetting(kind, bytes, &rows); break;
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
