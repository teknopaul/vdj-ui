#include "midi_router.h"

#include <alsa/asoundlib.h>

namespace vdj {

static inline snd_seq_t* seq(void* p) { return static_cast<snd_seq_t*>(p); }

MidiRouter::MidiRouter(QObject* parent) : QObject(parent) {}

MidiRouter::~MidiRouter() { close(); }

bool MidiRouter::open()
{
    if (seq_) return true;
    snd_seq_t* s = nullptr;
    if (snd_seq_open(&s, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0)
        return false;
    snd_seq_set_client_name(s, "vdj-midi-router");
    seq_ = s;
    return true;
}

void MidiRouter::close()
{
    if (seq_) { snd_seq_close(seq(seq_)); seq_ = nullptr; }
}

QList<MidiPort> MidiRouter::enumerate(bool wantInput) const
{
    QList<MidiPort> result;
    if (!seq_) return result;

    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t*   pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq(seq_), cinfo) >= 0) {
        int cid = snd_seq_client_info_get_client(cinfo);
        snd_seq_port_info_set_client(pinfo, cid);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq(seq_), pinfo) >= 0) {
            unsigned caps = snd_seq_port_info_get_capability(pinfo);
            bool hasRead  = (caps & SND_SEQ_PORT_CAP_READ)  &&
                            (caps & SND_SEQ_PORT_CAP_SUBS_READ);
            bool hasWrite = (caps & SND_SEQ_PORT_CAP_WRITE) &&
                            (caps & SND_SEQ_PORT_CAP_SUBS_WRITE);
            if (wantInput  && !hasRead)  continue;
            if (!wantInput && !hasWrite) continue;
            MidiPort p;
            p.clientId = cid;
            p.portId   = snd_seq_port_info_get_port(pinfo);
            p.name     = QString::fromLatin1(snd_seq_client_info_get_name(cinfo))
                       + QLatin1Char(':')
                       + QString::fromLatin1(snd_seq_port_info_get_name(pinfo));
            p.isInput  = hasRead;
            p.isOutput = hasWrite;
            result.append(p);
        }
    }
    return result;
}

QList<MidiPort> MidiRouter::inputs()  const { return enumerate(true);  }
QList<MidiPort> MidiRouter::outputs() const { return enumerate(false); }

bool MidiRouter::connect(const MidiPort& src, const MidiPort& dst)
{
    if (!seq_) return false;
    snd_seq_port_subscribe_t* sub;
    snd_seq_port_subscribe_alloca(&sub);
    snd_seq_addr_t sender{ (unsigned char)src.clientId, (unsigned char)src.portId };
    snd_seq_addr_t dest  { (unsigned char)dst.clientId, (unsigned char)dst.portId };
    snd_seq_port_subscribe_set_sender(sub, &sender);
    snd_seq_port_subscribe_set_dest(sub, &dest);
    return snd_seq_subscribe_port(seq(seq_), sub) >= 0;
}

bool MidiRouter::disconnect(const MidiPort& src, const MidiPort& dst)
{
    if (!seq_) return false;
    snd_seq_port_subscribe_t* sub;
    snd_seq_port_subscribe_alloca(&sub);
    snd_seq_addr_t sender{ (unsigned char)src.clientId, (unsigned char)src.portId };
    snd_seq_addr_t dest  { (unsigned char)dst.clientId, (unsigned char)dst.portId };
    snd_seq_port_subscribe_set_sender(sub, &sender);
    snd_seq_port_subscribe_set_dest(sub, &dest);
    return snd_seq_unsubscribe_port(seq(seq_), sub) >= 0;
}

void MidiRouter::refresh() { emit portsChanged(); }

} // namespace vdj
