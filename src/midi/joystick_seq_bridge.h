#pragma once

#include <QObject>
#include <QString>

namespace vdj {

class ConfigManager;

// Reads joystick events in a background thread and turns them into the same requests the MIDI map makes (see MidiMapInput), so a joystick and a
// MIDI controller behave identically. The default axis/button map mirrors adj_js.c (see joystick_map.h). Untested on real hardware.
class JoystickSeqBridge : public QObject {
    Q_OBJECT
public:
    explicit JoystickSeqBridge(ConfigManager* config, QObject* parent = nullptr);
    ~JoystickSeqBridge();

    void start();   // opens the device, starts the thread (silent if the device is absent)
    void stop();

signals:
    void toggleRequested();
    void stopRequested();
    void quantizedRestartRequested();      // always quantized, like adj
    void nudgeRequested(int ms);
    void tempoAdjustRequested(double bpmDelta);

private:
    ConfigManager*  config_;
    class Worker;
    Worker*         worker_ = nullptr;
    class QThread*  thread_ = nullptr;
};

} // namespace vdj
