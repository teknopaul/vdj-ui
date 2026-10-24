#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <QList>
#include <QString>

namespace vdj {

struct AudioDevice {
    QString name;           // the PulseAudio sink name, empty = the server's default
    QString description;    // what to show the user
};

// Output devices (sinks) of the PulseAudio server; PipeWire provides the same API through pipewire-pulse. Always
// starts with the default device. Blocks briefly while talking to the server.
QList<AudioDevice> listAudioDevices();

} // namespace vdj

#endif // AUDIO_OUTPUT_H
