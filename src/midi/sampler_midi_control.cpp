#include "sampler_midi_control.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <memory>

namespace vdj {

bool SamplerMidiControl::installExample(const QString& path)
{
    if (QFile::exists(path))
        return true;
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!QFile::copy(examplePath(), path))
        return false;
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);   // a resource copy is read-only
    return true;
}

bool SamplerMidiControl::loadFromFile(const QString& path)
{
    std::unique_ptr<AdjmmMap> m(AdjmmMap::readFromFile(path));
    if (!m) return false;
    map_ = SamplerCtlMap::fromAdjmm(*m);
    path_ = path;
    return true;
}

bool SamplerMidiControl::sourceAccepted(const QString& source) const
{
    const QString filter = map_.source();
    return filter.isEmpty() || source.contains(filter, Qt::CaseInsensitive);
}

void SamplerMidiControl::act(const ControlAction& a)
{
    switch (a.kind) {
        case ControlAction::Strip:
            if (a.control == Control::Knob)       emit knobMoved(a.strip, a.value);
            else if (a.control == Control::Fader) emit faderMoved(a.strip, a.value);
            else                                  emit buttonPressed(a.control, a.strip);
            break;
        case ControlAction::TrackBack:    emit trackBack(); break;
        case ControlAction::TrackForward: emit trackForward(); break;
        case ControlAction::ModeToggle:   emit modeToggleRequested(); break;
        default: break;
    }
}

void SamplerMidiControl::onControlEvent(int channel, int controller, int value, const QString& source)
{
    if (!enabled_ || !sourceAccepted(source)) return;
    emit eventSeen(QStringLiteral("ch %1  CC %2 = %3").arg(channel).arg(controller).arg(value));
    if (learning_) {
        emit learned(channel, controller, false, value);
        return;
    }
    act(map_.decodeControl(channel, controller, value));
}

void SamplerMidiControl::onNoteEvent(int channel, int note, int velocity, bool on, const QString& source)
{
    if (!enabled_ || !sourceAccepted(source)) return;
    emit eventSeen(QStringLiteral("ch %1  NOTE %2 %3 velocity %4").arg(channel).arg(note).arg(on ? "on" : "off").arg(velocity));
    if (learning_) {
        if (on && velocity > 0) emit learned(channel, note, true, velocity);
        return;
    }
    act(map_.decodeNote(channel, note, velocity, on));
}

} // namespace vdj
