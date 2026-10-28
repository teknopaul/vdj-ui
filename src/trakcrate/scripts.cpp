#include "scripts.h"

#include "sha256.h"
#include "track.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include <sys/stat.h>
#include <unistd.h>

namespace trakcrate {

namespace {

const char* const kHistory = ".trakcrate-history";

bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool isAudio(const fs::path& p)
{
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    for (const char* a : {".mp3", ".wav", ".flac", ".aiff", ".aif", ".m4a", ".ogg", ".opus"})
        if (e == a) return true;
    return false;
}

bool isImage(const fs::path& p)
{
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e == ".jpg" || e == ".jpeg" || e == ".png";
}

bool fail(std::string* err, const std::string& m)
{
    if (err) *err = m;
    return false;
}

void readHeader(ScriptInfo* s)
{
    std::ifstream in(s->path);
    std::string line;
    for (int n = 1; n <= 20 && std::getline(in, line); ++n) {
        std::string t = trim(line);
        if (n == 1 && startsWith(t, "#!")) continue;
        if (t.empty() || t[0] != '#') {
            if (n > 3) break;
            continue;
        }
        std::string body = trim(t.substr(1));
        auto value = [&](const char* key, std::string* out) {
            const std::string k = std::string(key) + ":";
            if (!startsWith(body, k)) return false;
            *out = trim(body.substr(k.size()));
            return true;
        };
        std::string v;
        if (value("description", &v)) { if (s->description.empty()) s->description = v; }
        else if (value("dry-run", &v)) s->dryRun = v == "yes";
        else if (value("selection", &v)) s->selection = v == "yes";
        else if (body == "no-snapshot") s->noSnapshot = true;
    }
    if (s->description.empty()) {
        std::ifstream d(s->path.string() + ".desc");
        std::string first;
        if (d && std::getline(d, first)) s->description = trim(first);
    }
}

std::string stamp()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%dT%H%M%S", &tm);
    return buf;
}

std::string safeName(const std::string& s)
{
    std::string r = s;
    for (char& c : r)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) c = '_';
    return r;
}

fs::path uniqueDir(const fs::path& parent, const std::string& scriptName)
{
    const std::string base = stamp() + "-" + safeName(scriptName);
    fs::path d = parent / base;
    std::error_code ec;
    for (int n = 2; fs::exists(d, ec); ++n) d = parent / (base + "." + std::to_string(n));
    return d;
}

bool copyInto(const fs::path& from, const fs::path& to, std::string* err)
{
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) return fail(err, "cannot copy " + from.string() + ": " + ec.message());
    return true;
}

} // namespace

const char* scriptStateName(ScriptState s)
{
    switch (s) {
        case ScriptState::Ready: return "ready";
        case ScriptState::New: return "new";
        case ScriptState::Changed: return "changed";
        case ScriptState::Warning: return "warning";
        case ScriptState::Foreign: return "foreign";
        case ScriptState::Disabled: return "disabled";
    }
    return "";
}

fs::path scriptsDir(const fs::path& root) { return root / "scripts"; }

std::vector<ScriptInfo> listScripts(const fs::path& root, ScriptKind kind, const std::map<std::string, std::string>& reviewed,
                                    const std::set<std::string>& disabled)
{
    std::vector<ScriptInfo> out;
    const std::string prefix = kind == ScriptKind::Track ? "track-script-" : "crate-script-";
    const fs::path dir = scriptsDir(root);
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    struct stat dirStat{};
    const bool dirOpen = ::stat(dir.c_str(), &dirStat) == 0 && (dirStat.st_mode & (S_IWGRP | S_IWOTH));
    for (const fs::path& f : files) {
        const std::string fname = f.filename().string();
        if (!startsWith(fname, prefix) || fname.size() == prefix.size()) continue;
        struct stat st{};
        if (::stat(f.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (::access(f.c_str(), X_OK) != 0) continue;                        // the executable bit, for this user
        ScriptInfo s;
        s.kind = kind;
        s.name = fname.substr(prefix.size());
        s.path = f;
        readHeader(&s);
        std::string bytes;
        if (readFile(f, &bytes)) s.sha256 = sha256Hex(bytes);
        if (st.st_uid != ::getuid()) { s.state = ScriptState::Foreign; s.warning = "owned by another user"; }
        else if (st.st_mode & (S_IWGRP | S_IWOTH)) { s.state = ScriptState::Warning; s.warning = "writable by others: chmod go-w " + fname; }
        else if (dirOpen) { s.state = ScriptState::Warning; s.warning = "the scripts folder is writable by others: chmod go-w scripts"; }
        else if (disabled.count(s.name)) s.state = ScriptState::Disabled;
        else {
            auto it = reviewed.find(s.name);
            if (it == reviewed.end()) { s.state = ScriptState::New; s.warning = "new script: review it first"; }
            else if (it->second != s.sha256) { s.state = ScriptState::Changed; s.warning = "changed since it was reviewed"; }
        }
        out.push_back(s);
    }
    return out;
}

fs::path scriptWorkDir(const ScriptInfo& s, const ScriptRun& r) { return s.kind == ScriptKind::Track ? r.trackDir : r.libraryRoot; }

std::vector<std::string> scriptArgs(const ScriptInfo& s, const ScriptRun& r)
{
    return {(s.kind == ScriptKind::Track ? r.trackDir : r.libraryRoot).string()};
}

std::vector<std::pair<std::string, std::string>> scriptEnvironment(const ScriptInfo& s, const ScriptRun& r)
{
    std::vector<std::pair<std::string, std::string>> e;
    e.emplace_back("TRAKCRATE_API", "1");
    e.emplace_back("TRAKCRATE_LIBRARY", r.libraryRoot.string());
    e.emplace_back("TRAKCRATE_SCRIPT_KIND", s.kind == ScriptKind::Track ? "track" : "crate");
    e.emplace_back("TRAKCRATE_SCRIPT_NAME", s.name);
    if (s.kind == ScriptKind::Track) {
        e.emplace_back("TRAKCRATE_TRACK_DIR", r.trackDir.string());
        const fs::path audio = audioFileOf(r.trackDir);
        if (!audio.empty()) e.emplace_back("TRAKCRATE_TRACK_AUDIO", audio.string());
        if (r.positionMs >= 0) e.emplace_back("TRAKCRATE_TRACK_POSITION_MS", std::to_string(r.positionMs));
        if (r.deck > 0) e.emplace_back("TRAKCRATE_DECK", std::to_string(r.deck));
    } else if (!r.selectionFile.empty()) {
        e.emplace_back("TRAKCRATE_SELECTION", r.selectionFile.string());
    }
    if (r.dryRun) e.emplace_back("TRAKCRATE_DRY_RUN", "1");
    if (!r.trakcrateBin.empty()) e.emplace_back("TRAKCRATE_BIN", r.trakcrateBin);
    return e;
}

fs::path snapshotTrack(const fs::path& trackDir, const std::string& scriptName, std::string* err)
{
    const fs::path snap = uniqueDir(trackDir / kHistory, scriptName);
    std::error_code ec;
    fs::create_directories(snap, ec);
    if (ec) { fail(err, "cannot create " + snap.string() + ": " + ec.message()); return fs::path(); }
    for (const auto& e : fs::directory_iterator(trackDir, ec)) {
        if (!e.is_regular_file(ec) || isAudio(e.path())) continue;                // the history and other folders are not files
        if (!copyInto(e.path(), snap / e.path().filename(), err)) return fs::path();
    }
    return snap;
}

fs::path snapshotCrate(const fs::path& root, const std::string& scriptName, std::string* err)
{
    const fs::path snap = uniqueDir(root / kHistory, scriptName);
    std::error_code ec;
    fs::create_directories(snap, ec);
    if (ec) { fail(err, "cannot create " + snap.string() + ": " + ec.message()); return fs::path(); }
    const fs::path master = root / "track-master-copy";
    for (auto it = fs::recursive_directory_iterator(master, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_directory(ec) && it->path().filename() == kHistory) { it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec) || isAudio(it->path()) || isImage(it->path())) continue;
        if (!copyInto(it->path(), snap / "track-master-copy" / it->path().lexically_relative(master), err)) return fs::path();
    }
    return snap;
}

fs::path lastSnapshot(const fs::path& where)
{
    std::error_code ec;
    fs::path best;
    for (const auto& e : fs::directory_iterator(where / kHistory, ec))
        if (e.is_directory(ec) && (best.empty() || e.path().filename() > best.filename())) best = e.path();
    return best;
}

bool restoreSnapshot(const fs::path& snapshot, const fs::path& where, std::string* err)
{
    std::error_code ec;
    if (!fs::is_directory(snapshot, ec)) return fail(err, "no snapshot: " + snapshot.string());
    for (auto it = fs::recursive_directory_iterator(snapshot, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (!copyInto(it->path(), where / it->path().lexically_relative(snapshot), err)) return false;
    }
    return !ec || fail(err, ec.message());
}

} // namespace trakcrate
