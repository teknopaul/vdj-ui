#include "seq_bar.h"
#include <cmath>

namespace vdj {

int wrapOffset(int offset)
{
    return ((offset % 4) + 4) % 4;
}

int barPos(double barBeats)
{
    return wrapOffset(int(std::floor(barBeats))) + 1;
}

double beatsToNextBeat(double beats)
{
    return std::ceil(beats) - beats;
}

double beatsToWallMs(double beats, double bpm)
{
    return bpm > 0.0 ? beats * 60000.0 / bpm : 0.0;
}

} // namespace vdj
