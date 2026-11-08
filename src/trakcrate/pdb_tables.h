#ifndef TRAKCRATE_PDB_TABLES_H
#define TRAKCRATE_PDB_TABLES_H

#include "pdb_writer.h"

#include <cstdint>
#include <string>
#include <vector>

// The small tables of export.pdb that exports used to copy from a template unread: columns (16), menu (17), history (19), history playlists (11)
// and history entries (12). Layouts: doc/trakcrate/pdb-small-tables.md.
namespace trakcrate {

constexpr uint32_t kPdbTableHistoryPlaylists = 11, kPdbTableHistoryEntries = 12, kPdbTableColumns = 16, kPdbTableMenu = 17, kPdbTableHistory = 19;

// Table 16: the labels of the browse categories ("GENRE", "ARTIST", "TRACK" ...). The file stores them between U+FFFA and U+FFFB; name has those removed.
struct PdbColumn {
    uint16_t id = 0;
    uint16_t unknown = 0;            // 0x80 + the position in the list in the samples
    std::string name;
};

// Table 17: which menus the player shows and in what order. categoryId is a column id. rekordcrate changed `hidden` and `sortOrder` on a CDJ-350 and
// the menu followed; some menus never show on that model even when visible. Not tried on any other model.
struct PdbMenuItem {
    uint16_t categoryId = 0;
    uint16_t contentPointer = 0;     // which list the menu opens (2 = artists)
    uint8_t unknown = 0;             // 1 track, 2 artist, 3 album, 5 bpm, 0x63 a generic list
    bool hidden = false;
    uint16_t sortOrder = 0;
};

// Table 19: one row per sync of the stick. rekordbox marks the previous row deleted and writes a new one, so the deleted rows are the history.
struct PdbHistory {
    uint32_t numTracks = 0;          // tracks in the database after that sync
    std::string date;                // "2026-10-16"
    std::string version;             // "1000"
    std::string label;
    bool deleted = false;
};

// Table 11 and 12: the sessions the player recorded (HISTORY 001 ...) and their tracks.
struct PdbHistoryPlaylist {
    uint32_t id = 0;
    std::string name;
};
struct PdbHistoryEntry {
    uint32_t trackId = 0, playlistId = 0, index = 0;
};

// Present rows only (history: also deleted rows, flagged), in page then slot order. A row that cannot be decoded is skipped.
std::vector<PdbColumn> readColumns(const PdbFile& f);
std::vector<PdbMenuItem> readMenu(const PdbFile& f);
std::vector<PdbHistory> readHistory(const PdbFile& f);
std::vector<PdbHistoryPlaylist> readHistoryPlaylists(const PdbFile& f);
std::vector<PdbHistoryEntry> readHistoryEntries(const PdbFile& f);

// What an export should show in the player's menu: one entry per menu, matched by categoryId; menus not listed keep the template's values.
struct MenuSetting {
    uint16_t categoryId = 0;
    bool visible = true;
    uint16_t sortOrder = 0;
};

// Rewrites visibility and sort order in the rows of menu pages (same row size, so the page layout does not change). Returns how many rows changed.
int applyMenuSettings(std::vector<PdbPage>* menuPages, const std::vector<MenuSetting>& settings);

} // namespace trakcrate

#endif
