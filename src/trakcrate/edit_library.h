#ifndef TRAKCRATE_EDIT_LIBRARY_H
#define TRAKCRATE_EDIT_LIBRARY_H

#include "library.h"

#include <atomic>
#include <string>

namespace trakcrate {

struct EditImportResult {
    bool ok = false;
    std::string error;
    fs::path trackDir;              // the new track
    std::string playlist;           // the playlist the edit was inserted into, "" when none
    int playlistIndex = -1;         // where it went (the original follows it)
    std::string note;               // why no playlist was used, "" otherwise
};

// Makes a rendered edit a track of the library, in the same master folder as the original: the rendered file is moved in (tags and artwork as
// importFolder extracts them), the waveform and loudness are analysed, the beat grid is the known one of the edit (origin "edited"), the info
// file is kept as edit.txt, and when a playlist named exactly like the folder holds the original the edit is inserted just before it.
// The original track folder is not touched. On a failure nothing half made is left in the library.
EditImportResult importEditToLibrary(const Library& lib, const fs::path& originalTrackDir, const fs::path& renderedFile, const fs::path& infoFile,
                                     const BeatGrid& outputGrid, const std::atomic<bool>* cancel = nullptr);

} // namespace trakcrate

#endif
