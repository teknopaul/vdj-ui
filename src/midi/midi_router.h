#ifndef MIDI_ROUTER_H
#define MIDI_ROUTER_H

#include <QObject>
#include <QList>
#include <QString>

namespace vdj {

struct MidiPort {
    int     clientId = 0;
    int     portId   = 0;
    QString name;
    bool    isInput  = false;
    bool    isOutput = false;
};

class MidiRouter : public QObject {
    Q_OBJECT
public:
    explicit MidiRouter(QObject* parent = nullptr);
    ~MidiRouter() override;

    bool open();
    void close();
    bool isOpen() const { return seq_ != nullptr; }  // seq_ is snd_seq_t*

    QList<MidiPort> inputs()  const;
    QList<MidiPort> outputs() const;

    // Connect src port → dst port via ALSA subscription.
    bool connect(const MidiPort& src, const MidiPort& dst);
    bool disconnect(const MidiPort& src, const MidiPort& dst);

    void refresh();

signals:
    void portsChanged();

private:
    QList<MidiPort> enumerate(bool inputs) const;
    void* seq_ = nullptr;  // snd_seq_t* — opaque to avoid header pollution
};

} // namespace vdj
#endif
