#include "drum_editor.h"
#include "audio/drum_kits.h"
#include "drum_voice_selector.h"
#include "sample_chooser.h"
#include "sampler_panel.h"
#include "step_chooser.h"
#include "swing_slider.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>

namespace vdj {

ChokeSpinBox::ChokeSpinBox(QWidget* parent)
    : QSpinBox(parent)
{
    setRange(0, 16);
    setSuffix(tr(" /4 beat"));
    setSpecialValueText(tr("off"));                  // 0
    setKeyboardTracking(false);
    setToolTip(tr("Cut the sample off after this many quarter beats (it fades out over 100 ms). off = play to the end."));
}

void ChokeSpinBox::stepBy(int steps)
{
    int v = value();
    for (int i = 0; i < std::abs(steps); ++i)
        v = drumChokeNext(v, steps > 0 ? +1 : -1);
    setValue(v);
}

QString ChokeSpinBox::textFromValue(int value) const
{
    return QString::number(value);
}

int ChokeSpinBox::valueFromText(const QString& text) const
{
    QString t = text;
    t.remove(suffix());
    t = t.trimmed();
    if (t.compare(specialValueText(), Qt::CaseInsensitive) == 0)
        return 0;
    return drumChokeClamp(t.toInt());
}

QValidator::State ChokeSpinBox::validate(QString& input, int&) const
{
    QString t = input;
    t.remove(suffix());
    t = t.trimmed();
    if (t.isEmpty() || t.compare(specialValueText().left(t.size()), Qt::CaseInsensitive) == 0)
        return QValidator::Intermediate;
    bool ok = false;
    t.toInt(&ok);
    return ok ? QValidator::Acceptable : QValidator::Invalid;
}

void ChokeSpinBox::fixup(QString& input) const
{
    input = QString::number(valueFromText(input));
}

DrumEditor::DrumEditor(std::shared_ptr<SamplerModule> module, SamplerPanel* panel, QWidget* parent)
    : QWidget(parent)
    , module_(std::move(module))
    , panel_(panel)
{
    selector_ = new DrumVoiceSelector(this);
    swing_ = new SwingSlider(this);

    sample_ = new SampleChooser(this);
    sample_->setPreview([panel](const QString& p) { panel->previewSample(p); });
    offset_ = new QSpinBox(this);
    offset_->setRange(int(-SamplerModule::kMaxOffsetMs), int(SamplerModule::kMaxOffsetMs));
    offset_->setSuffix(QStringLiteral(" ms"));
    offset_->setToolTip(tr("Offset of this voice to the beat: negative plays early, positive late"));
    volume_ = new QSlider(Qt::Horizontal, this);
    volume_->setRange(0, SamplerModule::kMaxVolume);
    volume_->setFixedWidth(80);
    volume_->setFocusPolicy(Qt::NoFocus);
    volumeLabel_ = new QLabel(this);
    volumeLabel_->setMinimumWidth(28);
    choke_ = new ChokeSpinBox(this);
    steps_ = new StepChooser(this);
    kits_ = new QComboBox(this);
    kits_->setMinimumWidth(120);
    kits_->setToolTip(tr("Drum kits: the six samples, volumes, offsets, chokes, patterns and the swing"));
    saveKit_ = new QPushButton(tr("Save kit"), this);
    deleteKit_ = new QPushButton(tr("Delete"), this);

    auto* top = new QHBoxLayout;
    top->addWidget(selector_, 1);
    top->addWidget(new QLabel(tr("Kit"), this));
    top->addWidget(kits_);
    top->addWidget(saveKit_);
    top->addWidget(deleteKit_);
    top->addWidget(swing_);
    auto* mid = new QHBoxLayout;
    mid->addWidget(sample_, 1);
    mid->addWidget(new QLabel(tr("offset"), this));
    mid->addWidget(offset_);
    mid->addWidget(new QLabel(tr("vol"), this));
    mid->addWidget(volume_);
    mid->addWidget(volumeLabel_);
    mid->addWidget(new QLabel(tr("choke"), this));
    mid->addWidget(choke_);
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(3);
    l->addLayout(top);
    l->addLayout(mid);
    l->addWidget(steps_);

    connect(selector_, &DrumVoiceSelector::selectedChanged, this, [this](int) { loadVoice(); });
    connect(swing_, &SwingSlider::valueChanged, this, [this](int v) {
        if (loading_)
            return;
        ModuleSettings s = module_->settings();
        s.swing = v;
        module_->setSettings(s);
        panel_->moduleEdited(module_->id());
    });
    connect(sample_, &SampleChooser::sampleChanged, this, [this](const QString& p) { edit([&](DrumVoice& v) { v.samplePath = p; }); });
    connect(offset_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) { edit([ms](DrumVoice& v) { v.offsetMs = ms; }); });
    connect(volume_, &QSlider::valueChanged, this, [this](int vol) {
        volumeLabel_->setText(QString::number(vol));
        volume_->setToolTip(tr("Volume of this voice %1 (0-127). Turn the mouse wheel over the editor to change it.").arg(vol));
        edit([vol](DrumVoice& v) { v.volume = vol; });
    });
    connect(choke_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int steps) { edit([steps](DrumVoice& v) { v.chokeSteps = steps; }); });
    connect(steps_, &StepChooser::changed, this, [this](int step, quint8 state) { edit([&](DrumVoice& v) { v.steps[step] = state; }); });
    connect(kits_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (i > 0)
            loadKit(kits_->itemText(i));
    });
    connect(saveKit_, &QPushButton::clicked, this, [this]() {
        const QString current = kits_->currentIndex() > 0 ? kits_->currentText() : QString();
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save drum kit"), tr("Name"), QLineEdit::Normal, current, &ok);
        if (ok && !name.trimmed().isEmpty() && !saveKitAs(name))
            QMessageBox::warning(this, tr("Save drum kit"), tr("Cannot save the kit \"%1\"").arg(name));
    });
    connect(deleteKit_, &QPushButton::clicked, this, [this]() {
        if (kits_->currentIndex() <= 0)
            return;
        const QString name = kits_->currentText();
        if (QMessageBox::question(this, tr("Delete drum kit"), tr("Delete the kit \"%1\"?").arg(name)) == QMessageBox::Yes) {
            panel_->drumKits()->remove(name);
            panel_->drumKitsChanged();
        }
    });
    connect(panel_, &SamplerPanel::kitsChanged, this, [this]() { refreshKits(); });
    refreshKits();
    reload();
}

bool DrumEditor::saveKitAs(const QString& name)
{
    if (!panel_->drumKits()->save(name, drumKitFromSettings(module_->settings())))
        return false;
    panel_->drumKitsChanged();                       // every DR-6S refreshes its list (this one too)
    const int i = kits_->findText(DrumKits::sanitise(name));
    if (i > 0)
        kits_->setCurrentIndex(i);
    return true;
}

bool DrumEditor::loadKit(const QString& name)
{
    DrumKit kit;
    QString err;
    if (!panel_->drumKits()->load(name, &kit, &err)) {
        emit panel_->message(tr("Cannot load drum kit: %1").arg(err));
        return false;
    }
    ModuleSettings s = module_->settings();
    applyDrumKit(&s, kit);                           // the voices and the swing only
    module_->setSettings(s);
    reload();
    panel_->moduleEdited(module_->id());
    return true;
}

void DrumEditor::refreshKits()
{
    const QString keep = kits_->currentIndex() > 0 ? kits_->currentText() : QString();
    QSignalBlocker block(kits_);
    kits_->clear();
    kits_->addItem(tr("(kits)"));
    kits_->addItems(panel_->drumKits()->list());
    const int i = kits_->findText(keep);
    kits_->setCurrentIndex(i > 0 ? i : 0);
    deleteKit_->setEnabled(kits_->count() > 1);
}

int DrumEditor::selectedVoice() const
{
    return selector_->selected();
}

void DrumEditor::selectVoice(int voice)
{
    selector_->setSelected(voice);
    loadVoice();
}

template <typename F>
void DrumEditor::edit(F f)
{
    if (loading_)
        return;
    ModuleSettings s = module_->settings();
    f(s.drums[selector_->selected()]);
    module_->setSettings(s);
    panel_->moduleEdited(module_->id());
}

void DrumEditor::reload()
{
    loading_ = true;
    swing_->setValue(module_->settings().swing);
    loading_ = false;
    loadVoice();
}

void DrumEditor::loadVoice()
{
    const ModuleSettings s = module_->settings();
    const DrumVoice& v = s.drums[selector_->selected()];
    loading_ = true;
    sample_->setFolder(panel_->sampleFolder());
    sample_->setSample(v.samplePath);
    offset_->setValue(int(v.offsetMs));
    volume_->setValue(v.volume);
    volumeLabel_->setText(QString::number(v.volume));
    volume_->setToolTip(tr("Volume of this voice %1 (0-127). Turn the mouse wheel over the editor to change it.").arg(v.volume));
    choke_->setValue(v.chokeSteps);
    steps_->setSteps(v.steps);
    loading_ = false;
}

void DrumEditor::wheelVolume(QWheelEvent* event)
{
    event->accept();
    const int notches = wheel_.add(event);
    if (notches == 0)
        return;
    const int step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    volume_->setValue(std::clamp(volume_->value() + notches * step, 0, SamplerModule::kMaxVolume));   // the slider's signal writes the voice
}

} // namespace vdj
