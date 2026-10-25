#include "sampler_module_widget.h"
#include "beat_chooser.h"
#include "sample_chooser.h"
#include "sampler_icons.h"
#include "sampler_panel.h"

#include <QCheckBox>
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
void SamplerModuleWidget::edit(F f)
{
    if (loading_)
        return;
    ModuleSettings s = module_->settings();
    f(s);
    module_->setSettings(s);
    panel_->moduleEdited(module_->id());
}

SamplerModuleWidget::SamplerModuleWidget(std::shared_ptr<SamplerModule> module, SamplerPanel* panel, QWidget* parent)
    : QFrame(parent)
    , module_(std::move(module))
    , panel_(panel)
{
    setFrameShape(QFrame::StyledPanel);
    const SamplerModule& m = *module_;

    on_ = new QToolButton(this);
    on_->setCheckable(true);
    on_->setIconSize(QSize(24, 24));
    on_->setAutoRaise(true);
    on_->setStyleSheet(QStringLiteral("QToolButton:checked, QToolButton:pressed { background: transparent; border: none; }"));   // the icon shows on/off
    on_->setFocusPolicy(Qt::NoFocus);
    name_ = new QLineEdit(this);
    name_->setPlaceholderText(m.typeName());
    QFont bold = name_->font();
    bold.setBold(true);
    name_->setFont(bold);
    volume_ = new QSlider(Qt::Horizontal, this);
    volume_->setRange(0, SamplerModule::kMaxVolume);
    volume_->setFixedWidth(80);
    volume_->setFocusPolicy(Qt::NoFocus);
    auto* type = new QLabel(m.typeName(), this);
    type->setStyleSheet(QStringLiteral("color: gray;"));
    remove_ = new QToolButton(this);
    remove_->setText(QStringLiteral("✕"));
    remove_->setToolTip(tr("Remove this module"));
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
    offset_ = new QSpinBox(this);
    offset_->setRange(int(-SamplerModule::kMaxOffsetMs), int(SamplerModule::kMaxOffsetMs));
    offset_->setSuffix(QStringLiteral(" ms"));
    offset_->setToolTip(tr("Offset to the beat: negative plays early, positive late"));
    second->addWidget(new QLabel(tr("offset"), this));
    second->addWidget(offset_);

    auto* third = new QHBoxLayout;
    if (m.supportsReverseSync()) {
        reverse_ = new QCheckBox(tr("Reverse sync"), this);
        reverse_->setToolTip(tr("The sample ends on the beat instead of starting on it"));
        third->addWidget(reverse_);
    }
    if (m.supportsOneBar()) {
        oneBar_ = new QToolButton(this);
        oneBar_->setIcon(samplerIcon(SamplerIcon::Green));
        oneBar_->setIconSize(QSize(24, 24));
        oneBar_->setText(tr("1 bar"));
        oneBar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        oneBar_->setToolTip(tr("Play one bar from the next bar line, then stop. Click again to cancel."));
        oneBar_->setFocusPolicy(Qt::NoFocus);
        third->addWidget(oneBar_);
    }
    if (m.type() == SamplerModule::OneShot) {
        fire_ = new QToolButton(this);
        fire_->setIcon(samplerIcon(SamplerIcon::Green));
        fire_->setIconSize(QSize(24, 24));
        fire_->setText(tr("Fire"));
        fire_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        fire_->setToolTip(tr("Play the sample once"));
        fire_->setFocusPolicy(Qt::NoFocus);
        quantize_ = new QCheckBox(tr("Quantize"), this);
        quantize_->setToolTip(tr("Start on the next beat"));
        third->addWidget(fire_);
        third->addWidget(quantize_);
    }
    if (m.type() == SamplerModule::OneShot || m.type() == SamplerModule::AcidLoop) {
        if (m.type() == SamplerModule::OneShot) {
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
    if (m.type() == SamplerModule::MidiMatic) {
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
    l->addLayout(second);
    if (third->count() > 1)
        l->addLayout(third);

    // widgets -> settings
    connect(on_, &QToolButton::toggled, this, [this](bool on) {
        updateOnButton(on);
        edit([on](ModuleSettings& s) { s.enabled = on; });
        if (on)
            panel_->ensureRunning();
    });
    connect(name_, &QLineEdit::textEdited, this, [this](const QString& t) { edit([&](ModuleSettings& s) { s.name = t; }); });
    connect(volume_, &QSlider::valueChanged, this, [this](int v) {
        volume_->setToolTip(tr("Volume %1 (0-127). Turn the mouse wheel over the module to change it.").arg(v));
        edit([v](ModuleSettings& s) { s.volume = v; });
    });
    connect(remove_, &QToolButton::clicked, this, [this]() { emit removeRequested(module_->id()); });
    connect(offset_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { edit([v](ModuleSettings& s) { s.offsetMs = v; }); });
    if (sample_)
        connect(sample_, &SampleChooser::sampleChanged, this, [this](const QString& p) { edit([&](ModuleSettings& s) { s.samplePath = p; }); });
    if (beats_)
        connect(beats_, &BeatChooser::changed, this, [this](quint8 mask) { edit([mask](ModuleSettings& s) { s.beatMask = mask; }); });
    if (reverse_)
        connect(reverse_, &QCheckBox::toggled, this, [this](bool on) { edit([on](ModuleSettings& s) { s.reverseSync = on; }); });
    if (oneBar_)
        connect(oneBar_, &QToolButton::clicked, this, [this]() {
            panel_->ensureRunning();
            panel_->scheduler()->fireOneBar(module_->id());
        });
    if (fire_)
        connect(fire_, &QToolButton::clicked, this, [this]() {
            panel_->ensureRunning();
            panel_->scheduler()->fireOneShot(module_->id());
        });
    if (quantize_)
        connect(quantize_, &QCheckBox::toggled, this, [this](bool on) { edit([on](ModuleSettings& s) { s.quantize = on; }); });
    if (stretch_)
        connect(stretch_, &QCheckBox::toggled, this, [this](bool on) { edit([on](ModuleSettings& s) { s.stretch = on; }); });
    if (bpm_)
        connect(bpm_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) { edit([v](ModuleSettings& s) { s.manualBpm = v; }); });
    if (device_)
        connect(device_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            const QString d = device_->currentData().toString();
            edit([&](ModuleSettings& s) { s.midiDevice = d; });
        });
    if (channel_)
        connect(channel_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { edit([v](ModuleSettings& s) { s.midiChannel = v; }); });
    if (note_)
        connect(note_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { edit([v](ModuleSettings& s) { s.midiNote = v; }); });

    refreshMidiDevices();
    reloadFromModule();

    // the wheel anywhere over the module is the volume, also over its spin boxes and drop-downs (they have their arrows)
    for (QWidget* child : findChildren<QWidget*>())
        child->installEventFilter(this);
}

void SamplerModuleWidget::setVolume(int volume)
{
    volume_->setValue(std::clamp(volume, 0, SamplerModule::kMaxVolume));     // the slider's signal updates the module
}

void SamplerModuleWidget::wheelEvent(QWheelEvent* event)
{
    const int notches = event->angleDelta().y() / 120;
    if (notches == 0) {
        event->ignore();
        return;
    }
    const int step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;       // shift: coarse
    setVolume(volume_->value() + notches * step);
    event->accept();
}

bool SamplerModuleWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Wheel) {
        wheelEvent(static_cast<QWheelEvent*>(event));
        return true;
    }
    return QFrame::eventFilter(watched, event);
}

void SamplerModuleWidget::updateOnButton(bool on)
{
    on_->setIcon(samplerIcon(on ? SamplerIcon::Red : SamplerIcon::Yellow));
    const QString key = on_->text();
    on_->setToolTip((on ? tr("On") : tr("Off")) + (key.isEmpty() ? QString() : tr(" (%1 switches it)").arg(key)));
}

void SamplerModuleWidget::setKeyLabel(const QString& label)
{
    on_->setText(label);
    on_->setToolButtonStyle(label.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    updateOnButton(on_->isChecked());
}

void SamplerModuleWidget::reloadFromModule()
{
    const ModuleSettings s = module_->settings();
    loading_ = true;
    on_->setChecked(s.enabled);
    updateOnButton(s.enabled);
    volume_->setValue(s.volume);
    volume_->setToolTip(tr("Volume %1 (0-127). Turn the mouse wheel over the module to change it.").arg(s.volume));
    name_->setText(s.name);
    offset_->setValue(int(s.offsetMs));
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

QString SamplerModuleWidget::displayName() const
{
    const QString n = module_->settings().name.trimmed();
    return n.isEmpty() ? module_->typeName() : n;
}

bool SamplerModuleWidget::setSample(const QString& path)
{
    if (!sample_)
        return false;
    sample_->setSample(path);
    edit([&](ModuleSettings& s) { s.samplePath = path; });
    return true;
}

void SamplerModuleWidget::refreshMidiDevices()
{
    if (!device_)
        return;
    const QString current = module_->settings().midiDevice;
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

void SamplerModuleWidget::refresh(double nowBeat)
{
    if (!oneBar_)
        return;
    ++blink_;
    switch (module_->oneBarState(nowBeat)) {
    case OneBarState::Idle: oneBar_->setIcon(samplerIcon(SamplerIcon::Green)); break;
    case OneBarState::Armed: oneBar_->setIcon(samplerIcon((blink_ / 3) % 2 ? SamplerIcon::Grey : SamplerIcon::Green)); break;   // blinks
    case OneBarState::Playing: oneBar_->setIcon(samplerIcon(SamplerIcon::Red)); break;
    }
}

} // namespace vdj
