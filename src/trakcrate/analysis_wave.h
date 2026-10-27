#ifndef TRAKCRATE_ANALYSIS_WAVE_H
#define TRAKCRATE_ANALYSIS_WAVE_H

#include "audio_decode.h"

#include <cstdint>
#include <string>
#include <vector>

namespace trakcrate {

// Pre-rendered waveforms in the shape of the ANLZ tags (doc/trakcrate/waveforms.md). The height scale is FIXED: one absolute curve for every
// track, never normalised per track, so a DJ can see that a track is quiet. The curve and the colour mapping are not yet calibrated against
// rekordbox's own waveforms (see heightFromPeak / bandLevel).
struct Waveform {
    static constexpr int kColumnsPerSecond = 150;
    static constexpr int kPreviewColumns = 400;      // PWAV
    static constexpr int kTinyColumns = 100;         // PWV2

    std::vector<uint8_t> pwav;      // 400: low 5 bits height 0..31, high 3 bits whiteness 0..7
    std::vector<uint8_t> pwv2;      // 100: low 4 bits height 0..15
    std::vector<uint8_t> pwv3;      // 150 per second, encoded as PWAV
    std::vector<uint16_t> pwv5;     // 150 per second: R3 G3 B3 (bits 15..7), height5 (bits 6..2), 2 unused bits
    // PWV4 (colour preview, 1200 x 6 bytes) is not generated yet: its layout is only partly understood.

    size_t columns() const { return pwv3.size(); }
    bool operator==(const Waveform& o) const { return pwav == o.pwav && pwv2 == o.pwv2 && pwv3 == o.pwv3 && pwv5 == o.pwv5; }
};

// peak: absolute sample peak of a column, 0..1. 0 dBFS = 31.
int heightFromPeak(double peak);
// Band peak (0..1) to a 3 bit level.
int bandLevel(double peak);

Waveform analyseWaveform(const DecodedPcm& pcm);

// waveform.bin: "TCWV", u32 version, u32 columns, then sections [4 byte tag][u32 length][bytes] (little endian lengths; PWV5 entries big endian
// like the ANLZ tag).
std::string serialiseWaveform(const Waveform& w);
bool parseWaveform(const std::string& bytes, Waveform* out);

struct Loudness {
    double peakDb = -120;    // sample peak, dBFS
    double rmsDb = -120;     // RMS of both channels over the whole track, dBFS
};
Loudness measureLoudness(const DecodedPcm& pcm);

} // namespace trakcrate

#endif
