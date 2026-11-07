#ifndef TRAKCRATE_PDB_APPEND_H
#define TRAKCRATE_PDB_APPEND_H

#include "pdb_build.h"
#include "pdb_writer.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Adding a playlist to the export.pdb that is already on a rekordbox (or trakcrate) pendrive, without rebuilding the database. The rules, taken from
// real files, and what is still a guess are in doc/trakcrate/pdb-append-notes.md.
namespace trakcrate {

struct DriveTrack {
    uint32_t id = 0;
    std::string title, filePath, analyzePath;
    uint32_t fileSize = 0;
};

// What an existing database holds, indexed for the code that adds to it. Built by loadDriveDb, which also checks the database.
struct DriveDb {
    PdbFile file;                                                       // the whole file, unchanged
    std::map<uint32_t, uint32_t> maxId;                                 // per table type: the highest id of any row, present or deleted
    std::map<std::string, uint32_t> artist, genre, label, artwork;      // exact name (artwork: path) -> id of a present row
    std::map<std::pair<uint32_t, std::string>, uint32_t> album;         // (artist id, name) -> id
    std::vector<DriveTrack> tracks;                                     // present rows
    std::map<std::string, size_t> trackByPath;                          // lower case file path -> index into tracks
    std::set<std::string> analyzePaths;                                 // of every track row, present or deleted
    std::map<std::string, uint32_t> playlistByName;                     // lower case name -> id, present playlists
    std::set<uint32_t> playlistIds;                                     // present playlists
    uint32_t maxRootSortOrder = 0;
    uint32_t artworkCount = 0;
    std::vector<std::string> warnings;                                  // things that are odd but do not stop an addition
};

// Parses and checks an export.pdb. False with the reasons in *problems (every problem found, each naming a table, page or row) when the database is
// not one this module can safely add to; `out` is filled as far as it got. Deleted rows, pages flagged 0x34, wholly deleted pages and play history
// are normal and accepted. Reading never changes anything.
bool loadDriveDb(const std::string& bytes, DriveDb* out, std::vector<std::string>* problems);

struct AppendOptions {
    std::string templatePdb;       // bytes of the template export.pdb (the same one full exports use): new track rows copy its unknown bytes
};

struct AppendResult {
    std::string bytes;                          // the new database
    std::vector<uint32_t> trackIds;             // one per track of the plan: the id of its row (a track nobody references gets 0)
    std::vector<bool> trackIsNew;               // false: the track was already on the stick, its row and files are used as they are
    std::vector<std::string> analyzePaths;      // one per plan track, as the database names them
    std::vector<std::string> artworkPaths;      // artwork paths of the new artwork rows
    uint32_t playlistId = 0;
    int rowsAdded = 0, pagesAdded = 0;
    std::vector<uint32_t> touchedPages;         // pages whose bytes changed (including the pages added)
};

// Adds the playlists of the plan to the database, without changing any existing row (doc/trakcrate/pdb-append-notes.md). A plan track whose
// filePath is the path of a track already in the database (case insensitive) is referenced, not added; the caller must only give it that path
// when it is the same audio. New rows get the next free ids (artists, albums, genres, labels and artwork are reused by name), new rows go at the end
// of the table's last page or on a new page taken from the table's empty candidate, and only the pages that gain rows are rewritten.
// `db` is replaced by the new database (reloaded from the result). False with *err and `db` untouched when the playlist name already exists, a row
// cannot be laid out, or the result would not pass loadDriveDb.
bool appendPlaylist(DriveDb& db, const ExportPlan& plan, const AppendOptions& o, AppendResult* out, std::string* err);

// Compares the database before and after an addition and the plan: every row and page of `before` still there and unchanged (index pages may only gain
// their first data page), the new rows read back through libcdj's parser and ours say what the plan says, the playlists' entries are in order and the
// counters agree with the contents. Returns the differences; empty means the addition is what was asked for.
std::vector<std::string> verifyAppended(const std::string& before, const std::string& after, const ExportPlan& plan, const AppendResult& result);

} // namespace trakcrate

#endif
