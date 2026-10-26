#include "drum_editor.h"
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QDialog>
#include "audio/drum_kits.h"
#include "drum_voice_selector.h"
#include "sample_chooser.h"
#include "djtoolz_panel.h"
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
#include <cmath>

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

DrumEditor::DrumEditor(std::shared_ptr<DjtoolzTool> tool, DjtoolzPanel* panel, QWidget* parent)
    : QWidget(parent)
    , tool_(std::move(tool))
    , panel_(panel)
{
    selector_ = new DrumVoiceSelector(this);
    swing_ = new SwingSlider(this);

    sample_ = new SampleChooser(this);
    sample_->setPreview([panel](const QString& p) { panel->previewSample(p); });
    offset_ = new QSpinBox(this);
    offset_->setRange(int(-DjtoolzTool::kMaxOffsetMs), int(DjtoolzTool::kMaxOffsetMs));
    offset_->setSuffix(QStringLiteral(" ms"));
    offset_->setToolTip(tr("Offset of this voice to the beat: negative plays early, positive late"));
    volume_ = new QSlider(Qt::Horizontal, this);
    volume_->setRange(0, DjtoolzTool::kMaxVolume);
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
    connect(selector_, &DrumVoiceSelector::muteToggled, this, [this](int voice, bool muted) { setVoiceMuted(voice, muted); });
    connect(swing_, &SwingSlider::valueChanged, this, [this](int v) {
        if (loading_)
            return;
        ToolSettings s = tool_->settings();
        s.swing = v;
        tool_->setSettings(s);
        panel_->toolEdited(tool_->id());
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
    connect(saveKit_, &QPushButton::clicked, this, [this]() { saveKitDialog(); });
    connect(deleteKit_, &QPushButton::clicked, this, [this]() {
        if (kits_->currentIndex() <= 0)
            return;
        const QString name = kits_->currentText();
        if (panel_->drumKits()->isImported(name)) {
            QMessageBox::information(this, tr("Delete drum kit"),
                                     tr("\"%1\" is an imported kit. Remove its folder in Config > Instruments > Presets.").arg(name));
            return;
        }
        if (QMessageBox::question(this, tr("Delete drum kit"), tr("Delete the kit \"%1\"?").arg(name)) == QMessageBox::Yes) {
            panel_->drumKits()->remove(name);
            panel_->drumKitsChanged();
        }
    });
    connect(panel_, &DjtoolzPanel::kitsChanged, this, [this]() { refreshKits(); });
    refreshKits();
    reload();
}

void DrumEditor::setVoiceMuted(int voice, bool muted)
{
    if (voice < 0 || voice >= kDrumVoices)
        return;
    selector_->setMuted(voice, muted);
    ToolSettings s = tool_->settings();
    if (s.drums[voice].muted == muted)
        return;
    s.drums[voice].muted = muted;
    tool_->setSettings(s);
    panel_->toolEdited(tool_->id());
}

void DrumEditor::toggleVoiceMute(int voice)
{
    if (voice >= 0 && voice < kDrumVoices)
        setVoiceMuted(voice, !tool_->settings().drums[voice].muted);
}

int DrumEditor::voiceVolume(int voice) const
{
    return tool_->settings().drums[std::clamp(voice, 0, kDrumVoices - 1)].volume;
}

double DrumEditor::voiceOffsetMs(int voice) const
{
    return tool_->settings().drums[std::clamp(voice, 0, kDrumVoices - 1)].offsetMs;
}

void DrumEditor::setVoiceVolume(int voice, int volume)
{
    if (voice < 0 || voice >= kDrumVoices)
        return;
    if (voice == selector_->selected()) {
        volume_->setValue(std::clamp(volume, 0, DjtoolzTool::kMaxVolume));    // its signal writes the tool
        return;
    }
    ToolSettings s = tool_->settings();
    s.drums[voice].volume = std::clamp(volume, 0, DjtoolzTool::kMaxVolume);
    tool_->setSettings(s);
    panel_->toolEdited(tool_->id());
}

void DrumEditor::setVoiceOffsetMs(int voice, double ms)
{
    if (voice < 0 || voice >= kDrumVoices)
        return;
    const double v = std::clamp(std::round(ms), -DjtoolzTool::kMaxOffsetMs, DjtoolzTool::kMaxOffsetMs);
    if (voice == selector_->selected()) {
        offset_->setValue(int(v));
        return;
    }
    ToolSettings s = tool_->settings();
    s.drums[voice].offsetMs = v;
    tool_->setSettings(s);
    panel_->toolEdited(tool_->id());
}

bool DrumEditor::stepKit(int dir)
{
    const QStringList kits = panel_->drumKits()->list();
    if (kits.isEmpty() || dir == 0)
        return false;
    const int cur = kits_->currentIndex() > 0 ? kits.indexOf(kits_->currentText()) : -1;
    int next;
    if (dir > 0)
        next = cur < 0 ? 0 : std::min(cur + 1, int(kits.size()) - 1);
    else
        next = cur < 0 ? int(kits.size()) - 1 : std::max(cur - 1, 0);
    if (next == cur)
        return false;
    if (!loadKit(kits[next]))
        return false;
    const int i = kits_->findText(kits[next]);
    if (i > 0) {
        QSignalBlocker block(kits_);
        kits_->setCurrentIndex(i);
    }
    return true;
}

// The Save kit popup: the name, then Save (into the config folder) or Export (a folder with copies of the samples, to back up or move).
void DrumEditor::saveKitDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Save drum kit"));
    auto* form = new QVBoxLayout(&dlg);
    form->addWidget(new QLabel(tr("Name"), &dlg));
    auto* name = new QLineEdit(&dlg);
    if (kits_->currentIndex() > 0 && !panel_->drumKits()->isImported(kits_->currentText()))
        name->setText(kits_->currentText());
    form->addWidget(name);
    auto* hint = new QLabel(tr("Export copies the samples the kit uses into an empty folder, with a file that loads the kit back "
                               "on this or another PC (Config > Instruments > Presets). The original samples are not touched."), &dlg);
    hint->setWordWrap(true);
    form->addWidget(hint);
    auto* box = new QDialogButtonBox(&dlg);
    QPushButton* save = box->addButton(tr("Save"), QDialogButtonBox::AcceptRole);
    QPushButton* exp = box->addButton(tr("Export to Folder..."), QDialogButtonBox::ActionRole);
    box->addButton(QDialogButtonBox::Cancel);
    form->addWidget(box);
    bool exporting = false;
    connect(save, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(exp, &QPushButton::clicked, &dlg, [&]() { exporting = true; dlg.accept(); });
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted || name->text().trimmed().isEmpty())
        return;
    const QString n = name->text().trimmed();
    if (!exporting) {
        if (!saveKitAs(n))
            QMessageBox::warning(this, tr("Save drum kit"), tr("Cannot save the kit \"%1\"").arg(n));
        return;
    }
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Export drum kit: choose an empty folder"));
    if (folder.isEmpty())
        return;
    QString err;
    if (exportKitTo(n, folder, &err))
        emit panel_->message(tr("Exported the drum kit \"%1\" to %2").arg(n, folder));
    else
        QMessageBox::warning(this, tr("Export drum kit"), err);
}

bool DrumEditor::exportKitTo(const QString& name, const QString& folder, QString* error)
{
    return DrumKits::exportKit(name, drumKitFromSettings(tool_->settings()), folder, error);
}

bool DrumEditor::saveKitAs(const QString& name)
{
    if (!panel_->drumKits()->save(name, drumKitFromSettings(tool_->settings())))
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
    ToolSettings s = tool_->settings();
    applyDrumKit(&s, kit);                           // the voices and the swing only
    tool_->setSettings(s);
    reload();
    panel_->toolEdited(tool_->id());
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
    ToolSettings s = tool_->settings();
    f(s.drums[selector_->selected()]);
    tool_->setSettings(s);
    panel_->toolEdited(tool_->id());
}

void DrumEditor::reload()
{
    loading_ = true;
    swing_->setValue(tool_->settings().swing);
    loading_ = false;
    const ToolSettings s = tool_->settings();
    for (int v = 0; v < kDrumVoices; ++v)
        selector_->setMuted(v, s.drums[v].muted);
    loadVoice();
}

bool DrumEditor::setVoiceSample(int voice, const QString& path)
{
    if (voice < 0 || voice >= kDrumVoices)
        return false;
    selectVoice(voice);
    sample_->setSample(path);                        // (setSample does not emit: write the voice here)
    edit([&](DrumVoice& v) { v.samplePath = path; });
    return true;
}

void DrumEditor::loadVoice()
{
    const ToolSettings s = tool_->settings();
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
    volume_->setValue(std::clamp(volume_->value() + notches * step, 0, DjtoolzTool::kMaxVolume));   // the slider's signal writes the voice
}

} // namespace vdj
