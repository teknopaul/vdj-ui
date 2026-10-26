#include "djtoolz_controller.h"

#include "drum_editor.h"
#include "drum_voice_selector.h"
#include "midi/midi_map_input.h"
#include "djtoolz_tool_widget.h"
#include "djtoolz_panel.h"

namespace vdj {

void wireDjtoolzControl(MidiMapInput* input, DjtoolzPanel* panel, DjtoolzMidiControl* control)
{
    QObject::connect(input, &MidiMapInput::controlEvent, control, &DjtoolzMidiControl::onControlEvent);
    QObject::connect(input, &MidiMapInput::noteEvent, control, &DjtoolzMidiControl::onNoteEvent);
    panel->controller()->attach(control);
}

DjtoolzController::DjtoolzController(DjtoolzPanel* panel)
    : QObject(panel)
    , panel_(panel)
{
}

void DjtoolzController::attach(DjtoolzMidiControl* c)
{
    if (control_)
        disconnect(control_, nullptr, this, nullptr);
    control_ = c;
    if (!c)
        return;
    connect(c, &DjtoolzMidiControl::buttonPressed, this, &DjtoolzController::onButton);
    connect(c, &DjtoolzMidiControl::knobMoved, this, &DjtoolzController::onKnob);
    connect(c, &DjtoolzMidiControl::faderMoved, this, &DjtoolzController::onFader);
    connect(c, &DjtoolzMidiControl::trackBack, this, [this]() { onTrack(-1); });
    connect(c, &DjtoolzMidiControl::trackForward, this, [this]() { onTrack(+1); });
    connect(c, &DjtoolzMidiControl::modeToggleRequested, this,
            [this]() { setMode(mode_ == Tools ? DrumMachine : Tools); });
}

void DjtoolzController::setMode(Mode mode)
{
    if (mode == mode_)
        return;
    mode_ = mode;
    resetPickups();
    emit modeChanged(int(mode));
}

void DjtoolzController::resetPickups()
{
    for (Pickup& p : pickups_)
        p.reset();
}

void DjtoolzController::softwareChanged(int toolId)
{
    if (ownChange_)
        return;
    if (mode_ == DrumMachine) {
        if (panel_->drumTarget() && panel_->drumTarget()->tool()->id() == toolId)
            resetPickups();
        return;
    }
    // Only the strips of that tool matter, but a change by the mouse is rare: re-arm the tool's strip
    const auto widgets = panel_->toolWidgets();
    for (int i = 0; i < widgets.size(); ++i)
        if (widgets[i]->tool()->id() == toolId) {
            for (const char* k : { "k", "f" })
                if (pickups_.contains(QStringLiteral("%1:%2:%3").arg(int(Tools)).arg(i + 1).arg(k)))
                    pickups_[QStringLiteral("%1:%2:%3").arg(int(Tools)).arg(i + 1).arg(k)].reset();
        }
}

bool DjtoolzController::pickup(const QString& key, int hardware, int software, int* out)
{
    Pickup& p = pickups_[key];
    p.setEnabledKeepState(!control_ || control_->map().pickup());
    return p.filter(hardware, software, out);
}

DjtoolzToolWidget* DjtoolzController::drumTarget()
{
    DjtoolzToolWidget* w = panel_->drumTarget();
    const int id = w ? w->tool()->id() : -1;
    if (id != targetId_) {
        targetId_ = id;
        resetPickups();
    }
    return w;
}

void DjtoolzController::onButton(Control control, int strip)
{
    if (mode_ == DrumMachine) {
        DjtoolzToolWidget* w = drumTarget();
        if (w && control == Control::Mute && strip >= 1 && strip <= kDrumVoices) {
            ownChange_ = true;
            w->drumEditor()->toggleVoiceMute(strip - 1);
            ownChange_ = false;
        }
        return;                                      // solo / rec / select have no job here
    }
    const auto widgets = panel_->toolWidgets();
    if (strip < 1 || strip > widgets.size())
        return;
    DjtoolzToolWidget* w = widgets[strip - 1];
    ownChange_ = true;
    switch (control) {
        case Control::Mute:   w->toggleOnOff(); break;
        case Control::Solo:   w->pressGreenButton(); break;
        case Control::Rec:    w->stepSample(recIsForward() ? +1 : -1); break;
        case Control::Select: w->stepSample(recIsForward() ? -1 : +1); break;
        default: break;
    }
    ownChange_ = false;
}

void DjtoolzController::fxKnob(DjtoolzToolWidget* w, int strip, int control, const char* kind, int value)
{
    int sw = 0, hw = 0;
    if (!w->fxValue(control, &sw))
        return;
    if (!pickup(QStringLiteral("%1:%2:%3").arg(int(mode_)).arg(strip).arg(kind), value, sw, &hw))
        return;
    ownChange_ = true;
    w->setFxValue(control, hw);
    ownChange_ = false;
}

void DjtoolzController::onKnob(int strip, int value)
{
    if (mode_ == DrumMachine) {
        DjtoolzToolWidget* w = drumTarget();
        if (!w || strip < 1 || strip > kDrumVoices)
            return;
        DrumEditor* ed = w->drumEditor();
        int hw = 0;
        if (!pickup(QStringLiteral("%1:%2:k").arg(int(mode_)).arg(strip), value, valueFromOffsetMs(ed->voiceOffsetMs(strip - 1)), &hw))
            return;
        ownChange_ = true;
        ed->setVoiceOffsetMs(strip - 1, offsetMsFromValue(hw));
        ownChange_ = false;
        return;
    }
    const auto widgets = panel_->toolWidgets();
    if (strip < 1 || strip > widgets.size())
        return;
    DjtoolzToolWidget* w = widgets[strip - 1];
    if (w->tool()->isEffect()) {                     // an FX tool: the knob is the plugin's first control (the Xone's Freq)
        fxKnob(w, strip, 0, "k", value);
        return;
    }
    double ms = 0;
    if (!w->currentOffsetMs(&ms))
        return;
    int hw = 0;
    if (!pickup(QStringLiteral("%1:%2:k").arg(int(mode_)).arg(strip), value, valueFromOffsetMs(ms), &hw))
        return;
    ownChange_ = true;
    w->setOffsetMs(offsetMsFromValue(hw));
    ownChange_ = false;
}

void DjtoolzController::onFader(int strip, int value)
{
    if (mode_ == DrumMachine) {
        DjtoolzToolWidget* w = drumTarget();
        if (!w || strip < 1 || strip > kDrumVoices)
            return;
        DrumEditor* ed = w->drumEditor();
        int hw = 0;
        if (!pickup(QStringLiteral("%1:%2:f").arg(int(mode_)).arg(strip), value, ed->voiceVolume(strip - 1), &hw))
            return;
        ownChange_ = true;
        ed->setVoiceVolume(strip - 1, volumeFromValue(hw));
        ownChange_ = false;
        return;
    }
    const auto widgets = panel_->toolWidgets();
    if (strip < 1 || strip > widgets.size())
        return;
    DjtoolzToolWidget* w = widgets[strip - 1];
    if (w->tool()->isEffect()) {                     // the fader is its second control (the Xone's Wild)
        fxKnob(w, strip, 1, "f", value);
        return;
    }
    int hw = 0;
    if (!pickup(QStringLiteral("%1:%2:f").arg(int(mode_)).arg(strip), value, w->currentVolume(), &hw))
        return;
    ownChange_ = true;
    w->setVolume(volumeFromValue(hw));
    ownChange_ = false;
}

void DjtoolzController::onTrack(int dir)
{
    if (mode_ != DrumMachine)
        return;
    DjtoolzToolWidget* w = drumTarget();
    if (!w)
        return;
    ownChange_ = true;
    const bool changed = w->drumEditor()->stepKit(dir);
    ownChange_ = false;
    if (changed)
        resetPickups();                              // the kit has other volumes and offsets
}

} // namespace vdj
