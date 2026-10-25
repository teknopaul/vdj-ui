#ifndef DRUM_EDITOR_H
#define DRUM_EDITOR_H

#include <QSpinBox>
#include <QWidget>
#include <memory>
#include "audio/sampler_module.h"
#include "wheel_notches.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QWheelEvent;

namespace vdj {

class DrumVoiceSelector;
class SampleChooser;
class SamplerPanel;
class StepChooser;
class SwingSlider;

// The choke length of a DR-6S voice in quarter beats: only 0 (off), 1, 2, 4, 8 and 16. The arrows and the up / down keys walk
// through that list, a typed number snaps to the nearest one.
class ChokeSpinBox : public QSpinBox {
    Q_OBJECT

public:
    explicit ChokeSpinBox(QWidget* parent = nullptr);
    void stepBy(int steps) override;

protected:
    QString textFromValue(int value) const override;
    int valueFromText(const QString& text) const override;
    QValidator::State validate(QString& input, int& pos) const override;
    void fixup(QString& input) const override;
};

// The editor of the DR-6S inside its module widget: the six voice buttons and the swing, the sample / offset / volume / choke of the
// voice that is selected, and its 16 step pattern. Every edit goes straight into the module's settings.drums[voice].
class DrumEditor : public QWidget {
    Q_OBJECT

public:
    DrumEditor(std::shared_ptr<SamplerModule> module, SamplerPanel* panel, QWidget* parent = nullptr);

    void reload();                                   // everything from the module's settings (a preset or kit was loaded)
    int selectedVoice() const;
    void selectVoice(int voice);
    void wheelVolume(QWheelEvent* event);

    // Drum kits: the six voices and the swing, saved apart from the sampler presets (the module's name, on/off and master volume
    // are not part of a kit).
    bool saveKitAs(const QString& name);             // no dialog (the Save button asks for the name)
    bool loadKit(const QString& name);
    void refreshKits();
    QComboBox* kitCombo() const { return kits_; }
    QPushButton* saveKitButton() const { return saveKit_; }
    QPushButton* deleteKitButton() const { return deleteKit_; }            // the mouse wheel over the editor: the selected voice's volume

    DrumVoiceSelector* selector() const { return selector_; }
    SwingSlider* swing() const { return swing_; }
    SampleChooser* sampleChooser() const { return sample_; }
    QSpinBox* offsetSpin() const { return offset_; }
    QSlider* volumeSlider() const { return volume_; }
    ChokeSpinBox* chokeSpin() const { return choke_; }
    StepChooser* stepChooser() const { return steps_; }

private:
    template <typename F> void edit(F f);
    void loadVoice();

    std::shared_ptr<SamplerModule> module_;
    SamplerPanel* panel_;
    bool loading_ = false;
    DrumVoiceSelector* selector_;
    SwingSlider* swing_;
    SampleChooser* sample_;
    QSpinBox* offset_;
    QSlider* volume_;
    QLabel* volumeLabel_;
    ChokeSpinBox* choke_;
    StepChooser* steps_;
    QComboBox* kits_;
    QPushButton* saveKit_;
    QPushButton* deleteKit_;
    WheelNotches wheel_;
};

} // namespace vdj

#endif // DRUM_EDITOR_H
