#ifndef MASTER_BEAT_MATH_H
#define MASTER_BEAT_MATH_H

#include <cstdint>

namespace vdj {

// Pure maths for sending beat packets from ALSA echo events (see AlsaClock). No ALSA, no threads.
// as_master_difflock: positive offset = the packet is sent that many ms AFTER the MIDI clock beat (the sequencer is early),
// negative = before it; the same meaning as the difflock target.

// Tick (96 PPQ) at which the beat echo for whole beat `beat` is queued: half a beat before that beat.
int64_t beatEchoTick(int64_t beat);

// Queue real time (ns) at which the send echo for whole beat `beat` is due:
//   realNowNs + (beat - beatsNow) * 60e9 / bpm + clamped offset
// beatsNow / realNowNs are one consistent reading of the queue. False for bpm <= 0 or beatsNow < 0.
bool sendEchoRealNs(double beatsNow, int64_t realNowNs, double bpm, int offsetMs, int64_t beat, int64_t* outNs);

// The offset limited to +/-0.45 of a beat period at bpm (the lead the beat echo gives); 0 when bpm <= 0.
int clampAsMasterOffsetMs(int offsetMs, double bpm);

// The beat number and bar position (1..4) in the two 32-bit words of an echo event.
void packBeatEcho(int64_t beat, int barPos, uint32_t out[2]);
void unpackBeatEcho(const uint32_t in[2], int64_t* beat, int* barPos);

} // namespace vdj

#endif // MASTER_BEAT_MATH_H
