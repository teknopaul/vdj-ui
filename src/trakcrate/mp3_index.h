#ifndef TRAKCRATE_MP3_INDEX_H
#define TRAKCRATE_MP3_INDEX_H

#include <cstdint>
#include <filesystem>
#include <vector>

// Where the frames of a variable bitrate mp3 start. Rekordbox writes this seek information into the analysis files of VBR mp3s so a player can jump
// into the file without scanning it: the PVBR table (ANLZ0000.DAT) and, for every cue point, the byte offset and the time in 1/75 s.
// Checked against a real rekordbox export of a 10564 frame file (decoded in doc/trakcrate/UNKNOWN_BYTES.md): every PVBR word and cue offset of
// that file is reproduced exactly. Offsets count from the first mpeg frame (after the ID3v2 tag), which for a VBR file is its Xing/Info frame.
namespace trakcrate {

namespace fs = std::filesystem;

struct Mp3Index {
    bool ok = false;
    bool vbr = false;                    // has a Xing / VBRI header (a constant bitrate file gets zeros, as before)
    uint32_t sampleRate = 0;
    uint32_t samplesPerFrame = 0;        // 1152 (MPEG 1) or 576 (MPEG 2 / 2.5)
    std::vector<uint32_t> frameStart;    // byte offset of each frame from the first frame

    // Byte offset of the frame rekordbox seeks to for a time: frame floor(ms / frame length) - 8, not before the first frame.
    uint32_t offsetAtMs(uint32_t ms) const;
    // The 401 words of PVBR: the offsets of frame floor((i + 1) x frames / 400) - 8 for i = 0..399, then the total sample count.
    std::vector<uint32_t> pvbr() const;
};

Mp3Index indexMp3(const fs::path& file);

} // namespace trakcrate

#endif
