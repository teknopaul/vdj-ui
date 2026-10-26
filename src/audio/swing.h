#ifndef SWING_H
#define SWING_H

namespace vdj {

// Swing for tools that play on quarter beats (16 steps to a bar): every second step - the 2nd and 4th sixteenth of each beat,
// the odd 0-based indices - is delayed. The amount is 0..127: 0 = on the grid, 127 = a quarter of a beat = one step, so a delayed
// note lands where the next step is. The same in every beat, not per bar.
constexpr double kSwingMaxDelayBeats = 0.25;
constexpr int kSwingMax = 127;

int swingClamp(int swing);                           // 0..127

// Delay in beats of the note on quarter beat `quarterBeatIndex` (counted from any beat line; negative indices work).
double swingDelayBeats(int quarterBeatIndex, int swing);

} // namespace vdj

#endif // SWING_H
