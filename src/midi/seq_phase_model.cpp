#include "seq_phase_model.h"
#include <cmath>

namespace vdj {

void SeqPhaseModel::reset(int64_t realNs, double beats, double usPerBeat)
{
    valid_ = usPerBeat > 0.0;
    refNs_ = realNs;
    refBeats_ = beats;
    usPerBeat_ = usPerBeat;
}

double SeqPhaseModel::beatsAt(int64_t realNs) const
{
    if (!valid_)
        return -1.0;
    return refBeats_ + double(realNs - refNs_) / (usPerBeat_ * 1000.0);
}

void SeqPhaseModel::setTempo(int64_t realNs, double usPerBeat)
{
    if (!valid_ || !(usPerBeat > 0.0))
        return;
    refBeats_ = beatsAt(realNs);
    refNs_ = realNs;
    usPerBeat_ = usPerBeat;
}

bool SeqPhaseModel::consistentWith(double tickBeats, int64_t realNs, double tolTicks) const
{
    if (!valid_)
        return false;
    const double d = (beatsAt(realNs) - tickBeats) * kPpq;   // 0 up to 1 for an exact model
    return std::fabs(d - 0.5) <= tolTicks;
}

} // namespace vdj
