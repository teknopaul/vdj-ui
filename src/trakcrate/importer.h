#ifndef TRAKCRATE_IMPORTER_H
#define TRAKCRATE_IMPORTER_H

#include "library.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace trakcrate {

enum class ImportMode { Copy, Move, Link };

struct ImportOptions {
    bool recursive = false;           // subfolders: each becomes its own master folder "<folder> - <sub path>"
    ImportMode mode = ImportMode::Copy;
    std::string folder;               // master folder for files directly in the source (default: the source folder's name)
    std::string playlist;             // default: the source folder's name
    bool noPlaylist = false;
    bool append = false;              // playlist exists: add the new tracks at the end (already present tracks are not added again)
    bool replace = false;             // playlist exists: delete it first
    bool dedupe = true;               // a file whose content is already in the master is not stored again
    bool dryRun = false;              // report only, write nothing
    bool analyse = false;             // analyse each new track (beat grid, waveform, loudness) right after importing it
};

struct ImportItem {
    fs::path source;
    std::string status;               // imported | duplicate | skipped | failed  (dry run: "would import")
    std::string detail;               // reason for skipped/failed
    fs::path trackDir;                // master folder of the track (imported, duplicate)
};

struct ImportReport {
    bool ok = false;                  // false: nothing useful was done, see error
    bool cancelled = false;
    std::string error;
    std::vector<ImportItem> items;
    int imported = 0, duplicates = 0, skipped = 0, failed = 0;
    std::string playlist;             // name of the playlist written ("" when none)
    std::vector<fs::path> playlistOrder;   // track folders in the order they were added
};

// done, total, current file. Called from the importing thread.
using ImportProgress = std::function<void(int done, int total, const std::string& current)>;

// Imports the .mp3 / .wav / .flac files of dir into the library and builds an alphabetical playlist (natural, case-insensitive order of the
// relative path). Never modifies the source files unless mode is Move. cancel may be null; it is checked between files.
ImportReport importFolder(const Library& lib, const fs::path& dir, const ImportOptions& opts, const ImportProgress& progress = nullptr,
                          const std::atomic<bool>* cancel = nullptr);

} // namespace trakcrate

#endif
