#ifndef TRAKCRATE_ARTWORK_EDIT_H
#define TRAKCRATE_ARTWORK_EDIT_H

#include "fsutil.h"

#include <string>

// Artwork operations on a track folder, without Qt (doc/trakcrate/track-editor.md, "Artwork"). The artwork of a track is artwork.jpg, artwork.jpeg or
// artwork.png. Every change that replaces or removes it first keeps the previous image as .trakcrate-history/artwork.<ext> (one level: the next
// change overwrites it), and marks the artwork group "edited" in source.txt.
namespace trakcrate {

// The artwork file of a track folder; empty when there is none.
fs::path artworkOf(const fs::path& trackDir);

// Puts the image in `src` (jpg or png, checked by its first bytes, not its name) in place of the artwork. Other artwork.* files are removed.
bool replaceArtwork(const fs::path& trackDir, const fs::path& src, std::string* err = nullptr);
// Deletes the artwork (after the backup). True also when there was none.
bool removeArtwork(const fs::path& trackDir, std::string* err = nullptr);
// Takes the cover embedded in the audio file again; false (with err) when the file has none.
bool reextractArtwork(const fs::path& trackDir, std::string* err = nullptr);
// Puts the backup back (the current image becomes the new backup, so calling it twice swaps). False when there is no backup.
bool restoreArtwork(const fs::path& trackDir, std::string* err = nullptr);
// Marks the artwork "edited" without touching the file: for an external editor that saved in place.
bool markArtworkEdited(const fs::path& trackDir, std::string* err = nullptr);
// Keeps the current image as the backup (called before an external editor runs).
bool backupArtwork(const fs::path& trackDir, std::string* err = nullptr);

// "jpg" or "png" from the first bytes, empty if it is neither.
std::string imageExtensionOf(const std::string& bytes);

} // namespace trakcrate

#endif
