#ifndef TRAKCRATE_PDB_BUILD_H
#define TRAKCRATE_PDB_BUILD_H

#include "pdb_writer.h"

#include <string>
#include <vector>

// Builds a rekordbox export.pdb for a list of tracks and playlists (doc/trakcrate/pdb-write-notes.md, UNKNOWN_BYTES.md). The structure and every
// byte whose meaning is not known come from a TEMPLATE: a real export.pdb (the 7 track LMMS drive). The caller exports the audio, analysis and
// artwork files; this only produces the database that refers to them.
namespace trakcrate {

struct PlanTrack {
    std::string title, artist, album, genre, label, comment, mixName;
    std::string fileName;            // "song.mp3"
    std::string filePath;            // "/Contents/<folder>/song.mp3": where the exporter puts the audio
    std::string analyzePath;         // "/PIONEER/USBANLZ/P00C/0000D1E2/ANLZ0000.DAT"; empty = build derives one from audioHash
    std::string audioHash;           // hex digest of the audio (source of the derived analyze path; falls back to filePath)
    std::string artworkKey;          // identical keys share one artwork row; empty = no artwork
    std::string artworkPath;         // "/PIONEER/Artwork/00001/a1.jpg" (the exporter picked it for this key)
    std::string dateAdded;           // "YYYY-MM-DD"; empty = the template's date
    uint32_t fileSize = 0, sampleRate = 44100, bitrateKbps = 0, trackNumber = 0, bpmX100 = 0;
    uint32_t discNumber = 0, year = 0, sampleDepth = 16, durationSecs = 0, playCount = 0;
    uint8_t rating = 0;              // 0..5
    uint8_t colorId = 0;
};

struct PlanPlaylist {
    std::string name;
    std::vector<size_t> tracks;      // indexes into ExportPlan::tracks, in play order
};

struct ExportPlan {
    std::vector<PlanTrack> tracks;
    std::vector<PlanPlaylist> playlists;     // all at the top level of the tree
};

struct BuiltPdb {
    std::string bytes;
    std::vector<std::string> analyzePaths;   // as written, one per plan track
    std::vector<std::string> artworkPaths;   // distinct artwork paths in id order (id = index + 1)
    int dataPages = 0;
};

// templateBytes: the bytes of the template export.pdb. False (with err) when the template is unusable or a table cannot be laid out.
bool buildPdb(const ExportPlan& plan, const std::string& templateBytes, BuiltPdb* out, std::string* err = nullptr);

// Reads the built bytes back with libcdj's parsers and compares everything against the plan (counts, titles, ids, paths, playlists).
// Returns the list of differences; empty means the database says what the plan says.
std::vector<std::string> verifyPdb(const std::string& bytes, const ExportPlan& plan, const BuiltPdb& built);

// DeviceSQL string encoding (exposed for tests): short ASCII, long ASCII, or UTF-16LE for anything else.
std::string encodeDeviceSqlString(const std::string& utf8);

} // namespace trakcrate

#endif
