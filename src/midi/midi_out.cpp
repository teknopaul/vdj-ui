#include "midi_out.h"
#include "alsa_clock.h"

#include <algorithm>
#include <cmath>

namespace vdj {

std::vector<MidiNoteEvent> buildNoteEvents(double beat, const ToolSettings& s, double bpm, int toolId)
{
    std::vector<MidiNoteEvent> out;
    if (bpm <= 0.0 || s.volume <= 0)
        return out;
    const double offsetTicks = s.offsetMs * bpm * AlsaClock::kPpq / 60000.0;
    const qint64 on = std::max<qint64>(0, std::llround(beat * AlsaClock::kPpq + offsetTicks));
    MidiNoteEvent e;
    e.channel = std::clamp(s.midiChannel, 1, 16) - 1;
    e.note = std::clamp(s.midiNote, 0, 127);
    e.device = s.midiDevice;
    e.tag = toolId;
    e.kind = MidiNoteEvent::NoteOn;
    e.tick = on;
    e.velocity = std::clamp(int(std::lround(double(s.midiVelocity) * s.volume / DjtoolzTool::kMaxVolume)), 1, 127);
    out.push_back(e);
    e.kind = MidiNoteEvent::NoteOff;
    e.tick = on + AlsaClock::kPpq / 4;
    e.velocity = 0;
    out.push_back(e);
    return out;
}

bool parseMidiAddress(const QString& text, int* client, int* port)
{
    const QStringList parts = text.trimmed().split(QLatin1Char(':'));
    if (parts.size() != 2)
        return false;
    bool ok1 = false, ok2 = false;
    const int c = parts[0].toInt(&ok1), p = parts[1].toInt(&ok2);
    if (!ok1 || !ok2 || c < 0 || c > 255 || p < 0 || p > 255)
        return false;
    if (client) *client = c;
    if (port) *port = p;
    return true;
}

bool MidiOut::send(const MidiNoteEvent& e) const
{
    if (!clock_ || !clock_->isOpen())
        return false;
    int c = -1, p = 0;
    if (!e.device.isEmpty() && !parseMidiAddress(e.device, &c, &p))
        return false;                              // a device name that is not an address: do not spray the subscribers
    return clock_->sendDjtoolzMidi(int(e.kind), e.channel, e.note, e.velocity, e.tick, c, p);
}

} // namespace vdj
