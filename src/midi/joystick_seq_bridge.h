#pragma once

#include <QObject>
#include <QString>

namespace vdj {

class AlsaSequencer;
class ConfigManager;

// Reads joystick events in a background thread and routes them to AlsaSequencer.
// Default axis/button map mirrors adj_js.c (Logitech gamepad).
class JoystickSeqBridge : public QObject {
    Q_OBJECT
public:
    explicit JoystickSeqBridge(ConfigManager* config, QObject* parent = nullptr);
    ~JoystickSeqBridge();

    void setSequencer(AlsaSequencer* seq);
    void start();   // opens device, starts thread (silent if device absent)
    void stop();

signals:
    void deviceConnected(const QString& name);
    void deviceDisconnected();

private:
    ConfigManager*  config_;
    AlsaSequencer*  seq_    = nullptr;
    class Worker;
    Worker*         worker_ = nullptr;
    class QThread*  thread_ = nullptr;
};

} // namespace vdj
