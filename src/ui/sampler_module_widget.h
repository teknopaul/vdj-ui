#ifndef SAMPLER_MODULE_WIDGET_H
#define SAMPLER_MODULE_WIDGET_H

#include <QFrame>
#include <memory>
#include "audio/sampler_module.h"
#include "wheel_notches.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSlider;
class QSpinBox;
class QToolButton;

namespace vdj {

class BeatChooser;
class DrumEditor;
class SwingSlider;
class SampleChooser;
class SamplerPanel;

// The controls of one sampler module: on/off switch, name, remove button, and what its type needs (sample chooser, beat chooser,
// offset, reverse sync, one bar button, one shot fire button, MIDI device / channel / note ...). Every edit goes straight into the
// module's settings.
class SamplerModuleWidget : public QFrame {
    Q_OBJECT

public:
    SamplerModuleWidget(std::shared_ptr<SamplerModule> module, SamplerPanel* panel, QWidget* parent = nullptr);

    const std::shared_ptr<SamplerModule>& module() const { return module_; }
    void refresh(double nowBeat);                    // the one bar / blink state, called ~10 times a second
    void reloadFromModule();                         // after the settings were changed from outside (a preset)
    void refreshMidiDevices();
    QString displayName() const;                     // the module's name, else its type
    bool setSample(const QString& path);             // as if chosen with the sample chooser; false for a module without a sample

    QToolButton* onButton() const { return on_; }
    QLineEdit* nameEdit() const { return name_; }
    QToolButton* removeButton() const { return remove_; }
    QLabel* typeLabel() const { return typeLabel_; }       // "Bombomatic", "DR-6S" ... in bold, next to the remove button
    SampleChooser* sampleChooser() const { return sample_; }
    BeatChooser* beatChooser() const { return beats_; }
    QSpinBox* offsetSpin() const { return offset_; }
    QCheckBox* reverseSyncBox() const { return reverse_; }
    QToolButton* oneBarButton() const { return oneBar_; }
    QToolButton* fireButton() const { return fire_; }
    QCheckBox* quantizeBox() const { return quantize_; }
    QCheckBox* stretchBox() const { return stretch_; }
    QDoubleSpinBox* bpmSpin() const { return bpm_; }
    QComboBox* midiDeviceCombo() const { return device_; }
    QSpinBox* midiChannelSpin() const { return channel_; }
    QSpinBox* midiNoteSpin() const { return note_; }

    QSlider* volumeSlider() const { return volume_; }
    DrumEditor* drumEditor() const { return drum_; }
    SwingSlider* swingSlider() const { return swing_; }     // the Dembomatik's (the DR-6S has it in its editor)
    bool wantsFullRow() const { return module_->perVoiceSamples(); }   // the 16 steps need a whole row of the grid
    void setVolume(int volume);
    // The function key that switches this module on and off (shown next to the switch); empty for none.
    void setKeyLabel(const QString& label);
    QString keyLabel() const { return keyLabel_; }                      // 0..127, as if set with the wheel

signals:
    void removeRequested(int moduleId);

protected:
    void wheelEvent(QWheelEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    template <typename F> void edit(F f);
    void updateOnButton(bool on);

    std::shared_ptr<SamplerModule> module_;
    SamplerPanel* panel_;
    bool loading_ = false;
    QToolButton* on_ = nullptr;
    QLineEdit* name_ = nullptr;
    QToolButton* remove_ = nullptr;
    QLabel* typeLabel_ = nullptr;
    QString keyLabel_;
    QSlider* volume_ = nullptr;
    DrumEditor* drum_ = nullptr;
    SwingSlider* swing_ = nullptr;
    SampleChooser* sample_ = nullptr;
    BeatChooser* beats_ = nullptr;
    QSpinBox* offset_ = nullptr;
    QCheckBox* reverse_ = nullptr;
    QToolButton* oneBar_ = nullptr;
    QToolButton* fire_ = nullptr;
    QCheckBox* quantize_ = nullptr;
    QCheckBox* stretch_ = nullptr;
    QDoubleSpinBox* bpm_ = nullptr;
    QComboBox* device_ = nullptr;
    QSpinBox* channel_ = nullptr;
    QSpinBox* note_ = nullptr;
    int blink_ = 0;
    WheelNotches wheel_;
};

} // namespace vdj

#endif // SAMPLER_MODULE_WIDGET_H
