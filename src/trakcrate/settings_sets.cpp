#include "settings_sets.h"

#include "ext_pdb.h"
#include "natsort.h"
#include "settings_files.h"

#include <algorithm>
#include <ctime>
#include <map>

namespace trakcrate {

static bool fail(std::string* err, const std::string& msg)
{
    if (err) *err = msg;
    return false;
}

const std::vector<std::string>& settingsFileNames()
{
    static const std::vector<std::string> v = {"DEVSETTING.DAT", "MYSETTING.DAT", "djprofile.nxs", "RBFLTR.DAT", "exportExt.pdb"};
    return v;
}

const std::vector<std::string>& settingsDriveFileNames()
{
    static const std::vector<std::string> v = {"PIONEER/DEVSETTING.DAT", "PIONEER/MYSETTING.DAT", "PIONEER/djprofile.nxs", "PIONEER/rekordbox/RBFLTR.DAT",
                                               "PIONEER/rekordbox/exportExt.pdb"};
    return v;
}

bool isSafeSettingsSetName(const std::string& name)
{
    return isSafeName(name) && name[0] != '.';
}

// ---- set.txt: key: value lines; backslash is \\ and a newline \n -------------------------------------------------------------------

static std::string escapeValue(const std::string& v)
{
    std::string o;
    for (char c : v) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') continue;
        else o += c;
    }
    return o;
}

static std::string unescapeValue(const std::string& v)
{
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            ++i;
            o += v[i] == 'n' ? '\n' : v[i];
        } else {
            o += v[i];
        }
    }
    return o;
}

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static std::map<std::string, std::string> readKv(const fs::path& file)
{
    std::map<std::string, std::string> kv;
    std::string text;
    if (!readFile(file, &text)) return kv;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        if (line.empty() || line[0] == '#') continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        kv[trim(line.substr(0, colon))] = unescapeValue(trim(line.substr(colon + 1)));
    }
    return kv;
}

static std::string nowIso()
{
    time_t t = time(nullptr);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}

static bool writeSetInfo(const fs::path& setDir, const SettingsSet& s, std::string* err)
{
    std::string text = "created: " + escapeValue(s.created) + "\nsource: " + escapeValue(s.source) + "\nuser_name: " + escapeValue(s.userName) + "\n";
    return atomicWrite(setDir / "set.txt", text, err);
}

static std::vector<std::string> setDirNames(const Library& lib)
{
    std::vector<std::string> names;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(lib.settingsDir(), ec)) {
        std::string n = e.path().filename().string();
        if (n.empty() || n[0] == '.') continue;
        if (e.is_directory(ec)) names.push_back(n);
    }
    std::sort(names.begin(), names.end(), naturalLess);
    return names;
}

// ---- listing and the default --------------------------------------------------------------------------------------------------------

fs::path settingsSetDir(const Library& lib, const std::string& name) { return lib.settingsDir() / name; }

bool hasSettingsSet(const Library& lib, const std::string& name)
{
    std::error_code ec;
    return isSafeSettingsSetName(name) && fs::is_directory(settingsSetDir(lib, name), ec);
}

std::string defaultSettingsSet(const Library& lib)
{
    std::string want = readKv(lib.settingsDir() / "default.txt")["set"];
    if (!want.empty() && hasSettingsSet(lib, want)) return want;
    std::vector<std::string> names = setDirNames(lib);
    return names.empty() ? std::string() : names.front();
}

std::vector<SettingsSet> listSettingsSets(const Library& lib)
{
    std::vector<SettingsSet> out;
    const std::string def = defaultSettingsSet(lib);
    for (const std::string& n : setDirNames(lib)) {
        std::map<std::string, std::string> kv = readKv(settingsSetDir(lib, n) / "set.txt");
        SettingsSet s;
        s.name = n;
        s.created = kv["created"];
        s.source = kv["source"];
        s.userName = kv["user_name"];
        s.isDefault = n == def;
        out.push_back(s);
    }
    return out;
}

bool setDefaultSettingsSet(const Library& lib, const std::string& name, std::string* err)
{
    if (!hasSettingsSet(lib, name)) return fail(err, "no settings set named " + name);
    return atomicWrite(lib.settingsDir() / "default.txt", "set: " + escapeValue(name) + "\n", err);
}

std::string uniqueSettingsSetName(const Library& lib, const std::string& base)
{
    std::string b = sanitizeName(base);
    if (b.empty() || b[0] == '.') b = "settings";
    if (!hasSettingsSet(lib, b)) return b;
    for (int i = 2; i < 10000; ++i) {
        std::string n = b + " " + std::to_string(i);
        if (!hasSettingsSet(lib, n)) return n;
    }
    return b;
}

// Makes the library's first set the default; an existing valid default is kept.
static void ensureDefault(const Library& lib, const std::string& candidate)
{
    std::string cur = readKv(lib.settingsDir() / "default.txt")["set"];
    if (cur.empty() || !hasSettingsSet(lib, cur)) setDefaultSettingsSet(lib, candidate);
}

// ---- creating -----------------------------------------------------------------------------------------------------------------------

// Builds a set in a hidden temporary folder and renames it into place, so a listing never sees half a set.
static bool installSet(const Library& lib, const std::string& name, const std::vector<std::pair<std::string, std::string>>& files, const SettingsSet& info,
                       std::string* err)
{
    std::error_code ec;
    fs::create_directories(lib.settingsDir(), ec);
    fs::path tmp = makeTempDir(".tmp-set-", lib.settingsDir(), err);
    if (tmp.empty()) return false;
    bool ok = true;
    for (const auto& f : files)
        if (!atomicWrite(tmp / f.first, f.second, err)) { ok = false; break; }
    if (ok) ok = writeSetInfo(tmp, info, err);
    if (ok) {
        fs::rename(tmp, settingsSetDir(lib, name), ec);
        if (ec) ok = fail(err, "cannot create settings set " + name + ": " + ec.message());
    }
    if (!ok) fs::remove_all(tmp, ec);
    else ensureDefault(lib, name);
    return ok;
}

bool createSettingsSet(const Library& lib, const std::string& name, const std::string& userName, std::string* err)
{
    if (!isSafeSettingsSetName(name)) return fail(err, "not a usable settings set name: " + name);
    if (hasSettingsSet(lib, name)) return fail(err, "a settings set named " + name + " already exists");
    std::string profile = defaultDjprofile();
    if (!userName.empty() && !setDjprofileUserName(&profile, userName, err)) return false;
    SettingsSet info;
    info.name = name;
    info.created = nowIso();
    info.source = "built-in";
    DjProfile prof;
    if (parseDjprofile(profile, &prof)) info.userName = prof.userName;
    return installSet(lib, name,
                      {{"DEVSETTING.DAT", defaultDevSetting()}, {"MYSETTING.DAT", defaultMySetting()}, {"djprofile.nxs", profile}, {"RBFLTR.DAT", defaultRbfltr()},
                       {"exportExt.pdb", defaultExportExt()}},
                      info, err);
}

bool copySettingsSetFromDrive(const Library& lib, const std::string& name, const fs::path& driveRoot, std::vector<std::string>* found, std::string* err,
                              const std::vector<std::string>* only)
{
    if (!isSafeSettingsSetName(name)) return fail(err, "not a usable settings set name: " + name);
    if (hasSettingsSet(lib, name)) return fail(err, "a settings set named " + name + " already exists");
    if (found) found->clear();
    std::vector<std::pair<std::string, std::string>> files;
    const auto& disk = settingsFileNames();
    const auto& onDrive = settingsDriveFileNames();
    for (size_t i = 0; i < disk.size(); ++i) {
        if (only && std::find(only->begin(), only->end(), disk[i]) == only->end()) continue;
        std::error_code ec;
        const fs::path src = driveRoot / onDrive[i];
        if (!fs::is_regular_file(src, ec)) continue;
        std::string bytes, why;
        if (!readFile(src, &bytes, &why)) return fail(err, onDrive[i] + ": " + why);
        if (i < 2 && !verifySettingsFile(bytes, &why)) return fail(err, onDrive[i] + " is damaged: " + why);
        if (bytes.empty()) continue;
        files.emplace_back(disk[i], std::move(bytes));
    }
    if (files.empty()) return fail(err, "no settings files found under " + (driveRoot / "PIONEER").string());
    SettingsSet info;
    info.name = name;
    info.created = nowIso();
    info.source = "rekordbox:" + driveRoot.filename().string();
    for (const auto& f : files) {
        if (found) found->push_back(f.first);
    }
    return installSet(lib, name, files, info, err);
}

// ---- rename, duplicate, delete ------------------------------------------------------------------------------------------------------

bool renameSettingsSet(const Library& lib, const std::string& from, const std::string& to, std::string* err)
{
    if (!hasSettingsSet(lib, from)) return fail(err, "no settings set named " + from);
    if (!isSafeSettingsSetName(to)) return fail(err, "not a usable settings set name: " + to);
    if (hasSettingsSet(lib, to)) return fail(err, "a settings set named " + to + " already exists");
    const bool wasDefault = defaultSettingsSet(lib) == from;
    std::error_code ec;
    fs::rename(settingsSetDir(lib, from), settingsSetDir(lib, to), ec);
    if (ec) return fail(err, "cannot rename " + from + ": " + ec.message());
    for (const std::string& d : drivesUsingSettingsSet(lib, from))               // a pendrive follows its set to the new name
        if (!setDriveSettingsSet(lib, d, to, err)) return false;
    if (wasDefault) return setDefaultSettingsSet(lib, to, err);
    return true;
}

bool duplicateSettingsSet(const Library& lib, const std::string& from, const std::string& to, std::string* err)
{
    if (!hasSettingsSet(lib, from)) return fail(err, "no settings set named " + from);
    if (!isSafeSettingsSetName(to)) return fail(err, "not a usable settings set name: " + to);
    if (hasSettingsSet(lib, to)) return fail(err, "a settings set named " + to + " already exists");
    std::vector<std::pair<std::string, std::string>> files;
    for (const std::string& f : settingsFileNames()) {
        std::string bytes;
        std::error_code ec;
        if (fs::is_regular_file(settingsSetDir(lib, from) / f, ec) && readFile(settingsSetDir(lib, from) / f, &bytes, err)) files.emplace_back(f, std::move(bytes));
    }
    std::map<std::string, std::string> kv = readKv(settingsSetDir(lib, from) / "set.txt");
    SettingsSet info;
    info.name = to;
    info.created = nowIso();
    info.source = "copy of " + from;
    info.userName = kv["user_name"];
    return installSet(lib, to, files, info, err);
}

bool setSettingsSetUserName(const Library& lib, const std::string& name, const std::string& userName, std::string* err)
{
    if (!hasSettingsSet(lib, name)) return fail(err, "no settings set named " + name);
    const fs::path file = settingsSetDir(lib, name) / "djprofile.nxs";
    std::string bytes;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || !readFile(file, &bytes) || bytes.empty()) bytes = defaultDjprofile();
    if (!setDjprofileUserName(&bytes, userName, err) || !atomicWrite(file, bytes, err)) return false;
    std::map<std::string, std::string> kv = readKv(settingsSetDir(lib, name) / "set.txt");
    SettingsSet info;
    info.created = kv["created"];
    info.source = kv["source"];
    info.userName = userName;
    return writeSetInfo(settingsSetDir(lib, name), info, err);
}

bool removeSettingsTrackLinks(const Library& lib, const std::string& name, size_t* links, std::string* err)
{
    if (!hasSettingsSet(lib, name)) return fail(err, "no settings set named " + name);
    const fs::path file = settingsSetDir(lib, name) / "exportExt.pdb";
    std::string bytes, clean, why;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || !readFile(file, &bytes) || bytes.empty()) bytes = defaultExportExt();
    ExtPdbStats before;
    if (!extPdbStats(bytes, &before, &why) || !withoutTrackLinks(bytes, &clean, &why)) return fail(err, "exportExt.pdb of " + name + " cannot be cleaned: " + why);
    if (links) *links = before.trackLinks;
    if (clean == bytes && fs::is_regular_file(file, ec)) return true;
    return atomicWrite(file, clean, err);
}

bool deleteSettingsSet(const Library& lib, const std::string& name, std::string* err)
{
    if (!hasSettingsSet(lib, name)) return fail(err, "no settings set named " + name);
    const std::vector<std::string> users = drivesUsingSettingsSet(lib, name);
    if (!users.empty()) {
        std::string list;
        for (const std::string& u : users) list += (list.empty() ? "" : ", ") + u;
        return fail(err, "the settings " + name + " are used by the pendrive(s) " + list + "; link them to other settings first");
    }
    const bool wasDefault = defaultSettingsSet(lib) == name;
    std::error_code ec;
    fs::remove_all(settingsSetDir(lib, name), ec);
    if (ec) return fail(err, "cannot delete " + name + ": " + ec.message());
    if (wasDefault) {
        std::vector<std::string> left = setDirNames(lib);
        if (left.empty()) fs::remove(lib.settingsDir() / "default.txt", ec);
        else return setDefaultSettingsSet(lib, left.front(), err);
    }
    return true;
}

// ---- using a set --------------------------------------------------------------------------------------------------------------------

bool resolveSettingsFiles(const Library& lib, const std::string& setName, SettingsFiles* out, std::vector<std::string>* builtin, std::string* err)
{
    std::string set = setName.empty() ? defaultSettingsSet(lib) : setName;
    if (!set.empty() && !hasSettingsSet(lib, set)) return fail(err, "no settings set named " + set);
    const std::string* defaults[5] = {&defaultDevSetting(), &defaultMySetting(), &defaultDjprofile(), &defaultRbfltr(), &defaultExportExt()};
    std::string* targets[5] = {&out->devSetting, &out->mySetting, &out->djprofile, &out->rbfltr, &out->exportExt};
    if (builtin) builtin->clear();
    const auto& names = settingsFileNames();
    for (size_t i = 0; i < names.size(); ++i) {
        std::string bytes;
        std::error_code ec;
        if (!set.empty() && fs::is_regular_file(settingsSetDir(lib, set) / names[i], ec) && readFile(settingsSetDir(lib, set) / names[i], &bytes) && !bytes.empty()) {
            std::string why;
            if (i < 2 && !verifySettingsFile(bytes, &why)) return fail(err, names[i] + " of settings set " + set + " is damaged: " + why);
            *targets[i] = std::move(bytes);
        } else {
            *targets[i] = *defaults[i];
            if (builtin) builtin->push_back(names[i]);
        }
    }
    return true;
}

// ---- pendrive links ------------------------------------------------------------------------------------------------------------------

static fs::path driveFile(const Library& lib, const std::string& drive) { return lib.drivesDir() / drive / "drive.txt"; }

std::string driveSettingsSet(const Library& lib, const std::string& drive)
{
    if (!isSafeName(drive)) return std::string();
    return readKv(driveFile(lib, drive))["settings"];
}

bool setDriveSettingsSet(const Library& lib, const std::string& drive, const std::string& set, std::string* err)
{
    if (!lib.hasDrive(drive)) return fail(err, "no such pendrive: " + drive);
    if (!set.empty() && !hasSettingsSet(lib, set)) return fail(err, "no settings set named " + set);
    // keep the keys we do not know, in their order
    std::string text, old;
    readFile(driveFile(lib, drive), &old);
    size_t pos = 0;
    while (pos < old.size()) {
        size_t nl = old.find('\n', pos);
        const std::string line = old.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? old.size() : nl + 1;
        const size_t colon = line.find(':');
        if (colon != std::string::npos && trim(line.substr(0, colon)) == "settings") continue;
        if (!trim(line).empty()) text += line + "\n";
    }
    if (!set.empty()) text += "settings: " + escapeValue(set) + "\n";
    std::error_code ec;
    if (text.empty()) {
        fs::remove(driveFile(lib, drive), ec);
        return true;
    }
    return atomicWrite(driveFile(lib, drive), text, err);
}

std::string effectiveDriveSettingsSet(const Library& lib, const std::string& drive, bool* dangling)
{
    if (dangling) *dangling = false;
    const std::string linked = driveSettingsSet(lib, drive);
    if (linked.empty()) return std::string();
    if (hasSettingsSet(lib, linked)) return linked;
    if (dangling) *dangling = true;
    return std::string();
}

std::vector<std::string> drivesUsingSettingsSet(const Library& lib, const std::string& set)
{
    std::vector<std::string> out;
    for (const std::string& d : lib.listDrives())
        if (driveSettingsSet(lib, d) == set) out.push_back(d);
    return out;
}

// ---- migration ----------------------------------------------------------------------------------------------------------------------

bool migrateFlatSettings(const fs::path& libraryRoot, std::string* err)
{
    const fs::path dir = libraryRoot / "user-settings";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return true;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        std::string n = e.path().filename().string();
        if (!n.empty() && n[0] != '.' && e.is_directory(ec)) return true;          // sets exist already
    }
    std::vector<std::string> flat;
    for (const std::string& f : settingsFileNames())
        if (fs::is_regular_file(dir / f, ec)) flat.push_back(f);
    if (flat.empty()) return true;

    const fs::path set = dir / "imported";
    fs::create_directories(set, ec);
    if (ec) return fail(err, "cannot create " + set.string() + ": " + ec.message());
    for (const std::string& f : flat) {
        fs::rename(dir / f, set / f, ec);
        if (ec) return fail(err, "cannot move " + f + " into " + set.string() + ": " + ec.message());
    }
    SettingsSet info;
    info.created = nowIso();
    info.source = "migrated";
    return writeSetInfo(set, info, err) && atomicWrite(dir / "default.txt", "set: imported\n", err);
}

} // namespace trakcrate
