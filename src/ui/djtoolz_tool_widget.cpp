#include "djtoolz_tool_widget.h"
#include "quantize_button.h"
#include <QPushButton>
#include "beat_chooser.h"
#include "drum_editor.h"
#include "sample_chooser.h"
#include "djtoolz_icons.h"
#include "djtoolz_panel.h"
#include "swing_slider.h"
#include "wheel_notches.h"

#include "audio/ladspa_host.h"

#include <QCheckBox>
#include <QGridLayout>
#include <algorithm>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>
#include <QWheelEvent>
#include <QToolButton>
#include <QVBoxLayout>

namespace vdj {

template <typename F>
void DjtoolzToolWidget::edit(F f)
{
    if (loading_)
        return;
    ToolSettings s = tool_->settings();
    f(s);
    tool_->setSettings(s);
    panel_->toolEdited(tool_->id());
}

DjtoolzToolWidget::DjtoolzToolWidget(std::shared_ptr<DjtoolzTool> tool, DjtoolzPanel* panel, QWidget* parent)
    : QFrame(parent)
    , tool_(std::move(tool))
    , panel_(panel)
{
    setFrameShape(QFrame::StyledPanel);
    const DjtoolzTool& m = *tool_;

    on_ = new QToolButton(this);
    on_->setCheckable(true);
    on_->setIconSize(QSize(24, 24));
    on_->setAutoRaise(true);
    on_->setStyleSheet(QStringLiteral("QToolButton:checked, QToolButton:pressed { background: transparent; border: none; }"));   // the icon shows on/off
    on_->setFocusPolicy(Qt::NoFocus);
    name_ = new QLineEdit(this);
    name_->setPlaceholderText(m.typeName());

    volume_ = new QSlider(Qt::Horizontal, this);
    volume_->setRange(0, DjtoolzTool::kMaxVolume);
    volume_->setFixedWidth(80);
    volume_->setFocusPolicy(Qt::NoFocus);
    auto* type = new QLabel(m.typeName(), this);
    QFont bold = type->font();
    bold.setBold(true);
    type->setFont(bold);                             // the tool type, next to the remove button
    type->setStyleSheet(QStringLiteral("color: gray;"));
    typeLabel_ = type;
    remove_ = new QToolButton(this);
    remove_->setText(QStringLiteral("✕"));
    remove_->setToolTip(tr("Remove this tool"));
    remove_->setFocusPolicy(Qt::NoFocus);

    auto* top = new QHBoxLayout;
    top->addWidget(on_);
    top->addWidget(name_, 1);
    top->addWidget(volume_);
    top->addWidget(type);
    top->addWidget(remove_);

    auto* second = new QHBoxLayout;
    if (m.usesSample()) {
        sample_ = new SampleChooser(this);
        sample_->setPreview([panel](const QString& p) { panel->previewSample(p); });
        second->addWidget(sample_, 1);
    }
    if (m.hasBeatChooser()) {
        beats_ = new BeatChooser(this);
        second->addWidget(beats_);
    }
    if (m.perVoiceSamples()) {
        drum_ = new DrumEditor(tool_, panel_, this);       // the DR-6S: sample, offset and pattern are per voice, in its editor
    } else if (m.isEffect()) {
        fxPlugin_ = new QComboBox(this);
        fxPlugin_->setToolTip(tr("Any LADSPA effect with 1-2 audio inputs and outputs: filters, reverbs, delays ..."));
        fxPlugin_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        fxPlugin_->setMinimumContentsLength(24);
        fxPlugin_->addItem(tr("(none)"), QString());
        for (const LadspaPluginInfo& info : LadspaHost::plugins())
            fxPlugin_->addItem(info.name, info.key);
        second->addWidget(new QLabel(tr("effect"), this));
        second->addWidget(fxPlugin_, 1);
        fxKnobBox_ = new QComboBox(this);
        fxFaderBox_ = new QComboBox(this);
        fxKnobBox_->setToolTip(tr("The control the MIDI controller's knob drives (Controller: Tools mode)"));
        fxFaderBox_->setToolTip(tr("The control the MIDI controller's fader drives (Controller: Tools mode)"));
        second->addWidget(new QLabel(tr("knob"), this));
        second->addWidget(fxKnobBox_, 1);
        second->addWidget(new QLabel(tr("fader"), this));
        second->addWidget(fxFaderBox_, 1);
        fxHost_ = new QWidget(this);
        fxGrid_ = new QGridLayout(fxHost_);
        fxGrid_->setContentsMargins(0, 0, 0, 0);
        volume_->hide();                             // an effect is on or off; the knobs are the plugin's
    } else {
        offset_ = new QSpinBox(this);
        offset_->setRange(int(-DjtoolzTool::kMaxOffsetMs), int(DjtoolzTool::kMaxOffsetMs));
        offset_->setSuffix(QStringLiteral(" ms"));
        offset_->setToolTip(tr("Offset to the beat: negative plays early, positive late"));
        second->addWidget(new QLabel(tr("offset"), this));
        second->addWidget(offset_);
    }

    auto* third = new QHBoxLayout;
    if (m.supportsReverseSync()) {
        reverse_ = new QCheckBox(tr("Reverse sync"), this);
        reverse_->setToolTip(tr("The sample ends on the beat instead of starting on it"));
        third->addWidget(reverse_);
    }
    if (m.supportsOneBar()) {
        oneBar_ = new QToolButton(this);
        oneBar_->setIcon(djtoolzIcon(DjtoolzIcon::Green));
        oneBar_->setIconSize(QSize(24, 24));
        oneBar_->setAutoRaise(true);                 // just the round icon, the tooltip says what it does
        oneBar_->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; margin: 0px; border: none; background: transparent; }"));
        oneBar_->setToolTip(tr("1 bar: play one bar from the next bar line, then stop. Click again to cancel."));
        oneBar_->setFocusPolicy(Qt::NoFocus);
        top->insertWidget(1, oneBar_);               // right of the on/off button, the same place in every tool
    }
    if (m.type() == DjtoolzTool::OneShot) {
        fire_ = new QToolButton(this);
        fire_->setIcon(djtoolzIcon(DjtoolzIcon::Green));
        fire_->setIconSize(QSize(24, 24));
        fire_->setAutoRaise(true);
        fire_->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; margin: 0px; border: none; background: transparent; }"));
        fire_->setToolTip(tr("Fire: play the sample once"));
        fire_->setFocusPolicy(Qt::NoFocus);
        quantize_ = makeQuantizeButton(this);
        quantize_->setToolTip(tr("Quantize: start on the next beat"));
        top->insertWidget(1, fire_);                 // the one shot's green button sits where the other tools' 1 bar button is
        third->addWidget(quantize_);
    }
    if (m.type() == DjtoolzTool::OneShot || m.type() == DjtoolzTool::AcidLoop) {
        if (m.type() == DjtoolzTool::OneShot) {
            stretch_ = new QCheckBox(tr("Stretch"), this);
            stretch_->setToolTip(tr("Time-stretch to the sequencer's BPM when the sample's BPM is known"));
            third->addWidget(stretch_);
        }
        bpm_ = new QDoubleSpinBox(this);
        bpm_->setRange(0.0, 300.0);
        bpm_->setDecimals(1);
        bpm_->setSpecialValueText(tr("auto"));
        bpm_->setSuffix(QStringLiteral(" BPM"));
        bpm_->setToolTip(tr("The sample's BPM when the file does not say (auto = from the file)"));
        third->addWidget(bpm_);
    }
    if (m.supportsSwing() && !m.perVoiceSamples()) {
        swing_ = new SwingSlider(this);
        third->addWidget(swing_);
    }
    if (m.type() == DjtoolzTool::MidiMatic) {
        device_ = new QComboBox(this);
        device_->setMinimumWidth(160);
        device_->setToolTip(tr("ALSA MIDI port that receives the notes"));
        channel_ = new QSpinBox(this);
        channel_->setRange(1, 16);
        channel_->setPrefix(tr("ch "));
        note_ = new QSpinBox(this);
        note_->setRange(0, 127);
        note_->setPrefix(tr("note "));
        third->addWidget(device_, 1);
        third->addWidget(channel_);
        third->addWidget(note_);
    }
    third->addStretch(1);

    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(6, 4, 6, 4);
    l->setSpacing(3);
    l->addLayout(top);
    if (drum_)
        l->addWidget(drum_);
    else
        l->addLayout(second);
    if (third->count() > 1)
        l->addLayout(third);
    if (fxHost_)
        l->addWidget(fxHost_);

    // widgets -> settings
    connect(on_, &QToolButton::toggled, this, [this](bool on) {
        updateOnButton(on);
        edit([on](ToolSettings& s) { s.enabled = on; });
        if (on)
            panel_->ensureRunning();
    });
    connect(name_, &QLineEdit::textEdited, this, [this](const QString& t) { edit([&](ToolSettings& s) { s.name = t; }); });
    connect(volume_, &QSlider::valueChanged, this, [this](int v) {
        volume_->setToolTip(tr("Volume %1 (0-127). Turn the mouse wheel over the tool to change it.").arg(v));
        edit([v](ToolSettings& s) { s.volume = v; });
    });
    connect(remove_, &QToolButton::clicked, this, [this]() { emit removeRequested(tool_->id()); });
    if (fxPlugin_)
        connect(fxPlugin_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            if (loading_)
                return;
            static_cast<FxTool*>(tool_.get())->setPlugin(fxPlugin_->currentData().toString());
            buildFxKnobs();
            panel_->toolEdited(tool_->id());
        });
    if (fxKnobBox_) {
        connect(fxKnobBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            if (loading_) return;
            static_cast<FxTool*>(tool_.get())->setKnobControl(fxKnobBox_->currentData().toString());
            panel_->toolEdited(tool_->id());
        });
        connect(fxFaderBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            if (loading_) return;
            static_cast<FxTool*>(tool_.get())->setFaderControl(fxFaderBox_->currentData().toString());
            panel_->toolEdited(tool_->id());
        });
    }
    if (offset_)
        connect(offset_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { edit([v](ToolSettings& s) { s.offsetMs = v; }); });
    if (swing_)
        connect(swing_, &SwingSlider::valueChanged, this, [this](int v) { edit([v](ToolSettings& s) { s.swing = v; }); });
    if (sample_)
        connect(sample_, &SampleChooser::sampleChanged, this, [this](const QString& p) { edit([&](ToolSettings& s) { s.samplePath = p; }); });
    if (beats_)
        connect(beats_, &BeatChooser::changed, this, [this](quint8 mask) { edit([mask](ToolSettings& s) { s.beatMask = mask; }); });
    if (reverse_)
        connect(reverse_, &QCheckBox::toggled, this, [this](bool on) { edit([on](ToolSettings& s) { s.reverseSync = on; }); });
    if (oneBar_)
        connect(oneBar_, &QToolButton::clicked, this, [this]() {
            panel_->ensureRunning();
            panel_->scheduler()->fireOneBar(tool_->id());
        });
    if (fire_)
        connect(fire_, &QToolButton::clicked, this, [this]() {
            panel_->ensureRunning();
            panel_->scheduler()->fireOneShot(tool_->id());
        });
    if (quantize_)
        connect(quantize_, &QPushButton::toggled, this, [this](bool on) { edit([on](ToolSettings& s) { s.quantize = on; }); });
    if (stretch_)
        connect(stretch_, &QCheckBox::toggled, this, [this](bool on) { edit([on](ToolSettings& s) { s.stretch = on; }); });
    if (bpm_)
        connect(bpm_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) { edit([v](ToolSettings& s) { s.manualBpm = v; }); });
    if (device_)
        connect(device_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            const QString d = device_->currentData().toString();
            edit([&](ToolSettings& s) { s.midiDevice = d; });
        });
    if (channel_)
        connect(channel_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { edit([v](ToolSettings& s) { s.midiChannel = v; }); });
    if (note_)
        connect(note_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { edit([v](ToolSettings& s) { s.midiNote = v; }); });

    refreshMidiDevices();
    reloadFromTool();

    // the wheel anywhere over the tool is the volume, also over its spin boxes and drop-downs (they have their arrows)
    for (QWidget* child : findChildren<QWidget*>())
        child->installEventFilter(this);
}

void DjtoolzToolWidget::buildFxKnobs()
{
    fxSliders_.clear();
    fxNames_.clear();
    while (QLayoutItem* item = fxGrid_->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    auto* fx = static_cast<FxTool*>(tool_.get());
    const FxTool::Spec spec = fx->spec();
    LadspaPluginInfo info;
    const bool known = !spec.plugin.isEmpty() && LadspaHost::find(spec.plugin, &info);
    // the two controls the MIDI strip drives: the saved choice, else the first and second
    const bool wasLoading = loading_;
    loading_ = true;
    for (QComboBox* box : { fxKnobBox_, fxFaderBox_ }) {
        box->clear();
        if (known)
            for (const LadspaControl& c : info.controls)
                box->addItem(c.name, c.name);
        box->setEnabled(known && info.controls.size() > 1);
    }
    if (known) {
        const auto pick = [&](QComboBox* box, const QString& saved, size_t dflt) {
            int i = box->findData(saved);
            if (i < 0)
                i = int(std::min(dflt, info.controls.size() - 1));
            box->setCurrentIndex(i);
        };
        if (!info.controls.empty()) {
            pick(fxKnobBox_, spec.knob, 0);
            pick(fxFaderBox_, spec.fader, 1);
        }
    }
    loading_ = wasLoading;
    if (!known)
        return;
    int row = 0, col = 0;
    for (const LadspaControl& c : info.controls) {
        const auto it = spec.values.find(c.name);
        const float value = it == spec.values.end() ? c.def : it.value();
        auto* label = new QLabel(c.name, fxHost_);
        auto* slider = new QSlider(Qt::Horizontal, fxHost_);
        slider->setRange(0, c.toggled ? 1 : 1000);
        slider->setValue(c.toggled ? int(value > 0.5f) : int(std::lround(ladspaControlToUnit(c, value) * 1000.0)));
        slider->setFocusPolicy(Qt::NoFocus);
        slider->setMinimumWidth(90);
        slider->setToolTip(QStringLiteral("%1: %2 .. %3").arg(c.name).arg(double(c.lo)).arg(double(c.hi)));
        auto* valueLabel = new QLabel(fxHost_);
        valueLabel->setMinimumWidth(52);
        const auto show = [valueLabel](float v) { valueLabel->setText(QString::number(double(v), 'g', 4)); };
        show(value);
        connect(slider, &QSlider::valueChanged, this, [this, c, show, fx](int pos) {
            const float v = c.toggled ? float(pos) : ladspaControlFromUnit(c, pos / 1000.0);
            show(v);
            if (loading_)
                return;
            fx->setValue(c.name, v);
            panel_->toolEdited(tool_->id());
        });
        fxGrid_->addWidget(label, row, col * 3);
        fxGrid_->addWidget(slider, row, col * 3 + 1);
        fxGrid_->addWidget(valueLabel, row, col * 3 + 2);
        fxSliders_.append(slider);
        fxNames_.append(c.name);
        if (++col == 2) {
            col = 0;
            ++row;
        }
    }
}

void DjtoolzToolWidget::setVolume(int volume)
{
    volume_->setValue(std::clamp(volume, 0, DjtoolzTool::kMaxVolume));     // the slider's signal updates the tool
}

int DjtoolzToolWidget::currentVolume() const
{
    return volume_->value();
}

bool DjtoolzToolWidget::setOffsetMs(double ms)
{
    if (!offset_)
        return false;
    offset_->setValue(int(std::lround(ms)));         // clamped by the spin box; its signal updates the tool
    return true;
}

bool DjtoolzToolWidget::currentOffsetMs(double* ms) const
{
    if (!offset_)
        return false;
    *ms = offset_->value();
    return true;
}

int DjtoolzToolWidget::fxSliderForRole(int role) const
{
    if (!fxKnobBox_ || fxSliders_.isEmpty() || role < 0 || role > 1)
        return -1;
    const QComboBox* box = role == 0 ? fxKnobBox_ : fxFaderBox_;
    const int i = fxNames_.indexOf(box->currentData().toString());
    return i >= 0 ? i : (role < fxSliders_.size() ? role : -1);
}

bool DjtoolzToolWidget::fxValue(int role, int* value) const
{
    const int n = fxSliderForRole(role);
    if (n < 0)
        return false;
    const QSlider* sl = fxSliders_[n];
    *value = int(std::lround(double(sl->value() - sl->minimum()) * 127.0 / double(sl->maximum() - sl->minimum())));
    return true;
}

bool DjtoolzToolWidget::setFxValue(int role, int value)
{
    const int n = fxSliderForRole(role);
    if (n < 0)
        return false;
    QSlider* sl = fxSliders_[n];
    sl->setValue(sl->minimum() + int(std::lround(double(std::clamp(value, 0, 127)) * (sl->maximum() - sl->minimum()) / 127.0)));
    return true;
}

bool DjtoolzToolWidget::pressGreenButton()
{
    if (oneBar_) { oneBar_->click(); return true; }
    if (fire_) { fire_->click(); return true; }
    return false;
}

bool DjtoolzToolWidget::stepSample(int dir)
{
    return sample_ && sample_->stepSample(dir);
}

void DjtoolzToolWidget::toggleOnOff()
{
    on_->click();
}

void DjtoolzToolWidget::wheelEvent(QWheelEvent* event)
{
    event->accept();                                 // a wheel over a tool never scrolls the list, even for a part of a notch
    if (tool_->isEffect())
        return;                                      // no volume to change
    const int notches = wheel_.add(event);
    if (notches == 0)
        return;
    const int step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;       // shift: coarse
    setVolume(volume_->value() + notches * step);
}

void DjtoolzToolWidget::mousePressEvent(QMouseEvent* event)
{
    emit touched(tool_->id());
    QFrame::mousePressEvent(event);
}

bool DjtoolzToolWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress)
        emit touched(tool_->id());
    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        // the DR-6S: over its editor (but not the swing slider) the wheel is the volume of the selected voice, elsewhere the tool's
        auto* w = qobject_cast<QWidget*>(watched);
        if (drum_ && w && (w == drum_ || drum_->isAncestorOf(w)) && !SwingSlider::isInside(watched))
            drum_->wheelVolume(we);
        else
            wheelEvent(we);
        return true;
    }
    return QFrame::eventFilter(watched, event);
}

void DjtoolzToolWidget::updateOnButton(bool on)
{
    on_->setIcon(djtoolzIcon(on ? DjtoolzIcon::Red : DjtoolzIcon::Yellow));
    const QString key = keyLabel_;
    on_->setToolTip((on ? tr("On") : tr("Off")) + (key.isEmpty() ? QString() : tr(" (%1 switches it)").arg(key)));
}

void DjtoolzToolWidget::setKeyLabel(const QString& label)
{
    keyLabel_ = label;                               // only in the tooltip: the button shows just the round icon
    updateOnButton(on_->isChecked());
}

void DjtoolzToolWidget::reloadFromTool()
{
    const ToolSettings s = tool_->settings();
    loading_ = true;
    on_->setChecked(s.enabled);
    updateOnButton(s.enabled);
    volume_->setValue(s.volume);
    volume_->setToolTip(tr("Volume %1 (0-127). Turn the mouse wheel over the tool to change it.").arg(s.volume));
    name_->setText(s.name);
    if (offset_) offset_->setValue(int(s.offsetMs));
    if (fxPlugin_) {
        const QString key = static_cast<FxTool*>(tool_.get())->spec().plugin;
        int i = fxPlugin_->findData(key);
        if (i < 0) {
            fxPlugin_->addItem(tr("%1 (not installed)").arg(key), key);      // the preset names a plugin this PC lacks: keep it
            i = fxPlugin_->count() - 1;
        }
        fxPlugin_->setCurrentIndex(i);
        buildFxKnobs();
    }
    if (swing_) swing_->setValue(s.swing);
    if (drum_) drum_->reload();
    if (sample_) {
        sample_->setSample(s.samplePath);
        sample_->setFolder(panel_->sampleFolder());
    }
    if (beats_) beats_->setMask(s.beatMask);
    if (reverse_) reverse_->setChecked(s.reverseSync);
    if (quantize_) quantize_->setChecked(s.quantize);
    if (stretch_) stretch_->setChecked(s.stretch);
    if (bpm_) bpm_->setValue(s.manualBpm);
    if (channel_) channel_->setValue(s.midiChannel);
    if (note_) note_->setValue(s.midiNote);
    if (device_) {
        int i = device_->findData(s.midiDevice);
        if (i < 0 && !s.midiDevice.isEmpty()) {
            device_->addItem(tr("%1 (not found)").arg(s.midiDevice), s.midiDevice);   // keep a device that is unplugged right now
            i = device_->count() - 1;
        }
        device_->setCurrentIndex(i);
    }
    loading_ = false;
}

QString DjtoolzToolWidget::displayName() const
{
    const QString n = tool_->settings().name.trimmed();
    return n.isEmpty() ? tool_->typeName() : n;
}

bool DjtoolzToolWidget::setSample(const QString& path)
{
    if (!sample_)
        return false;
    sample_->setSample(path);
    edit([&](ToolSettings& s) { s.samplePath = path; });
    return true;
}

void DjtoolzToolWidget::refreshMidiDevices()
{
    if (!device_)
        return;
    const QString current = tool_->settings().midiDevice;
    const bool was = loading_;
    loading_ = true;
    device_->clear();
    device_->addItem(tr("(connected ports)"), QString());
    for (const MidiPort& p : panel_->midiDestinations())
        device_->addItem(p.name, QStringLiteral("%1:%2").arg(p.clientId).arg(p.portId));
    int i = device_->findData(current);
    if (i < 0 && !current.isEmpty()) {
        device_->addItem(tr("%1 (not found)").arg(current), current);
        i = device_->count() - 1;
    }
    device_->setCurrentIndex(i < 0 ? 0 : i);
    loading_ = was;
}

void DjtoolzToolWidget::refresh(double nowBeat)
{
    if (!oneBar_)
        return;
    ++blink_;
    switch (tool_->oneBarState(nowBeat)) {
    case OneBarState::Idle: oneBar_->setIcon(djtoolzIcon(DjtoolzIcon::Green)); break;
    case OneBarState::Armed: oneBar_->setIcon(djtoolzIcon((blink_ / 3) % 2 ? DjtoolzIcon::Grey : DjtoolzIcon::Green)); break;   // blinks
    case OneBarState::Playing: oneBar_->setIcon(djtoolzIcon(DjtoolzIcon::Red)); break;
    }
}

} // namespace vdj
