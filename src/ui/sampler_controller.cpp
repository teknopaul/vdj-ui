#include "sampler_controller.h"

#include "drum_editor.h"
#include "drum_voice_selector.h"
#include "midi/midi_map_input.h"
#include "sampler_module_widget.h"
#include "sampler_panel.h"

namespace vdj {

void wireSamplerControl(MidiMapInput* input, SamplerPanel* panel, SamplerMidiControl* control)
{
    QObject::connect(input, &MidiMapInput::controlEvent, control, &SamplerMidiControl::onControlEvent);
    QObject::connect(input, &MidiMapInput::noteEvent, control, &SamplerMidiControl::onNoteEvent);
    panel->controller()->attach(control);
}

SamplerController::SamplerController(SamplerPanel* panel)
    : QObject(panel)
    , panel_(panel)
{
}

void SamplerController::attach(SamplerMidiControl* c)
{
    if (control_)
        disconnect(control_, nullptr, this, nullptr);
    control_ = c;
    if (!c)
        return;
    connect(c, &SamplerMidiControl::buttonPressed, this, &SamplerController::onButton);
    connect(c, &SamplerMidiControl::knobMoved, this, &SamplerController::onKnob);
    connect(c, &SamplerMidiControl::faderMoved, this, &SamplerController::onFader);
    connect(c, &SamplerMidiControl::trackBack, this, [this]() { onTrack(-1); });
    connect(c, &SamplerMidiControl::trackForward, this, [this]() { onTrack(+1); });
    connect(c, &SamplerMidiControl::modeToggleRequested, this,
            [this]() { setMode(mode_ == Modules ? DrumMachine : Modules); });
}

void SamplerController::setMode(Mode mode)
{
    if (mode == mode_)
        return;
    mode_ = mode;
    resetPickups();
    emit modeChanged(int(mode));
}

void SamplerController::resetPickups()
{
    for (Pickup& p : pickups_)
        p.reset();
}

void SamplerController::softwareChanged(int moduleId)
{
    if (ownChange_)
        return;
    if (mode_ == DrumMachine) {
        if (panel_->drumTarget() && panel_->drumTarget()->module()->id() == moduleId)
            resetPickups();
        return;
    }
    // Only the strips of that module matter, but a change by the mouse is rare: re-arm the module's strip
    const auto widgets = panel_->moduleWidgets();
    for (int i = 0; i < widgets.size(); ++i)
        if (widgets[i]->module()->id() == moduleId) {
            for (const char* k : { "k", "f" })
                if (pickups_.contains(QStringLiteral("%1:%2:%3").arg(int(Modules)).arg(i + 1).arg(k)))
                    pickups_[QStringLiteral("%1:%2:%3").arg(int(Modules)).arg(i + 1).arg(k)].reset();
        }
}

bool SamplerController::pickup(const QString& key, int hardware, int software, int* out)
{
    Pickup& p = pickups_[key];
    p.setEnabledKeepState(!control_ || control_->map().pickup());
    return p.filter(hardware, software, out);
}

SamplerModuleWidget* SamplerController::drumTarget()
{
    SamplerModuleWidget* w = panel_->drumTarget();
    const int id = w ? w->module()->id() : -1;
    if (id != targetId_) {
        targetId_ = id;
        resetPickups();
    }
    return w;
}

void SamplerController::onButton(Control control, int strip)
{
    if (mode_ == DrumMachine) {
        SamplerModuleWidget* w = drumTarget();
        if (w && control == Control::Mute && strip >= 1 && strip <= kDrumVoices) {
            ownChange_ = true;
            w->drumEditor()->toggleVoiceMute(strip - 1);
            ownChange_ = false;
        }
        return;                                      // solo / rec / select have no job here
    }
    const auto widgets = panel_->moduleWidgets();
    if (strip < 1 || strip > widgets.size())
        return;
    SamplerModuleWidget* w = widgets[strip - 1];
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

void SamplerController::onKnob(int strip, int value)
{
    if (mode_ == DrumMachine) {
        SamplerModuleWidget* w = drumTarget();
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
    const auto widgets = panel_->moduleWidgets();
    if (strip < 1 || strip > widgets.size())
        return;
    SamplerModuleWidget* w = widgets[strip - 1];
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

void SamplerController::onFader(int strip, int value)
{
    if (mode_ == DrumMachine) {
        SamplerModuleWidget* w = drumTarget();
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
    const auto widgets = panel_->moduleWidgets();
    if (strip < 1 || strip > widgets.size())
        return;
    SamplerModuleWidget* w = widgets[strip - 1];
    int hw = 0;
    if (!pickup(QStringLiteral("%1:%2:f").arg(int(mode_)).arg(strip), value, w->currentVolume(), &hw))
        return;
    ownChange_ = true;
    w->setVolume(volumeFromValue(hw));
    ownChange_ = false;
}

void SamplerController::onTrack(int dir)
{
    if (mode_ != DrumMachine)
        return;
    SamplerModuleWidget* w = drumTarget();
    if (!w)
        return;
    ownChange_ = true;
    const bool changed = w->drumEditor()->stepKit(dir);
    ownChange_ = false;
    if (changed)
        resetPickups();                              // the kit has other volumes and offsets
}

} // namespace vdj
