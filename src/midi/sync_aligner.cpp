#include "sync_aligner.h"
#include <cmath>

namespace vdj {

double SyncAligner::wrap(double x, double beatLenMs)
{
    if (!(beatLenMs > 0.0))
        return x;
    x = std::fmod(x + beatLenMs / 2.0, beatLenMs);
    if (x < 0.0)
        x += beatLenMs;
    return x - beatLenMs / 2.0;
}

SyncAligner::Plan SyncAligner::plan(double avgDiffMs, double targetMs, double beatLenMs, double waitToGridMs)
{
    Plan p;
    const double shift = wrap(avgDiffMs - targetMs, beatLenMs);
    if (std::fabs(shift) < kDoneMs)
        return p;
    p.shiftMs = shift;
    p.beats = int(std::ceil(std::fabs(shift) / kStepMs));
    p.waitMs = waitToGridMs > 0.0 ? waitToGridMs : 0.0;
    return p;
}

int SyncAligner::step(const Plan& plan, int i)
{
    if (plan.beats <= 0 || i < 0 || i >= plan.beats)
        return 0;
    const double a = plan.shiftMs * double(i) / double(plan.beats);
    const double b = plan.shiftMs * double(i + 1) / double(plan.beats);
    return int(std::lround(b) - std::lround(a));
}

} // namespace vdj
