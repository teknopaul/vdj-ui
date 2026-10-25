#ifndef MIDI_MONITOR_DIALOG_H
#define MIDI_MONITOR_DIALOG_H

#include <QDialog>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QTimer;

namespace vdj {

class MidiMapInput;

// Non-modal window for debugging MIDI control: the map in use, the ALSA port the controller has to be connected to,
// who is connected to it, and every event that arrives with the operation the map turned it into (or "not mapped").
class MidiMonitorDialog : public QDialog {
    Q_OBJECT

public:
    explicit MidiMonitorDialog(MidiMapInput* input, QWidget* parent = nullptr);

    QPlainTextEdit* log() const { return log_; }
    QString infoText() const;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void onEvent(const QString& description, int op);
    void refreshInfo();

    MidiMapInput* input_;
    QLabel* info_;
    QPlainTextEdit* log_;
    QCheckBox* pause_;
    QTimer* infoTimer_;
};

} // namespace vdj

#endif // MIDI_MONITOR_DIALOG_H
