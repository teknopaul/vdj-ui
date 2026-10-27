#ifndef TRAKCRATE_ARTWORK_PATHS_H
#define TRAKCRATE_ARTWORK_PATHS_H

#include <cstdint>
#include <string>

namespace trakcrate {

// Where an artwork file lives on the drive and what the database stores for it (table 13): "/PIONEER/Artwork/00001/a1.jpg". Ids go 20 to a
// directory: id 0..19 in 00001, 20..39 in 00002 (so a20.jpg is in 00002). Checked against all 2087 artwork rows of the 2195 track export. The medium sized copy
// that rekordbox also writes next to it is "a1_m.jpg" (from the public documentation; not present in our samples).
std::string artworkPath(uint32_t id, bool medium = false);

// Pixel sizes we write (a guess, see doc/trakcrate/UNKNOWN_BYTES.md).
constexpr int kArtworkSize = 80;      // a real rekordbox drive (2148 images) holds only a<id>.jpg at 80x80, no _m copy

} // namespace trakcrate

#endif
