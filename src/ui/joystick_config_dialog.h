#ifndef JOYSTICK_CONFIG_DIALOG_H
#define JOYSTICK_CONFIG_DIALOG_H

#include <QDialog>
#include <QList>

class QComboBox;
class QListWidget;
class QLabel;
class QPushButton;
class QTimer;

namespace vdj {

class ConfigManager;

struct JoystickInfo {
    QString device;       // /dev/input/js0
    QString name;
    int axes = 0;
    int buttons = 0;
};

class JoystickConfigDialog : public QDialog {
    Q_OBJECT

public:
    explicit JoystickConfigDialog(ConfigManager* config, QWidget* parent = nullptr);
    ~JoystickConfigDialog() override;

    static QList<JoystickInfo> scanJoysticks();

private slots:
    void startLearning();

private:
    void populate();
    void stopLearning();
    void refreshMappings();

    ConfigManager* config_;
    QComboBox* device_;
    QComboBox* action_;
    QListWidget* mappingList_;
    QLabel* learnStatus_;
    QPushButton* learnButton_;
    QTimer* pollTimer_;
    QString learnedControl_;
    int fd_ = -1;
    int deviceId_ = -1;
};

} // namespace vdj

#endif // JOYSTICK_CONFIG_DIALOG_H