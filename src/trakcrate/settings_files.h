#ifndef TRAKCRATE_SETTINGS_FILES_H
#define TRAKCRATE_SETTINGS_FILES_H

#include <cstdint>
#include <string>

// PIONEER/DEVSETTING.DAT and MYSETTING.DAT (doc/trakcrate/settings-files.md): a 0x60 byte text header (brand, "rekordbox", version), a payload
// length, the magic 0x12345678, the payload and a CRC-16/XMODEM over the bytes from the magic up to the CRC (stored little endian, followed by
// two zero bytes).
namespace trakcrate {

// CRC-16/XMODEM (polynomial 0x1021, initial value 0, no reflection).
uint16_t crc16Xmodem(const void* data, size_t len);

// True for a well formed settings file: size, header length, payload length, magic and CRC all agree. err says which check failed.
bool verifySettingsFile(const std::string& bytes, std::string* err = nullptr);

// Recomputes the CRC of a settings file after its payload was changed. False if the file is not the right shape.
bool fixSettingsCrc(std::string* bytes);

// The files copied verbatim from a real drive; both pass verifySettingsFile.
const std::string& defaultDevSetting();
const std::string& defaultMySetting();

// The other three files a player expects next to the settings, copied verbatim from a real drive (data/trakcrate/user-settings/) and compiled
// in, so an export always has them: PIONEER/djprofile.nxs, PIONEER/rekordbox/RBFLTR.DAT and PIONEER/rekordbox/exportExt.pdb.
const std::string& defaultDjprofile();
const std::string& defaultRbfltr();
const std::string& defaultExportExt();
// PIONEER/USBANLZ/USBMNG.DAT of a drive with no path collisions: the 56 byte PMNG/PTBL header and a zeroed table of 200003 16 bit slots, which is
// what rekordbox writes (400058 bytes). A copy from another drive carries that drive's entries, so it is not used.
std::string emptyUsbmng();

// PIONEER/djprofile.nxs (doc/trakcrate/settings-files.md): 160 bytes. 0x00 u32 unknown, 0x04 u64 big endian milliseconds since 1970 (the profile's
// date), 0x1c four unknown bytes (not a checksum of anything tried, see the doc), 0x20 the user name, NUL terminated, zero padded to the end.
constexpr size_t kDjprofileSize = 160;
constexpr size_t kDjprofileNameAt = 0x20;
constexpr size_t kDjprofileNameMax = 63;           // bytes; the field is 128 long but only this much is assumed safe

struct DjProfile {
    uint64_t timestampMs = 0;
    std::string userName;
    uint32_t unknownAt1c = 0;                      // shown by the viewer; left untouched by an edit
};
bool parseDjprofile(const std::string& bytes, DjProfile* out, std::string* err = nullptr);

// Writes the user name into the bytes of a djprofile.nxs, leaving every other byte (including the four unknown ones at 0x1c) as it was.
// The name is UTF-8, 1..kDjprofileNameMax bytes, no control characters. UNTESTED ON A DECK: see doc/trakcrate/settings-files.md.
bool setDjprofileUserName(std::string* bytes, const std::string& name, std::string* err = nullptr);

// Reads a settings file a user provided as the template (for example from their own drive) and refuses a damaged one.
bool loadSettingsFile(const std::string& path, std::string* out, std::string* err = nullptr);

} // namespace trakcrate

#endif
