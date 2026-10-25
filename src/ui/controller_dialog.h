#ifndef CONTROLLER_DIALOG_H
#define CONTROLLER_DIALOG_H

#include <QDialog>
#include <QHash>
#include <map>
#include "midi/midi_map_input.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace vdj {

class SamplerMidiControl;

// Edits the sampler part of the controller's .adjmm file: what each strip's buttons, knob and fader send. "Learn" waits for the
// next event of the controller and writes it into the selected cell the way adj_midilearn would (buttons: the pressed value,
// knobs and faders: any value, notes: N <note>). Save keeps every other line of the file (deck ops, comments).
class ControllerDialog : public QDialog {
    Q_OBJECT

public:
    // Rows of the table: the six strip controls, then track back / forward / mode toggle (strip 1 column only)
    static constexpr int kRows = 9;
    static constexpr int kStrips = 8;

    ControllerDialog(SamplerMidiControl* control, QWidget* parent = nullptr);
    ~ControllerDialog() override;

    QTableWidget* table() const { return table_; }
    QPushButton* learnButton() const { return learn_; }
    QPushButton* saveButton() const { return save_; }
    QPushButton* resetButton() const { return reset_; }
    QPushButton* clearButton() const { return clear_; }
    QCheckBox* anyChannelBox() const { return anyChannel_; }
    QCheckBox* fillBox() const { return fill_; }
    QCheckBox* pickupBox() const { return pickup_; }
    QCheckBox* recForwardBox() const { return recForward_; }
    QLineEdit* sourceEdit() const { return source_; }
    QLabel* liveLabel() const { return live_; }
    QLabel* statusLabel() const { return status_; }

    // Name of the op of a table cell ("strip3_fader", "track_fwd"); empty for a cell without one
    static QString opNameFor(int row, int strip);
    void startLearn(int row, int strip);             // as the Learn button does for the selected cell
    bool learning() const { return learnRow_ >= 0; }
    // The cell as shown: "ch 2 / CC 18", "any / CC 18", "any / N 36" or "-"
    QString cellText(int row, int strip) const;
    bool saveFile();                                 // false (and a status message) on failure
    void resetToExample();                           // in the table only; Save writes it
    void reloadFromFile();

signals:
    void saved(const QString& path);

private:
    void fillTable();
    void onLearned(int channel, int controller, bool isNote, int value);
    void learnSelected();
    void stopLearning();
    void setOp(const QString& name, const AdjmmOp& op);

    SamplerMidiControl* control_;
    QLabel* path_;
    QLineEdit* source_;
    QCheckBox* pickup_;
    QCheckBox* recForward_;
    QCheckBox* anyChannel_;
    QCheckBox* fill_;
    QTableWidget* table_;
    QPushButton* learn_;
    QPushButton* clear_;
    QPushButton* reset_;
    QPushButton* save_;
    QLabel* live_;
    QLabel* status_;
    std::map<QString, AdjmmOp> ops_;                 // what the table shows (an op with controller ADJMM_UNSET = removed)
    std::map<QString, AdjmmOp> orig_;                // as read from the file: only changed ops are written back
    QHash<QString, QString> origDirectives_;
    int learnRow_ = -1;
    int learnStrip_ = 0;
};

} // namespace vdj

#endif // CONTROLLER_DIALOG_H
