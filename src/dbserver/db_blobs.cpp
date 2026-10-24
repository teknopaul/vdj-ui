#include "db_blobs.h"

#include <cstring>

namespace vdj {
namespace db {

namespace {

void putLe32(std::vector<uint8_t>& o, uint32_t v)
{
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
    o.push_back(uint8_t(v >> 16));
    o.push_back(uint8_t(v >> 24));
}

void putLe16(std::vector<uint8_t>& o, uint16_t v)
{
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
}

void setLe32(std::vector<uint8_t>& o, size_t at, uint32_t v)
{
    o[at] = uint8_t(v); o[at + 1] = uint8_t(v >> 8); o[at + 2] = uint8_t(v >> 16); o[at + 3] = uint8_t(v >> 24);
}

} // namespace

std::vector<uint8_t> beatGridBlob(const anlz_beatgrid_t& g)
{
    std::vector<uint8_t> b;
    b.reserve(20 + size_t(g.count) * 16);
    b.insert(b.end(), {0x00, 0x00, 0x08, 0x00});      // constant in every capture
    putLe32(b, g.count);
    putLe32(b, g.count * 16);
    putLe32(b, 1);
    putLe32(b, 0);              // GUESS: looks like a heap pointer on the real deck (0x0ceea950, 0x0ceeeab4); meaningless
    for (uint32_t i = 0; i < g.count; ++i) {
        anlz_beat_t e{};
        anlz_beat_at(&g, i, &e);
        putLe16(b, e.beat_number);
        putLe16(b, e.bpm_x100);
        putLe32(b, e.time_ms);
        for (int k = 0; k < 8; ++k) b.push_back(0xff);        // GUESS: undetermined, always ff in the captures
    }
    return b;
}

std::vector<uint8_t> waveformPreviewBlob(const TrackAnalysis& a)
{
    anlz_waveform_t pw{}, tiny{};
    if (!a.waveform(ANLZ_WAVE_PREVIEW, &pw) || pw.count != 400 || pw.entry_bytes != 1) return {};
    std::vector<uint8_t> b;
    b.reserve(900);
    for (uint32_t i = 0; i < 400; ++i) {
        b.push_back(uint8_t(pw.data[i] & 0x1f));        // height
        b.push_back(uint8_t(pw.data[i] >> 5));          // whiteness
    }
    if (a.waveform(ANLZ_WAVE_TINY, &tiny) && tiny.count == 100 && tiny.entry_bytes == 1) b.insert(b.end(), tiny.data, tiny.data + 100);
    else b.insert(b.end(), 100, 0);
    return b;
}

std::vector<uint8_t> waveformDetailBlob(const TrackAnalysis& a)
{
    anlz_waveform_t w{};
    if (!a.waveform(ANLZ_WAVE_DETAIL, &w) || w.entry_bytes != 1 || w.count == 0) return {};
    std::vector<uint8_t> b;
    b.reserve(20 + w.count);
    putLe32(b, w.count);
    putLe32(b, 1);
    putLe32(b, w.count);
    putLe32(b, 150);
    putLe32(b, 0);              // GUESS: heap pointer on the real deck (0x0cf233a0)
    b.insert(b.end(), w.data, w.data + w.count);
    return b;
}

std::vector<uint8_t> extendedCuesBlob(const std::vector<anlz_cue_t>& cues)
{
    std::vector<uint8_t> b;
    for (const anlz_cue_t& c : cues) {
        const size_t at = b.size();
        b.resize(at + kExtendedCueBytes, 0);
        std::vector<uint8_t> e;
        putLe32(e, uint32_t(kExtendedCueBytes));
        e.push_back(c.hot_cue); e.push_back(0);         // hot cue 0 = memory point, 1 = A ...
        e.push_back(c.is_loop ? 2 : 1); e.push_back(0); // 1 cue, 2 loop
        putLe16(e, 0);
        putLe16(e, uint16_t(c.marker));                 // 150 on the first cue of the sample export, 1000 elsewhere
        putLe32(e, c.time_ms);
        putLe32(e, c.is_loop ? c.loop_time_ms : 0);
        putLe32(e, 0);
        putLe32(e, 1);                                  // 1 in every capture (GUESS: meaning)
        putLe32(e, 0);
        // 0x20..0x33: GUESS. 0xff fill; the captured hot cues had a few zero words in there, a loop had all ff.
        if (c.is_loop) {
            e.insert(e.end(), 20, 0xff);
        } else {
            e.insert(e.end(), 8, 0xff);
            putLe32(e, 0);
            e.insert(e.end(), 4, 0xff);
            putLe32(e, 0);
        }
        putLe32(e, 0x12);                               // 0x34: 0x12 in every capture (GUESS: meaning)
        e.resize(0x42, 0);
        putLe16(e, c.loop_num);                         // 0x42: loop length in beats = num/den
        putLe16(e, c.loop_den);
        e.resize(kExtendedCueBytes, 0);
        std::memcpy(b.data() + at, e.data(), kExtendedCueBytes);
    }
    return b;
}

} // namespace db
} // namespace vdj
