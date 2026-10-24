#pragma once

#include <QDialog>
#include <QList>

class QTabWidget;
class QTableWidget;
class QComboBox;
class QListWidget;
class QPushButton;

namespace vdj {

class ConfigManager;
class InstrumentManager;

class InstrumentConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit InstrumentConfigDialog(InstrumentManager* manager,
                                    ConfigManager* config,
                                    QWidget* parent = nullptr);

private slots:
    void rescan();
    void saveAndWire();
    void applyJoystickProfile();

private:
    void buildMidiTab(QWidget* tab);
    void buildJoystickTab(QWidget* tab);
    void populateMidiTab();
    void populateJoystickTab();

    InstrumentManager* manager_;
    ConfigManager*     config_;

    // MIDI tab
    QTableWidget* deviceTable_;
    QComboBox*    inputCombo_;
    QComboBox*    outputCombo_;
    QList<QString> comboKeys_; // parallel to both combos: device key or ""

    // Joystick tab
    QComboBox*   jsDeviceCombo_;
    QComboBox*   jsProfileCombo_;
    QListWidget* jsAssignList_;
};

} // namespace vdj
