#include "master_beat_math.h"
#include <cmath>

namespace vdj {

namespace {
constexpr int64_t kPpq = 96;                 // ticks per beat, as AlsaClock::kPpq
constexpr double kMaxOffsetBeats = 0.45;     // less than the half beat of lead the beat echo gives
}

int64_t beatEchoTick(int64_t beat)
{
    return beat * kPpq - kPpq / 2;
}

int clampAsMasterOffsetMs(int offsetMs, double bpm)
{
    if (bpm <= 0.0)
        return 0;
    const int limit = int(std::floor(kMaxOffsetBeats * 60000.0 / bpm));
    if (offsetMs > limit) return limit;
    if (offsetMs < -limit) return -limit;
    return offsetMs;
}

bool sendEchoRealNs(double beatsNow, int64_t realNowNs, double bpm, int offsetMs, int64_t beat, int64_t* outNs)
{
    if (bpm <= 0.0 || beatsNow < 0.0 || !outNs)
        return false;
    const double toBeatNs = (double(beat) - beatsNow) * 60e9 / bpm;
    *outNs = realNowNs + int64_t(std::llround(toBeatNs)) + int64_t(clampAsMasterOffsetMs(offsetMs, bpm)) * 1000000;
    return true;
}

// word 0: low 32 bits of the beat; word 1: bits 0-1 = barPos - 1, bits 2-31 = the beat's bits 32-61
void packBeatEcho(int64_t beat, int barPos, uint32_t out[2])
{
    const uint64_t b = uint64_t(beat);
    out[0] = uint32_t(b & 0xffffffffu);
    out[1] = (uint32_t(b >> 32) << 2) | uint32_t((barPos - 1) & 3);
}

void unpackBeatEcho(const uint32_t in[2], int64_t* beat, int* barPos)
{
    if (beat)
        *beat = int64_t((uint64_t(in[1] >> 2) << 32) | in[0]);
    if (barPos)
        *barPos = int(in[1] & 3) + 1;
}

} // namespace vdj
