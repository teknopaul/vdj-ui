#ifndef TRAKCRATE_PQTZ_OFFSET_H
#define TRAKCRATE_PQTZ_OFFSET_H

#include <cdj/anlz.h>

#include <cstdint>
#include <cstdio>

namespace trakcrate {

// The grid shift a player saves when the beat grid start is moved on the deck (doc/deck-edits.md). The PQTZ entries of the .DAT stay as exported;
// the shift is a signed 16 bit number of milliseconds in the tag header at 0x12 (0 in a normal export, +414 for a grid start moved from 29 ms to
// 443 ms). It applies to every beat of the grid. libcdj's anlz_beat_at() does not know it yet: add this to the beat times it returns (and ignore
// the PQT2 microsecond fractions, which belong to the unshifted grid). Returns 0 when the file has no PQTZ or cannot be read.
inline int pqtzGridOffsetMs(const char* root, const char* analyzePath)
{
    char path[4096];
    if (anlz_resolve_path(root, analyzePath, ANLZ_FILE_DAT, path, sizeof path) != 0) return 0;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return 0;
    unsigned char buf[4096];
    const size_t n = std::fread(buf, 1, sizeof buf, f);
    std::fclose(f);
    // tags follow the 28 byte file header (its own length is at 4): walk them until PQTZ
    size_t p = n >= 8 ? (size_t(buf[4]) << 24 | size_t(buf[5]) << 16 | size_t(buf[6]) << 8 | buf[7]) : n;
    while (p + 24 <= n) {
        const size_t len = size_t(buf[p + 8]) << 24 | size_t(buf[p + 9]) << 16 | size_t(buf[p + 10]) << 8 | buf[p + 11];
        if (buf[p] == 'P' && buf[p + 1] == 'Q' && buf[p + 2] == 'T' && buf[p + 3] == 'Z')
            return int16_t(uint16_t(buf[p + 0x12] << 8 | buf[p + 0x13]));
        if (len < 12) break;
        p += len;
    }
    return 0;
}

} // namespace trakcrate

#endif
