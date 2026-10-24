#ifndef USB_CONFIG_DIALOG_H
#define USB_CONFIG_DIALOG_H

#include <QDialog>

class QListWidget;
class QComboBox;
class QPushButton;

namespace vdj {

class ConfigManager;

struct MidiClientInfo {
    int id = -1;
    QString name;
    int inputPorts = 0;
    int outputPorts = 0;
    QString type; // "USB", "ALSA", "Sequencer"...
};

class UsbConfigDialog : public QDialog {
    Q_OBJECT

public:
    explicit UsbConfigDialog(ConfigManager* config, QWidget* parent = nullptr);

    static QList<MidiClientInfo> scanMidiClients();
    static int midiOutputClientId();

private:
    void populateDevices();
    void saveRoutes();

    ConfigManager* config_;
    QListWidget* devices_;
    QComboBox* source_;
    QComboBox* destination_;
    QList<int> sourceIds_;
    QList<int> destIds_;
};

} // namespace vdj

#endif // USB_CONFIG_DIALOG_H