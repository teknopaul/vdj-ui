#ifndef TRAKCRATE_SCRIPTS_H
#define TRAKCRATE_SCRIPTS_H

#include "fsutil.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// User scripts (doc/trakcrate/scripts.md), the part that needs no Qt: finding them, judging whether they may run, the environment they get, and the
// snapshots that make a mistake recoverable. Scripts only ever run when the user starts them; nothing here runs anything.
namespace trakcrate {

enum class ScriptKind { Track, Crate };

enum class ScriptState {
    Ready,          // may run
    New,            // never reviewed: review it first
    Changed,        // reviewed once, the file is different now
    Warning,        // world/group writable, or in a directory others can write: not runnable until fixed
    Foreign,        // owned by another user: not runnable
    Disabled        // switched off by the user (the file is untouched)
};

struct ScriptInfo {
    ScriptKind kind = ScriptKind::Track;
    std::string name;                // after the prefix: "fix-titles"
    fs::path path;
    std::string description;         // "# description: ..." on line 2, else <script>.desc
    bool dryRun = false;             // "# dry-run: yes"
    bool selection = false;          // "# selection: yes"
    bool noSnapshot = false;         // "# no-snapshot"
    ScriptState state = ScriptState::Ready;
    std::string sha256;
    std::string warning;             // why it is not Ready, for the UI
    bool runnable() const { return state == ScriptState::Ready; }
};

const char* scriptStateName(ScriptState s);                       // "ready", "new", "changed", "warning", "foreign", "disabled"
fs::path scriptsDir(const fs::path& libraryRoot);                 // <root>/scripts

// reviewed: script name -> the SHA-256 the user reviewed. disabled: names switched off. Only regular executable files with the right prefix count,
// sorted by file name; subdirectories are ignored.
std::vector<ScriptInfo> listScripts(const fs::path& libraryRoot, ScriptKind kind, const std::map<std::string, std::string>& reviewed,
                                    const std::set<std::string>& disabled);

// What a run needs.
struct ScriptRun {
    fs::path libraryRoot;
    fs::path trackDir;               // track scripts
    fs::path selectionFile;          // crate scripts with a selection: a file listing track folders, one per line
    bool dryRun = false;
    int deck = 0;                    // 0 = not from a deck
    long long positionMs = -1;       // < 0 = not loaded in a deck
    std::string trakcrateBin;        // path of the CLI, empty if there is none
};

fs::path scriptWorkDir(const ScriptInfo& s, const ScriptRun& r);                  // the track folder, or the library base
std::vector<std::string> scriptArgs(const ScriptInfo& s, const ScriptRun& r);     // the one argument the convention says
// The TRAKCRATE_* variables to add to the user's environment.
std::vector<std::pair<std::string, std::string>> scriptEnvironment(const ScriptInfo& s, const ScriptRun& r);

// Snapshots: a track script's goes in <track>/.trakcrate-history/<time>-<name>/ (every file except the audio and the history), a crate script's in
// <root>/.trakcrate-history/<time>-<name>/ (the text and waveform files of every track, not audio and not images). Empty path + err on failure.
fs::path snapshotTrack(const fs::path& trackDir, const std::string& scriptName, std::string* err = nullptr);
fs::path snapshotCrate(const fs::path& libraryRoot, const std::string& scriptName, std::string* err = nullptr);
// The newest snapshot below <where>/.trakcrate-history, empty if none.
fs::path lastSnapshot(const fs::path& where);
// Copies the files of a snapshot back over the track/library it was made from (files the script created stay). `where` is the track folder or the
// library base. The snapshot folder is kept, so restoring twice is harmless.
bool restoreSnapshot(const fs::path& snapshot, const fs::path& where, std::string* err = nullptr);

} // namespace trakcrate

#endif
