#ifndef DRUM_PATTERN_H
#define DRUM_PATTERN_H

#include <QString>
#include <QVariantMap>
#include <QtGlobal>

namespace vdj {

constexpr int kDrumVoices = 6;
constexpr int kDrumSteps = 16;                       // a bar in sixteenths = quarter beats

enum DrumStep : quint8 { StepOff = 0, StepOn = 1, StepHalf = 2 };

// BD, SD, SY, CL, HH, OH
extern const char* const kDrumVoiceNames[kDrumVoices];

// How long a hit may ring, in quarter beats (steps): 0 = to the end of the sample. The spinner walks through exactly these.
constexpr int kDrumChokeValues[6] = { 0, 1, 2, 4, 8, 16 };
int drumChokeClamp(int steps);                       // the nearest allowed value (ties go to the lower one)
int drumChokeNext(int current, int dir);             // the next (dir > 0) or previous (dir < 0) allowed value, stops at the ends

struct DrumVoice {
    QString samplePath;
    int volume = 127;                                // 0..127
    double offsetMs = 0.0;
    int chokeSteps = 0;
    bool muted = false;                              // plays nothing while true (a voice that is already ringing finishes)
    quint8 steps[kDrumSteps] = {};                   // DrumStep per step of the bar

    bool operator==(const DrumVoice& o) const;
    bool operator!=(const DrumVoice& o) const { return !(*this == o); }
};

QString drumPatternToString(const quint8 steps[kDrumSteps]);                 // 16 characters of '0' '1' '2'
bool drumPatternFromString(const QString& text, quint8 out[kDrumSteps]);     // false (out untouched) for a wrong length or character
// One voice as a QVariantMap { sample, volume, offsetMs, choke, pattern } (tool presets and drum kits use the same form);
// fromVariant keeps the voice's current value for every key the map does not have, a bad pattern string leaves the pattern as it is.
// "muted" is written only when true (old files stay identical); drum kits leave it out (withMute = false): a kit is a sound, not a mix.
QVariantMap drumVoiceToVariant(const DrumVoice& v, bool withMute = true);
void drumVoiceFromVariant(const QVariantMap& m, DrumVoice* v);
float drumStepLevel(quint8 step);                    // 0 off, 1 on, 0.5 half volume
double drumStepBeat(int step);                       // step / 4

} // namespace vdj

#endif // DRUM_PATTERN_H
