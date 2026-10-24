#pragma once

#include "instrument_profile.h"
#include <QObject>
#include <QList>

namespace vdj {

class ConfigManager;
class AlsaClock;
class MidiRouter;

// Tracks connected USB MIDI instruments, matches them against profiles,
// performs ALSA port subscriptions (aconnect equivalent), and manages
// joystick profile assignment.
//
// The two roles are:
//   MIDI input  - controller device whose port is subscribed to the VDJ clock port
//                 so its events drive the sequencer (same as C adj_mod_xdj700 aconnect)
//   MIDI output - device that receives the VDJ clock/start/stop
//                 (drum machine, etc.)
class InstrumentManager : public QObject {
    Q_OBJECT
public:
    explicit InstrumentManager(ConfigManager* config, QObject* parent = nullptr);

    struct DetectedDevice {
        // Stable key: USB hw-id "2b73:000a" when available, "alsa:<name>" fallback
        QString key;
        QString usbId;       // raw "VVVV:PPPP" or empty
        QString displayName; // sysfs product / profile name / ALSA name
        int     alsaClientId = -1;
        int     alsaPortId   = -1; // first usable port
        QString alsaName;
        int     inputPorts   = 0;
        int     outputPorts  = 0;
        const InstrumentProfile* profile = nullptr;
    };

    // Supply before calling wireDevices() / autowire()
    void setSequencer(AlsaClock* seq) { seq_ = seq; }
    void setRouter(MidiRouter* router)    { router_ = router; }
    // The ALSA port that controller events are sent to (MidiMapInput::clientId/portId)
    void setInputPort(int client, int port) { inClient_ = client; inPort_ = port; }

    void loadProfiles();
    QList<DetectedDevice> detectDevices() const;
    const QList<InstrumentProfile>& profiles() const { return profiles_; }

    // Saved user assignments (stored in "instruments" config section)
    // Key is USB hw-id or "alsa:<name>" for non-USB ALSA clients.
    QString midiInputKey()  const;
    QString midiOutputKey() const;
    void setMidiInputKey(const QString& key);
    void setMidiOutputKey(const QString& key);

    // Create ALSA subscriptions for the configured devices.
    // Safe to call multiple times; duplicate subscriptions are silently ignored by ALSA.
    void wireDevices();

    // Same as wireDevices() — called on boot once seq + router are ready.
    void autowire() { wireDevices(); }

    // Copy a profile's default joystick button map into config for devicePath.
    void applyProfileToJoystick(const QString& devicePath, const QString& profileId);

    static QStringList profileSearchDirs();

signals:
    void wired(const QString& description); // human-readable summary of what was connected

private:
    static QString readUsbIdForCard(int cardNum);
    static QString readUsbProductForCard(int cardNum);

    ConfigManager*           config_;
    AlsaClock*           seq_    = nullptr;
    MidiRouter*              router_ = nullptr;
    int                      inClient_ = -1;
    int                      inPort_   = -1;
    QList<InstrumentProfile> profiles_;
};

} // namespace vdj
