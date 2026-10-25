#ifndef MIDI_OUT_H
#define MIDI_OUT_H

#include <QString>
#include <vector>
#include "audio/sampler_module.h"

namespace vdj {

class AlsaClock;

// One MIDI message of a MidiMatic hit, timed in sequencer ticks (AlsaClock::kPpq per beat).
struct MidiNoteEvent {
    enum Kind { NoteOn = 0, NoteOff = 1, AllNotesOff = 2 };
    Kind kind = NoteOn;
    qint64 tick = 0;
    int channel = 0;             // 0..15
    int note = 0;
    int velocity = 0;
    QString device;              // "client:port" (ALSA numbers); empty = whoever is connected to the "Sampler MIDI" port
    int tag = 0;                 // the module's id
};

// The note on / note off pair for a hit at integer `beat`. The offset (ms) is converted to whole ticks, so it is only accurate to
// 1 tick (about 5 ms at 120 BPM). The note off comes a quarter of a beat after the note on, like the ADJ bombomatic.
std::vector<MidiNoteEvent> buildNoteEvents(double beat, const ModuleSettings& s, double bpm, int moduleId = 0);

// "20:0" -> client 20, port 0. False for anything else (empty included).
bool parseMidiAddress(const QString& text, int* client, int* port);

// Sends events to the ALSA sequencer through the VDJ's AlsaClock ("Sampler MIDI" port, same queue as the clock).
class MidiOut {
public:
    explicit MidiOut(AlsaClock* clock) : clock_(clock) {}
    bool send(const MidiNoteEvent& e) const;

private:
    AlsaClock* clock_;
};

} // namespace vdj

#endif // MIDI_OUT_H
