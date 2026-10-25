#include "swing.h"

#include <algorithm>

namespace vdj {

int swingClamp(int swing)
{
    return std::clamp(swing, 0, kSwingMax);
}

double swingDelayBeats(int quarterBeatIndex, int swing)
{
    const int odd = ((quarterBeatIndex % 2) + 2) % 2;
    if (odd == 0)
        return 0.0;
    return kSwingMaxDelayBeats * double(swingClamp(swing)) / double(kSwingMax);
}

} // namespace vdj
