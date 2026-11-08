#include "anlz_writer.h"

#include <algorithm>
#include <cmath>

namespace trakcrate {

namespace {

void be32(std::string& s, uint32_t v) { for (int i = 3; i >= 0; --i) s += char((v >> (8 * i)) & 0xFF); }
void be16(std::string& s, uint16_t v) { s += char(v >> 8); s += char(v & 0xFF); }
void setBe32(std::string& s, size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) s[at + size_t(i)] = char((v >> (8 * (3 - i))) & 0xFF); }

// A tag: fourcc, header length, tag length, then headerBody (the header after the first 12 bytes) and body.
void tag(std::string& out, const char* fourcc, const std::string& headerBody, const std::string& body)
{
    out.append(fourcc, 4);
    be32(out, uint32_t(12 + headerBody.size()));
    be32(out, uint32_t(12 + headerBody.size() + body.size()));
    out += headerBody;
    out += body;
}

std::string utf16be(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0xFFFD;
        size_t n = 1;
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) { cp = (c & 0x1F) << 6 | (s[i + 1] & 0x3F); n = 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) { cp = (c & 0x0F) << 12 | (s[i + 1] & 0x3F) << 6 | (s[i + 2] & 0x3F); n = 3; }
        else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) { cp = (c & 0x07) << 18 | (s[i + 1] & 0x3F) << 12 | (s[i + 2] & 0x3F) << 6 | (s[i + 3] & 0x3F); n = 4; }
        i += n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            be16(o, uint16_t(0xD800 + (cp >> 10)));
            be16(o, uint16_t(0xDC00 + (cp & 0x3FF)));
        } else {
            be16(o, uint16_t(cp));
        }
    }
    return o;
}

void ppth(std::string& out, const std::string& path)
{
    std::string p = utf16be(path);
    p += std::string(2, '\0');                       // NUL terminated
    std::string hb;
    be32(hb, uint32_t(p.size()));
    tag(out, "PPTH", hb, p);
}

void pqtz(std::string& out, const std::vector<AnlzBeat>& beats, int16_t shiftMs)
{
    std::string hb;
    be32(hb, 0);
    be32(hb, 0x00080000 | uint16_t(shiftMs));   // 0x0008 unknown, the same in every real file; low half = grid shift
    be32(hb, uint32_t(beats.size()));
    std::string body;
    for (const AnlzBeat& b : beats) { be16(body, b.number); be16(body, b.bpmX100); be32(body, b.timeMs); }
    tag(out, "PQTZ", hb, body);
}

void pqt2(std::string& out, const std::vector<AnlzBeat>& beats, const std::vector<uint16_t>& sub)
{
    std::string hb;
    be32(hb, 0);
    be32(hb, 0x01000002);                            // constant in every real file that has a grid
    be32(hb, 0);
    const AnlzBeat& f = beats.front();
    const AnlzBeat& l = beats.back();
    be16(hb, f.number); be16(hb, f.bpmX100); be32(hb, f.timeMs);
    be16(hb, l.number); be16(hb, l.bpmX100); be32(hb, l.timeMs);
    be32(hb, uint32_t(beats.size()));
    be32(hb, 0);                                     // 0x2c: differs in every real file (a hash?); unknown, written as 0
    be32(hb, 0);
    be32(hb, 0);
    std::string body;
    for (size_t i = 0; i < beats.size(); ++i) be16(body, i < sub.size() ? sub[i] : 0);
    tag(out, "PQT2", hb, body);
}

void cueEntryPcpt(std::string& e, const AnlzCue& c)
{
    e.append("PCPT", 4);
    be32(e, 0x1c);
    be32(e, 0x38);
    be32(e, uint32_t(c.hotCue));
    if (c.seek) {                                    // as written for a VBR mp3 by a current rekordbox
        be32(e, 0);
        be32(e, 0x00010000);
        be32(e, 0xffffffff);
        e += char(c.isLoop ? 2 : 1);
        e += char((c.marker >> 16) & 0xFF); e += char((c.marker >> 8) & 0xFF); e += char(c.marker & 0xFF);
        be32(e, c.timeMs);
        be32(e, c.isLoop ? c.loopTimeMs : 0xffffffff);
        be32(e, uint32_t(uint64_t(c.timeMs) * 75 / 1000));
        be32(e, 0);
        be32(e, c.bytePos);
        be32(e, 0);
        return;
    }
    be32(e, 1);                                      // status: 1 in every sample
    be32(e, 0);
    be32(e, 0xffffffff);
    e += char(c.isLoop ? 2 : 1);
    e += char((c.marker >> 16) & 0xFF); e += char((c.marker >> 8) & 0xFF); e += char(c.marker & 0xFF);
    be32(e, c.timeMs);
    if (c.isLoop) {                                  // the three patterns seen: loop, plain cue, and the first cue "A" with marker 150
        be32(e, c.loopTimeMs); be32(e, 0xffffffff); be32(e, 0xffffffff); be32(e, 0xffffffff); be32(e, 0xffffffff);
    } else if (c.marker == 150) {
        be32(e, 0); be32(e, 2); be32(e, 0); be32(e, 0); be32(e, 0);
    } else {
        be32(e, 0); be32(e, 0xffffffff); be32(e, 0); be32(e, 0xffffffff); be32(e, 0);
    }
}

void cueEntryPcp2(std::string& e, const AnlzCue& c)
{
    e.append("PCP2", 4);
    be32(e, 0x10);
    be32(e, c.seek ? 0x58 : 0x2c);
    be32(e, uint32_t(c.hotCue));
    e += char(c.isLoop ? 2 : 1);
    e += char((c.marker >> 16) & 0xFF); e += char((c.marker >> 8) & 0xFF); e += char(c.marker & 0xFF);
    be32(e, c.timeMs);
    be32(e, c.isLoop ? c.loopTimeMs : (c.seek ? 0xffffffff : 0));
    be32(e, c.seek ? 0x00010000 : 0);                // 0x1c colour id and bytes: none
    be32(e, 0);
    be16(e, c.isLoop ? c.loopNum : 0);
    be16(e, c.isLoop ? c.loopDen : 0);
    be32(e, 0);                                      // comment length: no comment
    if (c.seek) {                                    // the current entry is 44 bytes longer: a word 0000ff00, then zeros
        be32(e, 0x0000ff00);
        e.append(40, '\0');
    }
}

// A current rekordbox lists the cues of a VBR mp3 from the highest hot cue down; the older sample files list them in order.
std::vector<AnlzCue> inRekordboxOrder(std::vector<AnlzCue> cues)
{
    if (!cues.empty() && cues.front().seek) std::stable_sort(cues.begin(), cues.end(), [](const AnlzCue& a, const AnlzCue& b) { return a.hotCue > b.hotCue; });
    return cues;
}

void pcob(std::string& out, int listType, const std::vector<AnlzCue>& cues)
{
    std::string hb;
    be32(hb, uint32_t(listType));
    be16(hb, 0);
    be16(hb, uint16_t(cues.size()));
    be32(hb, 0xffffffff);
    std::string body;
    for (const AnlzCue& c : inRekordboxOrder(cues)) cueEntryPcpt(body, c);
    tag(out, "PCOB", hb, body);
}

void pco2(std::string& out, int listType, const std::vector<AnlzCue>& cues)
{
    std::string hb;
    be32(hb, uint32_t(listType));
    be16(hb, uint16_t(cues.size()));
    be16(hb, 0);
    std::string body;
    for (const AnlzCue& c : inRekordboxOrder(cues)) cueEntryPcp2(body, c);
    tag(out, "PCO2", hb, body);
}

} // namespace

bool buildAnlz(const AnlzTrackData& d, AnlzFiles* out, std::string* err)
{
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (d.beats.size() > 65535) return fail("too many beats");
    if (d.pwav.size() != 400) return fail("PWAV needs 400 columns");
    if (d.pwv2.size() != 100) return fail("PWV2 needs 100 columns");
    if (d.includePwv5 && d.pwv5.size() < d.pwv3.size()) return fail("PWV5 needs at least as many columns as PWV3");
    if (!d.pwv4.empty() && d.pwv4.size() != 1200 * 6) return fail("PWV4 needs 1200 columns of 6 bytes");
    if (!d.beatSubUs.empty() && d.beatSubUs.size() != d.beats.size()) return fail("one microsecond fraction per beat is needed");

    std::vector<AnlzCue> hot3(d.hotCues.begin(), d.hotCues.begin() + long(std::min<size_t>(3, d.hotCues.size())));

    std::string dat;
    dat.append("PMAI", 4);
    be32(dat, 0x1c); be32(dat, 0); be32(dat, 0); be32(dat, 0x00010000); be32(dat, 0x00010000); be32(dat, 0);
    ppth(dat, d.audioPath);
    if (d.includePvbr) {   // PVBR: the seek index of variable bitrate files. All zero is what a constant bitrate file has; a VBR mp3 gets its frame offsets (mp3_index.h).
        std::string hb;
        be32(hb, 0);
        std::string body(1604, '\0');
        if (d.pvbr.size() == 401) { body.clear(); for (uint32_t w : d.pvbr) be32(body, w); }
        tag(dat, "PVBR", hb, body);
    }
    pqtz(dat, d.beats, d.gridShiftMs);
    {
        std::string hb;
        be32(hb, 400); be32(hb, 0x00010000);
        tag(dat, "PWAV", hb, std::string(d.pwav.begin(), d.pwav.end()));
        hb.clear();
        be32(hb, 100); be32(hb, 0x00010000);
        tag(dat, "PWV2", hb, std::string(d.pwv2.begin(), d.pwv2.end()));
    }
    pcob(dat, 1, hot3);
    pcob(dat, 0, d.memoryCues);
    setBe32(dat, 8, uint32_t(dat.size()));

    std::string ext;
    ext.append("PMAI", 4);
    be32(ext, 0x1c); be32(ext, 0); be32(ext, 0); be32(ext, 0x00010000); be32(ext, 0x00010000); be32(ext, 0);
    ppth(ext, d.audioPath);
    {
        std::string hb;
        be32(hb, 1); be32(hb, uint32_t(d.pwv3.size())); be32(hb, 0x00960000);
        tag(ext, "PWV3", hb, std::string(d.pwv3.begin(), d.pwv3.end()));
    }
    // the .EXT has PCOB lists that are empty even when there are cues (the cues live in PCO2 there)
    pcob(ext, 1, std::vector<AnlzCue>());
    pcob(ext, 0, std::vector<AnlzCue>());
    pco2(ext, 1, d.hotCues);
    pco2(ext, 0, d.memoryCues);
    if (!d.beats.empty()) pqt2(ext, d.beats, d.beatSubUs);
    if (d.includePwv5) {
        std::string hb, body;
        be32(hb, 2); be32(hb, uint32_t(d.pwv5.size())); be32(hb, 0x00960305);
        for (uint16_t v : d.pwv5) be16(body, v);
        tag(ext, "PWV5", hb, body);
    }
    if (!d.pwv4.empty()) {
        std::string hb;
        be32(hb, 6); be32(hb, 1200); be32(hb, 0);
        tag(ext, "PWV4", hb, std::string(d.pwv4.begin(), d.pwv4.end()));
    }
    setBe32(ext, 8, uint32_t(ext.size()));
    out->dat = dat;
    out->ext = ext;
    return true;
}

std::vector<AnlzBeat> beatsFromGrid(const BeatGrid& grid, double durationMs, std::vector<uint16_t>* sub)
{
    std::vector<AnlzBeat> beats;
    if (sub) sub->clear();
    if (grid.empty() || durationMs <= 0) return beats;
    const double periodUs = 60e6 / grid.bpm;
    const uint16_t tempo = uint16_t(std::lround(grid.bpm * 100.0));
    for (size_t i = 0;; ++i) {
        const double us = double(grid.firstBeatUs) + double(i) * periodUs;
        if (us / 1000.0 > durationMs || beats.size() >= 65535) break;
        if (us < 0) continue;                        // a beat before the start of the audio has no time in an unsigned field
        const long long t = std::llround(us);
        AnlzBeat b;
        b.number = uint16_t(i % 4 + 1);
        b.bpmX100 = tempo;
        b.timeMs = uint32_t(t / 1000);
        beats.push_back(b);
        if (sub) sub->push_back(uint16_t(t % 1000));
    }
    return beats;
}

void setWaveforms(AnlzTrackData* d, const Waveform& w, bool includePwv4)
{
    d->pwav = w.pwav;
    d->pwv2 = w.pwv2;
    d->pwv3 = w.pwv3;
    d->pwv5 = w.pwv5;
    d->pwv4.clear();
    if (!includePwv4) return;
    // PWV4 approximation (layout only partly understood): byte 0 the height 0..127, byte 1 about 255 minus it, bytes 2 and 3 follow the
    // loudness, bytes 4 and 5 the green and blue weights. Built from the detail waveform, 1200 columns over the whole track.
    const size_t cols = std::min(w.pwv3.size(), w.pwv5.size());       // the columns of PWV3; a longer PWV5 keeps its extra entries only in its own tag
    d->pwv4.assign(1200 * 6, 0);
    if (cols == 0) {
        for (size_t i = 0; i < 1200; ++i) d->pwv4[i * 6 + 1] = 255;
        return;
    }
    for (size_t i = 0; i < 1200; ++i) {
        size_t a = i * cols / 1200, b = std::max(a + 1, (i + 1) * cols / 1200);
        b = std::min(b, cols);
        if (a >= cols) a = cols - 1;
        int h = 0, g = 0, bl = 0;
        for (size_t k = a; k < b; ++k) {
            h = std::max<int>(h, (w.pwv5[k] >> 2) & 0x1F);
            g = std::max<int>(g, (w.pwv5[k] >> 10) & 7);
            bl = std::max<int>(bl, (w.pwv5[k] >> 7) & 7);
        }
        const int h127 = h * 127 / 31;
        uint8_t* e = &d->pwv4[i * 6];
        e[0] = uint8_t(h127);
        e[1] = uint8_t(255 - h127);
        e[2] = uint8_t(h127);
        e[3] = uint8_t(h127);
        e[4] = uint8_t(g * 127 / 7);
        e[5] = uint8_t(bl * 127 / 7);
    }
}

} // namespace trakcrate
