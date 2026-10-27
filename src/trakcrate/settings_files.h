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

// Reads a settings file a user provided as the template (for example from their own drive) and refuses a damaged one.
bool loadSettingsFile(const std::string& path, std::string* out, std::string* err = nullptr);

} // namespace trakcrate

#endif
