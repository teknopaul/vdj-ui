#ifndef DJTOOLZ_CONTROLLER_H
#define DJTOOLZ_CONTROLLER_H

#include <QHash>
#include <QObject>
#include "midi/pickup.h"
#include "midi/djtoolz_midi_control.h"

namespace vdj {

class DjtoolzToolWidget;
class DjtoolzPanel;

// Glue between a MIDI controller (DjtoolzMidiControl) and the DJ Tools panel. One mode at a time:
// Tools: strip n drives tool n (mute = on/off, solo = green button, rec/select = next/previous sample, knob = offset, fader = volume; an FX tool: knob = its first control, fader = its second).
// DrumMachine: strip n drives voice n of a DR-6S (added with the DR-6S mode).
// Faders and knobs use soft takeover (Pickup): after a mode change, a preset load or a change made with the mouse they do nothing
// until they reach the on-screen value.
class DjtoolzController : public QObject {
    Q_OBJECT

public:
    enum Mode { Tools = 0, DrumMachine = 1 };

    explicit DjtoolzController(DjtoolzPanel* panel);

    Mode mode() const { return mode_; }
    void setMode(Mode mode);
    void attach(DjtoolzMidiControl* control);
    DjtoolzMidiControl* control() const { return control_; }

    // The panel calls these when the software value changed other than through the controller (mouse, preset)
    void softwareChanged(int toolId);
    void resetPickups();

signals:
    void modeChanged(int mode);

private:
    void onButton(Control control, int strip);
    void onKnob(int strip, int value);
    void onFader(int strip, int value);
    void onTrack(int dir);
    void fxKnob(DjtoolzToolWidget* w, int strip, int control, const char* kind, int value);
    bool pickup(const QString& key, int hardware, int software, int* out);
    DjtoolzToolWidget* drumTarget();           // re-arms the pickups when the target changed
    bool recIsForward() const { return !control_ || control_->map().recIsForward(); }

    DjtoolzPanel* panel_;
    DjtoolzMidiControl* control_ = nullptr;
    Mode mode_ = Tools;
    int targetId_ = -1;                              // the DR-6S driven last
    bool ownChange_ = false;                         // set while the controller itself writes a value
    QHash<QString, Pickup> pickups_;                 // "<mode>:<strip>:<k|f>"
};

class MidiMapInput;

// Connects the raw events of the MIDI port reader to the typed controller signals and the controller to the panel.
// The events arrive on the reader thread; the connections are queued onto the GUI thread (the objects live there).
void wireDjtoolzControl(MidiMapInput* input, DjtoolzPanel* panel, DjtoolzMidiControl* control);

} // namespace vdj

#endif // DJTOOLZ_CONTROLLER_H
