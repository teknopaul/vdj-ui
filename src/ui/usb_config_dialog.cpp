#include "usb_config_dialog.h"
#include "config/config_manager.h"
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QDialogButtonBox>

#include <alsa/asoundlib.h>

namespace vdj {

static QString clientTypeName(const char* name)
{
    QString n = QString::fromUtf8(name);
    QString lower = n.toLower();
    if (lower.contains("virtual") || lower.contains("dj") || lower.contains("seq"))
        return QStringLiteral("Sequencer");
    if (lower.contains("usb") || lower.contains("midi"))
        return QStringLiteral("USB");
    if (n.isEmpty())
        return QStringLiteral("Unknown");
    return QStringLiteral("ALSA");
}

QList<MidiClientInfo> UsbConfigDialog::scanMidiClients()
{
    QList<MidiClientInfo> out;
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0)
        return out;

    snd_seq_client_info_t* cinfo = nullptr;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq, cinfo) >= 0) {
        int id = snd_seq_client_info_get_client(cinfo);
        if (id == SND_SEQ_CLIENT_SYSTEM)
            continue; // id 0: system client, skip

        MidiClientInfo info;
        info.id = id;
        info.name = QString::fromUtf8(snd_seq_client_info_get_name(cinfo));
        info.type = clientTypeName(snd_seq_client_info_get_name(cinfo));

        snd_seq_port_info_t* pinfo = nullptr;
        snd_seq_port_info_alloca(&pinfo);
        snd_seq_port_info_set_client(pinfo, id);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq, pinfo) >= 0) {
            if (snd_seq_port_info_get_capability(pinfo) & SND_SEQ_PORT_CAP_READ)
                info.inputPorts++;
            if (snd_seq_port_info_get_capability(pinfo) & SND_SEQ_PORT_CAP_WRITE)
                info.outputPorts++;
        }
        out.append(info);
    }
    snd_seq_close(seq);
    return out;
}

int UsbConfigDialog::midiOutputClientId()
{
    for (const MidiClientInfo& c : scanMidiClients()) {
        QString lower = c.name.toLower();
        if (lower.contains("virtualdj") || lower.contains("virtual dj"))
            return c.id;
    }
    return -1;
}

UsbConfigDialog::UsbConfigDialog(ConfigManager* config, QWidget* parent)
    : QDialog(parent), config_(config)
{
    setWindowTitle(QStringLiteral("USB / MIDI Configuration"));
    resize(520, 420);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(QStringLiteral(
        "Detected ALSA MIDI clients and routing. Use the source to pick a USB "
        "drum machine sequencer input and route it to VDJ's MIDI output."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    devices_ = new QListWidget(this);
    layout->addWidget(devices_);

    auto* form = new QFormLayout();

    source_ = new QComboBox(this);
    destination_ = new QComboBox(this);
    QPushButton* refresh = new QPushButton(QStringLiteral("Refresh"), this);

    form->addRow(QStringLiteral("Source (USB input):"), source_);
    form->addRow(QStringLiteral("Destination (VDJ MIDI):"), destination_);
    form->addRow(QString(), refresh);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    populateDevices();

    connect(refresh, &QPushButton::clicked, this, [this]() { populateDevices(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        saveRoutes();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void UsbConfigDialog::populateDevices()
{
    devices_->clear();
    source_->clear();
    destination_->clear();
    sourceIds_.clear();
    destIds_.clear();

    const QList<MidiClientInfo> clients = scanMidiClients();
    for (const MidiClientInfo& c : clients) {
        devices_->addItem(QStringLiteral("[%1] %2 (%3) in:%4 out:%5")
            .arg(c.id).arg(c.name).arg(c.type).arg(c.inputPorts).arg(c.outputPorts));
        if (c.inputPorts > 0) {
            source_->addItem(QStringLiteral("%1 (%2)").arg(c.name).arg(c.id));
            sourceIds_.append(c.id);
        }
        destination_->addItem(QStringLiteral("%1 (%2)").arg(c.name).arg(c.id));
        destIds_.append(c.id);
    }

    int vdj = midiOutputClientId();
    const QString savedSrc = config_->loadValue("usb", "source");
    const QString savedDst = config_->loadValue("usb", "destination");

    int si = 0;
    if (!savedSrc.isEmpty()) {
        int id = savedSrc.toInt();
        si = sourceIds_.indexOf(id);
    }
    source_->setCurrentIndex(si < 0 ? 0 : si);

    int di = destIds_.indexOf(vdj);
    if (di < 0 && !savedDst.isEmpty()) {
        di = destIds_.indexOf(savedDst.toInt());
    }
    destination_->setCurrentIndex(di < 0 ? 0 : di);
}

void UsbConfigDialog::saveRoutes()
{
    int srcIdx = source_->currentIndex();
    int dstIdx = destination_->currentIndex();
    if (srcIdx >= 0 && srcIdx < sourceIds_.size())
        config_->saveValue("usb", "source", QString::number(sourceIds_[srcIdx]));
    if (dstIdx >= 0 && dstIdx < destIds_.size())
        config_->saveValue("usb", "destination", QString::number(destIds_[dstIdx]));
}

} // namespace vdj