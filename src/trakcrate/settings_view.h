#ifndef TRAKCRATE_SETTINGS_VIEW_H
#define TRAKCRATE_SETTINGS_VIEW_H

#include <string>
#include <vector>

// A read only description of the player support files for the settings viewer (doc/trakcrate/settings-files.md). Only what is known is shown as
// fields; bytes whose meaning is not known are listed as such (known == false) so nothing looks understood that is not.
namespace trakcrate {

enum class SettingsKind { DevSetting, MySetting, Djprofile, Rbfltr, ExportExt };

// The kind of a set's file by its on disk name ("DEVSETTING.DAT"); false for any other name.
bool settingsKindForName(const std::string& fileName, SettingsKind* out);
const char* settingsKindName(SettingsKind k);

struct SettingsRow {
    std::string field;
    std::string value;
    std::string note;
    bool known = true;                       // false: the meaning is not decoded (or an option code outside the known table)
};

// Never fails: a file of the wrong shape gives a "problem" row and what could be read.
std::vector<SettingsRow> describeSettingsFile(SettingsKind kind, const std::string& bytes);

struct SettingsDiffRow {
    std::string field;
    std::string a, b;                        // "" = the field does not exist on that side
};
// The fields (as describeSettingsFile names them) whose values differ, in file order, plus a "raw bytes" row when the bytes differ at all.
std::vector<SettingsDiffRow> diffSettingsFiles(SettingsKind kind, const std::string& a, const std::string& b);

// "00000000  64 65 ...  |de..|" lines of 16 bytes, at most maxBytes of them (then a line saying how many more).
std::string hexDump(const std::string& bytes, size_t maxBytes = 4096);

} // namespace trakcrate

#endif
