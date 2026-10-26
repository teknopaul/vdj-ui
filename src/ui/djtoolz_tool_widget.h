#ifndef DJTOOLZ_MODULE_WIDGET_H
#define DJTOOLZ_MODULE_WIDGET_H

#include <QFrame>
#include <QList>
#include <QStringList>
#include <memory>
#include "audio/djtoolz_tool.h"
#include "wheel_notches.h"

class QCheckBox;
class QGridLayout;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QLineEdit;
class QSlider;
class QSpinBox;
class QToolButton;

namespace vdj {

class BeatChooser;
class DrumEditor;
class SwingSlider;
class SampleChooser;
class DjtoolzPanel;

// The controls of one DJ Tools tool: on/off switch, name, remove button, and what its type needs (sample chooser, beat chooser,
// offset, reverse sync, one bar button, one shot fire button, MIDI device / channel / note ...). Every edit goes straight into the
// tool's settings.
class DjtoolzToolWidget : public QFrame {
    Q_OBJECT

public:
    DjtoolzToolWidget(std::shared_ptr<DjtoolzTool> tool, DjtoolzPanel* panel, QWidget* parent = nullptr);

    const std::shared_ptr<DjtoolzTool>& tool() const { return tool_; }
    void refresh(double nowBeat);                    // the one bar / blink state, called ~10 times a second
    void reloadFromTool();                         // after the settings were changed from outside (a preset)
    void refreshMidiDevices();
    QString displayName() const;                     // the tool's name, else its type
    bool setSample(const QString& path);             // as if chosen with the sample chooser; false for a tool without a sample

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
    QPushButton* quantizeButton() const { return quantize_; }
    QCheckBox* stretchBox() const { return stretch_; }
    QDoubleSpinBox* bpmSpin() const { return bpm_; }
    QComboBox* midiDeviceCombo() const { return device_; }
    QSpinBox* midiChannelSpin() const { return channel_; }
    QSpinBox* midiNoteSpin() const { return note_; }

    QSlider* volumeSlider() const { return volume_; }
    QComboBox* fxPluginCombo() const { return fxPlugin_; }            // FX tools: the LADSPA plugin ("(none)" first)
    QList<QSlider*> fxSliders() const { return fxSliders_; }          // FX tools: one per control of the plugin (0..1000)
    DrumEditor* drumEditor() const { return drum_; }
    SwingSlider* swingSlider() const { return swing_; }     // the Dembomatik's (the DR-6S has it in its editor)
    bool wantsFullRow() const { return tool_->perVoiceSamples(); }   // the 16 steps need a whole row of the grid
    void setVolume(int volume);
    // For the MIDI controller (no mouse): each returns false when the tool has no such control.
    int currentVolume() const;
    bool setOffsetMs(double ms);                     // writes the tool's offset (whole ms, clamped)
    bool currentOffsetMs(double* ms) const;
    // FX tools: the control the MIDI strip's knob (role 0) or fader (role 1) drives, as 0..127. Which control that is: the FX tool's
    // "knob" / "fader" choice, else the plugin's first / second.
    bool fxValue(int role, int* value) const;
    bool setFxValue(int role, int value);
    QComboBox* fxKnobCombo() const { return fxKnobBox_; }       // which control the controller's knob drives
    QComboBox* fxFaderCombo() const { return fxFaderBox_; }
    bool pressGreenButton();                         // 1 bar or Fire, whichever the tool has
    bool stepSample(int dir);                        // the sample chooser's arrows: -1 previous, +1 next
    void toggleOnOff();
    // The function key that switches this tool on and off (shown next to the switch); empty for none.
    void setKeyLabel(const QString& label);
    QString keyLabel() const { return keyLabel_; }                      // 0..127, as if set with the wheel

signals:
    void removeRequested(int toolId);
    void touched(int toolId);                      // the human clicked something in this tool

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    template <typename F> void edit(F f);
    void updateOnButton(bool on);
    void buildFxKnobs();                             // one slider per control of the chosen plugin

    std::shared_ptr<DjtoolzTool> tool_;
    DjtoolzPanel* panel_;
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
    QPushButton* quantize_ = nullptr;
    QCheckBox* stretch_ = nullptr;
    QDoubleSpinBox* bpm_ = nullptr;
    QComboBox* device_ = nullptr;
    QComboBox* fxPlugin_ = nullptr;
    QWidget* fxHost_ = nullptr;
    QGridLayout* fxGrid_ = nullptr;
    QList<QSlider*> fxSliders_;
    QStringList fxNames_;                            // control names, parallel to fxSliders_
    QComboBox* fxKnobBox_ = nullptr;
    QComboBox* fxFaderBox_ = nullptr;
    int fxSliderForRole(int role) const;
    QSpinBox* channel_ = nullptr;
    QSpinBox* note_ = nullptr;
    int blink_ = 0;
    WheelNotches wheel_;
};

} // namespace vdj

#endif // DJTOOLZ_MODULE_WIDGET_H
