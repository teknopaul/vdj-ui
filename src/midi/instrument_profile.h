#pragma once

#include <QString>
#include <QHash>
#include <QList>

namespace vdj {

struct InstrumentProfile {
    QString  id;           // "pioneer-ddj-sx3"
    QString  name;         // "Pioneer DDJ-SX3"
    quint16  vendorId  = 0;
    quint16  productId = 0;
    bool     midiInput   = false;
    bool     midiOutput  = false;
    bool     hasJoystick = false;
    // action -> control, e.g. "play_pause" -> "button_0"
    QHash<QString, QString> joystickDefaults;
    QString  filePath;

    QString usbId() const {
        return QString::asprintf("%04x:%04x", vendorId, productId);
    }
    bool isValid() const { return vendorId != 0; }
};

QList<InstrumentProfile> loadInstrumentProfiles(const QStringList& searchDirs);

} // namespace vdj
