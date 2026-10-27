#ifndef TRAKCRATE_ANLZ_WRITER_H
#define TRAKCRATE_ANLZ_WRITER_H

#include "analysis_wave.h"
#include "track.h"

#include <cstdint>
#include <string>
#include <vector>

// Writes rekordbox analysis files (ANLZ0000.DAT and ANLZ0000.EXT; format notes in libcdj/doc-internal/rekordbox-anlz-format.text). The layouts
// were checked tag by tag against the real files of the 7 track drive (tests/test_trakcrate_anlz_roundtrip.cpp). Unknown words use the values
// those files have, or zero where they vary; see doc/trakcrate/UNKNOWN_BYTES.md.
namespace trakcrate {

struct AnlzBeat {
    uint16_t number = 1;             // 1..4 within the bar
    uint16_t bpmX100 = 0;
    uint32_t timeMs = 0;
};

struct AnlzCue {
    int hotCue = 0;                  // 0 = memory point, 1..8 = hot cue A..H
    bool isLoop = false;
    uint32_t timeMs = 0;
    uint32_t loopTimeMs = 0;         // end of the loop
    uint32_t marker = 1000;          // 3 bytes; 150 on the first cue of the second sample drive, 1000 on the others
    uint16_t loopNum = 0, loopDen = 0;       // loop length in beats = num / den (PCO2 only)
};

struct AnlzTrackData {
    std::string audioPath;                       // PPTH: "/Contents/Folder/song.mp3"
    std::vector<AnlzBeat> beats;                 // PQTZ; empty = a track without a grid
    std::vector<uint16_t> beatSubUs;             // PQT2 fractions, one per beat (microseconds after the ms); empty = zeros
    std::vector<uint8_t> pwav;                   // 400 (preview)
    std::vector<uint8_t> pwv2;                   // 100
    std::vector<uint8_t> pwv3;                   // 150/s
    std::vector<uint16_t> pwv5;                  // 150/s
    std::vector<uint8_t> pwv4;                   // 1200 x 6 bytes; empty = no PWV4 tag
    std::vector<AnlzCue> hotCues;                // the .DAT lists the first three, the .EXT all of them
    std::vector<AnlzCue> memoryCues;
    bool includePvbr = true;                     // compatibility testing: leave the PVBR / PWV5 tags out
    bool includePwv5 = true;
};

struct AnlzFiles {
    std::string dat;
    std::string ext;
};

// False (with err) for data the format cannot hold (more than 65535 beats, waveforms of the wrong length ...).
bool buildAnlz(const AnlzTrackData& data, AnlzFiles* out, std::string* err = nullptr);

// A constant tempo grid over durationMs: beat i at first + i x 60/bpm, beat numbers 1..4 from the first beat, the time in whole ms plus
// the microsecond remainder in sub. An empty grid gives no beats.
std::vector<AnlzBeat> beatsFromGrid(const BeatGrid& grid, double durationMs, std::vector<uint16_t>* sub);

// Fills the waveform members of data from an analysis (PWV4 approximated, see doc/trakcrate/UNKNOWN_BYTES.md; includePwv4 = false leaves it out).
void setWaveforms(AnlzTrackData* data, const Waveform& w, bool includePwv4 = true);

} // namespace trakcrate

#endif
