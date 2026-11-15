#ifndef MIX_WAVEFORM_H
#define MIX_WAVEFORM_H

#include "trakcrate/edit_plan.h"

#include <QVector>

namespace vdj {

// The colour waveform (150 columns per second of the ORIGINAL track, as PWV5) of a track after the studio mix has stretched it by `ratio`
// (> 1 = faster, shorter) and applied the plan (cuts removed, loops repeated; the plan is in frames of the stretched track). 150 columns per second
// of the result. An empty plan means "no regions": the stretched track as it is.
QVector<quint16> editedColorWaveform(const QVector<quint16>& color, const trakcrate::EditPlan& plan, double ratio, long long stretchedFrames);

} // namespace vdj

#endif
