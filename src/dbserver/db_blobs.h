#ifndef DB_BLOBS_H
#define DB_BLOBS_H

#include <cstdint>
#include <vector>

#include "rb_library.h"

// Binary payloads of the dbserver's analysis replies, built from the libcdj ANLZ structures. The layouts come from the
// captured replies of a real deck (tests/fixtures/dbserver/REPLY_LAYOUTS.md); bytes whose meaning is unknown are
// marked GUESS in the .cpp.

namespace vdj {
namespace db {

// 0x4602: 20 byte head, then 16 bytes per beat (u16 LE beat in bar, u16 LE bpm*100, u32 LE ms, 8 x 0xff). The times are the .DAT entries without
// the grid shift (grid.shift_ms), which goes into the reply's fifth argument.
std::vector<uint8_t> beatGridBlob(const anlz_beatgrid_t& grid);

// 0x4402: 400 x (height, whiteness) from PWAV, then the 100 byte PWV2 tiny preview. Empty when PWAV is missing.
std::vector<uint8_t> waveformPreviewBlob(const TrackAnalysis& a);

// 0x4a02: 20 byte head, then the PWV3 bytes (150 entries per second). Empty when PWV3 is missing.
std::vector<uint8_t> waveformDetailBlob(const TrackAnalysis& a);

// 0x4e02: 76 byte entries; memory points first, then hot cues.
std::vector<uint8_t> extendedCuesBlob(const std::vector<anlz_cue_t>& cues);
constexpr size_t kExtendedCueBytes = 76;

} // namespace db
} // namespace vdj

#endif // DB_BLOBS_H
