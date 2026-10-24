#include "instrument_manager.h"
#include "config/config_manager.h"
#include "midi_router.h"
#include "alsa_clock.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QCoreApplication>

#include <alsa/asoundlib.h>

namespace vdj {

// ── Profile file loader ───────────────────────────────────────────────────────

QList<InstrumentProfile> loadInstrumentProfiles(const QStringList& searchDirs)
{
    QList<InstrumentProfile> out;
    QSet<QString> seen; // dedup by id, first file wins

    for (const QString& dir : searchDirs) {
        QDir d(dir);
        if (!d.exists()) continue;
        const QStringList files = d.entryList(QStringList() << QStringLiteral("*.device"),
                                              QDir::Files);
        for (const QString& fname : files) {
            QString id = QFileInfo(fname).baseName();
            if (seen.contains(id)) continue;

            QFile f(d.filePath(fname));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

            InstrumentProfile p;
            p.id       = id;
            p.filePath = d.filePath(fname);

            while (!f.atEnd()) {
                QString line = QString::fromUtf8(f.readLine()).trimmed();
                if (line.isEmpty() || line.startsWith('#')) continue;
                int eq = line.indexOf('=');
                if (eq <= 0) continue;
                QString key = line.left(eq).trimmed();
                QString val = line.mid(eq + 1).trimmed();

                if (key == QStringLiteral("name"))
                    p.name = val;
                else if (key == QStringLiteral("vendor"))
                    p.vendorId = static_cast<quint16>(val.toUInt(nullptr, 0));
                else if (key == QStringLiteral("product"))
                    p.productId = static_cast<quint16>(val.toUInt(nullptr, 0));
                else if (key == QStringLiteral("midi_input"))
                    p.midiInput = (val.toLower() == QLatin1String("yes"));
                else if (key == QStringLiteral("midi_output"))
                    p.midiOutput = (val.toLower() == QLatin1String("yes"));
                else if (key == QStringLiteral("joystick"))
                    p.hasJoystick = (val.toLower() == QLatin1String("yes"));
                else if (key.startsWith(QStringLiteral("joystick_")))
                    p.joystickDefaults.insert(key.mid(9), val);
            }
            if (p.isValid()) {
                if (p.name.isEmpty()) p.name = id;
                seen.insert(id);
                out.append(p);
            }
        }
    }
    return out;
}

// ── InstrumentManager ─────────────────────────────────────────────────────────

InstrumentManager::InstrumentManager(ConfigManager* config, QObject* parent)
    : QObject(parent), config_(config)
{
}

QStringList InstrumentManager::profileSearchDirs()
{
    QStringList dirs;
    dirs << QDir::homePath() + QStringLiteral("/.config/vdj-ui/devices");
    dirs << QStringLiteral("/etc/vdj-ui/devices");
    dirs << QStringLiteral("/usr/share/vdj-ui/devices");
    dirs << QCoreApplication::applicationDirPath() + QStringLiteral("/../share/vdj-ui/devices");
    return dirs;
}

void InstrumentManager::loadProfiles()
{
    profiles_ = loadInstrumentProfiles(profileSearchDirs());
}

QString InstrumentManager::readUsbIdForCard(int cardNum)
{
    if (cardNum < 0) return {};
    QFile f(QStringLiteral("/proc/asound/card%1/usbid").arg(cardNum));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromLatin1(f.readLine()).trimmed().toLower();
}

QString InstrumentManager::readUsbProductForCard(int cardNum)
{
    if (cardNum < 0) return {};
    QFileInfo fi(QStringLiteral("/sys/class/sound/card%1/device").arg(cardNum));
    if (!fi.exists()) return {};
    QDir d(fi.canonicalFilePath());
    for (int i = 0; i < 5; ++i) {
        QFile pf(d.filePath(QStringLiteral("product")));
        if (pf.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString::fromLatin1(pf.readLine()).trimmed();
        if (!d.cdUp()) break;
    }
    return {};
}

QList<InstrumentManager::DetectedDevice> InstrumentManager::detectDevices() const
{
    QList<DetectedDevice> result;

    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0)
        return result;

    snd_seq_client_info_t* cinfo = nullptr;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_client_info_set_client(cinfo, -1);

    while (snd_seq_query_next_client(seq, cinfo) >= 0) {
        int id = snd_seq_client_info_get_client(cinfo);
        if (id == SND_SEQ_CLIENT_SYSTEM) continue;
        // Skip our own sequencer client — it's the VDJ clock, not an instrument
        if (seq_ && id == seq_->clientId()) continue;

        DetectedDevice dev;
        dev.alsaClientId = id;
        dev.alsaName     = QString::fromUtf8(snd_seq_client_info_get_name(cinfo));

        snd_seq_port_info_t* pinfo = nullptr;
        snd_seq_port_info_alloca(&pinfo);
        snd_seq_port_info_set_client(pinfo, id);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq, pinfo) >= 0) {
            unsigned int cap = snd_seq_port_info_get_capability(pinfo);
            bool readable  = (cap & SND_SEQ_PORT_CAP_READ)  && (cap & SND_SEQ_PORT_CAP_SUBS_READ);
            bool writable  = (cap & SND_SEQ_PORT_CAP_WRITE) && (cap & SND_SEQ_PORT_CAP_SUBS_WRITE);
            if ((readable || writable) && dev.alsaPortId < 0)
                dev.alsaPortId = snd_seq_port_info_get_port(pinfo);
            if (readable)  dev.inputPorts++;
            if (writable)  dev.outputPorts++;
        }

        int cardNum      = snd_seq_client_info_get_card(cinfo);
        dev.usbId        = readUsbIdForCard(cardNum);
        QString usbProd  = readUsbProductForCard(cardNum);
        dev.displayName  = usbProd.isEmpty() ? dev.alsaName : usbProd;
        dev.key          = dev.usbId.isEmpty()
                               ? (QStringLiteral("alsa:") + dev.alsaName)
                               : dev.usbId;

        for (const InstrumentProfile& p : profiles_) {
            if (!dev.usbId.isEmpty() && p.usbId() == dev.usbId) {
                dev.profile     = &p;
                dev.displayName = p.name;
                break;
            }
        }

        result.append(dev);
    }
    snd_seq_close(seq);
    return result;
}

// ── Config accessors ──────────────────────────────────────────────────────────

QString InstrumentManager::midiInputKey() const
{
    return config_->loadValue(QStringLiteral("instruments"), QStringLiteral("midi_input"));
}

QString InstrumentManager::midiOutputKey() const
{
    return config_->loadValue(QStringLiteral("instruments"), QStringLiteral("midi_output"));
}

void InstrumentManager::setMidiInputKey(const QString& key)
{
    config_->saveValue(QStringLiteral("instruments"), QStringLiteral("midi_input"), key);
}

void InstrumentManager::setMidiOutputKey(const QString& key)
{
    config_->saveValue(QStringLiteral("instruments"), QStringLiteral("midi_output"), key);
}

// ── ALSA wiring ───────────────────────────────────────────────────────────────

// Mirror of C adj_mod_xdj700.c:
//   aconnect 'in_port_name' 'vdj-ui:MIDI clock'   (MIDI input role)
//   aconnect 'vdj-ui:MIDI clock' 'out_port_name'  (MIDI output role)
void InstrumentManager::wireDevices()
{
    if (!seq_ || !router_) return;
    if (!router_->isOpen()) router_->open();

    const QList<MidiPort> allIns  = router_->inputs();
    const QList<MidiPort> allOuts = router_->outputs();

    // Locate VDJ clock port in each direction
    MidiPort vdjReadPort;   // source for outgoing clock
    MidiPort vdjWritePort;  // destination for incoming control events (the MIDI map reader's port)
    for (const MidiPort& p : allIns) {
        if (p.clientId == seq_->clientId()) { vdjReadPort  = p; break; }
    }
    if (inClient_ >= 0) {
        vdjWritePort.clientId = inClient_;
        vdjWritePort.portId   = inPort_;
    }
    Q_UNUSED(allOuts)

    const QList<DetectedDevice> devs = detectDevices();
    QStringList wiredDesc;

    // MIDI input: controller → VDJ clock
    // (e.g. XDJ-700 MIDI out → vdj-ui:MIDI clock, exactly like adj_mod_xdj700)
    const QString inKey = midiInputKey();
    if (!inKey.isEmpty() && vdjWritePort.clientId > 0) {
        for (const DetectedDevice& d : devs) {
            if (d.key == inKey && d.inputPorts > 0 && d.alsaPortId >= 0) {
                MidiPort src;
                src.clientId = d.alsaClientId;
                src.portId   = d.alsaPortId;
                if (router_->connect(src, vdjWritePort))
                    wiredDesc << QStringLiteral("%1 → vdj-clock (input)").arg(d.displayName);
                break;
            }
        }
    }

    // MIDI output: VDJ clock → drum machine
    // (e.g. vdj-ui:MIDI clock → TR-8S MIDI in)
    const QString outKey = midiOutputKey();
    if (!outKey.isEmpty() && vdjReadPort.clientId > 0) {
        for (const DetectedDevice& d : devs) {
            if (d.key == outKey && d.outputPorts > 0 && d.alsaPortId >= 0) {
                MidiPort dst;
                dst.clientId = d.alsaClientId;
                dst.portId   = d.alsaPortId;
                if (router_->connect(vdjReadPort, dst))
                    wiredDesc << QStringLiteral("vdj-clock → %1 (output)").arg(d.displayName);
                break;
            }
        }
    }

    if (!wiredDesc.isEmpty())
        emit wired(wiredDesc.join(QStringLiteral(", ")));
}

// ── Joystick profile copy ─────────────────────────────────────────────────────

void InstrumentManager::applyProfileToJoystick(const QString& devicePath,
                                               const QString& profileId)
{
    const InstrumentProfile* p = nullptr;
    for (const InstrumentProfile& pr : profiles_) {
        if (pr.id == profileId) { p = &pr; break; }
    }
    if (!p || p->joystickDefaults.isEmpty()) return;

    for (auto it = p->joystickDefaults.cbegin(); it != p->joystickDefaults.cend(); ++it)
        config_->saveJoystickMapping(devicePath, it.key(), it.value());

    config_->saveValue(QStringLiteral("joystick_profiles"), devicePath, profileId);
}

} // namespace vdj
