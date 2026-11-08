#ifndef TRAKCRATE_EDIT_RENDER_H
#define TRAKCRATE_EDIT_RENDER_H

#include "audio_decode.h"
#include "edit_plan.h"

#include <atomic>

// Renders an EditPlan (edit_plan.h) from decoded audio, no Qt. The audio around every join is always read from the untouched source, so
// the tail of a cut, the pre-roll of a crossfade and the like are the original material. Fades that would not fit in the audio around a
// join are shortened (never reads outside the source).
namespace trakcrate {

// False when cancelled (cancel may be null) or when the plan does not fit the source. out is replaced.
bool renderEdit(const DecodedPcm& src, const EditPlan& plan, const BeatGrid& grid, DecodedPcm* out, const std::atomic<bool>* cancel = nullptr);

// One pass of [startBeat, startBeat + lenBeats) made safe to repeat end to end: the join end -> start is applied inside the sample
// (cut: fades at both ends; cross: the last N beats are the pre-roll crossfade into the start; fadeout: last N beats faded, 5 ms in).
// The length is exactly the loop's. Empty when the range does not fit the source or the algo is not Cut, Cross or Fadeout.
DecodedPcm renderLoopSample(const DecodedPcm& src, const BeatGrid& grid, long long startBeat, long long lenBeats, EditAlgo algo, double param);

} // namespace trakcrate

#endif
