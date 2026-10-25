#ifndef SAMPLER_CONTROLLER_H
#define SAMPLER_CONTROLLER_H

#include <QHash>
#include <QObject>
#include "midi/pickup.h"
#include "midi/sampler_midi_control.h"

namespace vdj {

class SamplerModuleWidget;
class SamplerPanel;

// Glue between a MIDI controller (SamplerMidiControl) and the sampler panel. One mode at a time:
// Modules: strip n drives module n (mute = on/off, solo = green button, rec/select = next/previous sample, knob = offset, fader = volume).
// DrumMachine: strip n drives voice n of a DR-6S (added with the DR-6S mode).
// Faders and knobs use soft takeover (Pickup): after a mode change, a preset load or a change made with the mouse they do nothing
// until they reach the on-screen value.
class SamplerController : public QObject {
    Q_OBJECT

public:
    enum Mode { Modules = 0, DrumMachine = 1 };

    explicit SamplerController(SamplerPanel* panel);

    Mode mode() const { return mode_; }
    void setMode(Mode mode);
    void attach(SamplerMidiControl* control);
    SamplerMidiControl* control() const { return control_; }

    // The panel calls these when the software value changed other than through the controller (mouse, preset)
    void softwareChanged(int moduleId);
    void resetPickups();

signals:
    void modeChanged(int mode);

private:
    void onButton(Control control, int strip);
    void onKnob(int strip, int value);
    void onFader(int strip, int value);
    void onTrack(int dir);
    bool pickup(const QString& key, int hardware, int software, int* out);
    SamplerModuleWidget* drumTarget();           // re-arms the pickups when the target changed
    bool recIsForward() const { return !control_ || control_->map().recIsForward(); }

    SamplerPanel* panel_;
    SamplerMidiControl* control_ = nullptr;
    Mode mode_ = Modules;
    int targetId_ = -1;                              // the DR-6S driven last
    bool ownChange_ = false;                         // set while the controller itself writes a value
    QHash<QString, Pickup> pickups_;                 // "<mode>:<strip>:<k|f>"
};

class MidiMapInput;

// Connects the raw events of the MIDI port reader to the typed controller signals and the controller to the panel.
// The events arrive on the reader thread; the connections are queued onto the GUI thread (the objects live there).
void wireSamplerControl(MidiMapInput* input, SamplerPanel* panel, SamplerMidiControl* control);

} // namespace vdj

#endif // SAMPLER_CONTROLLER_H
