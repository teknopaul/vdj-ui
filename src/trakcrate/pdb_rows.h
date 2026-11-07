#ifndef TRAKCRATE_PDB_ROWS_H
#define TRAKCRATE_PDB_ROWS_H

#include "pdb_build.h"
#include "pdb_writer.h"

#include <cstdint>
#include <string>

// The row encoders of pdb_build.cpp, shared by buildPdb (a whole new database) and by the code that adds to an existing one
// (doc/trakcrate/pdb-append-notes.md). Every returned row is padded to a multiple of 4 bytes and a UTF-16 string inside it starts on a 4 byte
// boundary of the row, like in real rekordbox files.
namespace trakcrate {

// The strings of a track row: 21 slots in the order the template row stores them.
constexpr int kTrackFixed = 94, kTrackSlots = 21;

struct TrackTemplate {
    std::string fixed;                         // 94 bytes
    std::string strings[kTrackSlots];          // default encoded strings per slot
    std::string defaultDate;
};

// The first present track row of a database (a real export), whose unknown bytes every new track row copies. False when it has none.
bool loadTrackTemplate(const PdbFile& t, TrackTemplate* out);

std::string trackRow(const TrackTemplate& tpl, const PlanTrack& t, uint32_t id, uint32_t genreId, uint32_t albumId, uint32_t artistId,
                     uint32_t labelId, uint32_t artworkId, const std::string& analyzePath);
std::string nameRow(uint32_t id, const std::string& name);                       // genres, labels, artwork paths
std::string artistRow(uint32_t id, const std::string& name);
std::string albumRow(uint32_t id, uint32_t artistId, const std::string& name);
std::string playlistRow(uint32_t id, uint32_t sortOrder, const std::string& name);
std::string entryRow(uint32_t index, uint32_t trackId, uint32_t playlistId);

// Reads the DeviceSQL string that starts at byte `at` of a row (short ASCII, 0x40 long ASCII or 0x90 UTF-16LE) as UTF-8. False when it does not
// fit in the row or has an unknown kind; `*end` (optional) is the offset just after the string.
bool decodeDeviceSqlString(const std::string& row, size_t at, std::string* utf8, size_t* end = nullptr);

} // namespace trakcrate

#endif
