#ifndef TRAKCRATE_SETTINGS_SETS_H
#define TRAKCRATE_SETTINGS_SETS_H

#include "fsutil.h"
#include "library.h"

#include <string>
#include <vector>

// Named sets of the five player support files (TRAKCRATE-FORMAT.md section 10), one folder each under <library>/user-settings/:
//
//   user-settings/default.txt          "set: <name>": the set exports use unless told otherwise (absent = the first set by name)
//   user-settings/<set>/set.txt        created, source and user_name (key: value)
//   user-settings/<set>/DEVSETTING.DAT, MYSETTING.DAT, djprofile.nxs, RBFLTR.DAT, exportExt.pdb
//
// A set may lack files (a partial copy from a drive); a missing file falls back to the compiled in bytes when the set is resolved for an export.
// None of the files is optional on a pendrive: the decks misbehave without them. USBMNG.DAT is never part of a set (it is generated).
namespace trakcrate {

struct SettingsSet {
    std::string name;
    std::string created;                     // ISO 8601, UTC
    std::string source;                      // "built-in", "rekordbox:<drive>", "migrated", "copy of <set>"
    std::string userName;                    // the name in djprofile.nxs as last written by us (a cache, the file is the truth)
    bool isDefault = false;
};

// The bytes of the five files of a set, or the compiled in ones where the set has none.
struct SettingsFiles {
    std::string devSetting, mySetting, djprofile, rbfltr, exportExt;
};

// The five file names inside a set folder, and where each lives on a rekordbox drive (relative to the drive root); the two lists are parallel.
const std::vector<std::string>& settingsFileNames();
const std::vector<std::string>& settingsDriveFileNames();

// A single plain path component that does not start with '.' (those are hidden from listings).
bool isSafeSettingsSetName(const std::string& name);

std::vector<SettingsSet> listSettingsSets(const Library& lib);      // natural name order
bool hasSettingsSet(const Library& lib, const std::string& name);
fs::path settingsSetDir(const Library& lib, const std::string& name);
// The default set's name; "" when the library has no set. A default.txt naming a set that is gone is ignored.
std::string defaultSettingsSet(const Library& lib);
bool setDefaultSettingsSet(const Library& lib, const std::string& name, std::string* err = nullptr);
// `base` made unique among the sets (" 2", " 3", ...) after sanitizing it.
std::string uniqueSettingsSetName(const Library& lib, const std::string& base);

// A new set from the built in files. userName (may be empty) is written into djprofile.nxs. The first set of a library becomes the default.
bool createSettingsSet(const Library& lib, const std::string& name, const std::string& userName, std::string* err = nullptr);
// A new set from a mounted rekordbox drive (a folder holding PIONEER/). A damaged DEVSETTING.DAT / MYSETTING.DAT refuses the whole copy; a drive
// with none of the files is refused. `found` gets the on disk names of the files that were copied. `only` (on disk names) limits the copy to those files.
bool copySettingsSetFromDrive(const Library& lib, const std::string& name, const fs::path& driveRoot, std::vector<std::string>* found = nullptr,
                              std::string* err = nullptr, const std::vector<std::string>* only = nullptr);
bool renameSettingsSet(const Library& lib, const std::string& from, const std::string& to, std::string* err = nullptr);
bool duplicateSettingsSet(const Library& lib, const std::string& from, const std::string& to, std::string* err = nullptr);
// Changes the user name in the set's djprofile.nxs (the built in profile when the set has none) and the cached name in set.txt.
bool setSettingsSetUserName(const Library& lib, const std::string& name, const std::string& userName, std::string* err = nullptr);
// Deleting the default makes the first remaining set the default.
bool deleteSettingsSet(const Library& lib, const std::string& name, std::string* err = nullptr);

// The files of a set for an export; setName empty = the default set, and a library without any set gives the built in files. The two *SETTING.DAT
// files must pass verifySettingsFile or this fails naming the file. `builtin` gets the names of the files that fell back to the compiled in bytes.
bool resolveSettingsFiles(const Library& lib, const std::string& setName, SettingsFiles* out, std::vector<std::string>* builtin = nullptr,
                          std::string* err = nullptr);

// ---- the link from a virtual pendrive to a set: drives/<name>/drive.txt, "settings: <set>" (other keys are kept) ----
// The set a pendrive is linked to; "" when there is no link (the pendrive follows the default set).
std::string driveSettingsSet(const Library& lib, const std::string& drive);
// Links the pendrive to a set; an empty set removes the link. Refuses a set that does not exist.
bool setDriveSettingsSet(const Library& lib, const std::string& drive, const std::string& set, std::string* err = nullptr);
// The set to export a pendrive with: its link, or "" (= the default set) when there is none or the link points at a set that is gone (dangling is
// then true, so the caller can warn).
std::string effectiveDriveSettingsSet(const Library& lib, const std::string& drive, bool* dangling = nullptr);
// The pendrives linked to a set.
std::vector<std::string> drivesUsingSettingsSet(const Library& lib, const std::string& set);

// Flat files left in user-settings/ by earlier versions move into a set named "imported" (the default) when the library holds no set yet.
// Idempotent; called by Library::open.
bool migrateFlatSettings(const fs::path& libraryRoot, std::string* err = nullptr);

} // namespace trakcrate

#endif
