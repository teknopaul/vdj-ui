#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QTabWidget;
class QTableWidget;
class QComboBox;
class QListWidget;
class QPushButton;
class QLineEdit;

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
    void inputDeviceChanged();
    void browseMap();
    void clearMap();

private:
    void buildMidiTab(QWidget* tab);
    void buildJoystickTab(QWidget* tab);
    void buildPresetsTab(QWidget* tab);
    void refreshPresetFolders();
    void addPresetFolder();
    void removePresetFolder();
    void populateMidiTab();
    void populateJoystickTab();

    InstrumentManager* manager_;
    ConfigManager*     config_;

    // MIDI tab
    QTableWidget* deviceTable_;
    QComboBox*    inputCombo_;
    QComboBox*    djtoolzCombo_;
    QComboBox*    outputCombo_;
    QList<QString> comboKeys_; // parallel to both combos: device key or ""
    QLineEdit*    mapEdit_;
    QPushButton*  mapBrowse_;
    QPushButton*  mapClear_;
    QHash<QString, QString> maps_;   // device key -> map path, saved with the dialog
    QString shownKey_;               // the input device the map row currently belongs to

    // Presets tab: folders of exported drum kits
    QTableWidget* kitFolderTable_ = nullptr;
    QStringList   kitFolders_;

    // Joystick tab
    QComboBox*   jsDeviceCombo_;
    QComboBox*   jsProfileCombo_;
    QListWidget* jsAssignList_;
};

} // namespace vdj
