#ifndef SEQ_BAR_H
#define SEQ_BAR_H

namespace vdj {

// Pure bar arithmetic for the ALSA sequencer. The sequencer is a steady clock; "where we are in the bar" is only a label.
int barPos(double barBeats);                    // 1..4: floor(barBeats) mod 4 + 1, also for negative values
int wrapOffset(int offset);                     // 0..3, negative inputs wrap
double beatsToNextBeat(double beats);           // ceil(b) - b; exactly on a beat -> 0
double beatsToWallMs(double beats, double bpm); // wall ms for that many beats at bpm

} // namespace vdj

#endif // SEQ_BAR_H
