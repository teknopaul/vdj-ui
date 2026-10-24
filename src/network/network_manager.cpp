#include "network_manager.h"
#include "debug/packet_logger.h"
#include <arpa/inet.h>
#include <cstring>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
extern "C" {
#include <cdj/vdj_master.h>
}

namespace vdj {

static NetworkManager* instanceFrom(vdj_t* v)
{
    return v ? static_cast<NetworkManager*>(v->client) : nullptr;
}

// Runs on the thread that calls vdj_exec_discovery() (the GUI thread, it blocks for about four seconds): let the status bar
// repaint between the packets, but never take user input in the middle of the join.
static void onProgress(vdj_s* v, int phase, int step, int steps, int playerId)
{
    NetworkManager* nm = instanceFrom(reinterpret_cast<vdj_t*>(v));
    if (!nm) return;
    QString text;
    switch (phase) {
    case VDJ_PHASE_HELLO:   text = QStringLiteral("Joining the link: announcing (packet %1 of %2)").arg(step).arg(steps); break;
    case VDJ_PHASE_MAC:     text = QStringLiteral("Joining the link: claiming by MAC address (stage 1, packet %1 of %2)").arg(step).arg(steps); break;
    case VDJ_PHASE_NUMBER:  text = QStringLiteral("Joining the link: asking for player number %1 (stage 2, packet %2 of %3)").arg(playerId).arg(step).arg(steps); break;
    case VDJ_PHASE_CONFIRM: text = QStringLiteral("Joining the link: taking player number %1 (stage 3, packet %2 of %3)").arg(playerId).arg(step).arg(steps); break;
    case VDJ_PHASE_DONE:    text = QStringLiteral("Joined the link as player %1").arg(playerId); break;
    default:                text = QStringLiteral("Joining the link (phase %1)").arg(phase); break;
    }
    PacketLogger::instance().log(QStringLiteral("USER"), text);
    emit nm->discoveryPhase(text);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

static void onUpdate(vdj_t* v, cdj_cdj_status_packet_t* cs_pkt)
{
    NetworkManager* nm = instanceFrom(v);
    if (!nm) return;
    int pid = cdj_status_player_id(cs_pkt);
    double bpm = cdj_status_calculated_bpm(cs_pkt);
    int pitch = static_cast<int>(cdj_status_pitch(cs_pkt));
    int flags = cdj_status_flags(cs_pkt);
    int masterState = cdj_status_master_state(cs_pkt);
    int active = cdj_status_active(cs_pkt);

    QString detail = QStringLiteral("p%1 bpm=%2 pitch=%3 flags=0x%4 master=%5 "
                                    "active=%6 track=%7 model=%8")
        .arg(pid)
        .arg(bpm, 0, 'f', 2)
        .arg(pitch)
        .arg(flags, 0, 16)
        .arg(masterState)
        .arg(active)
        .arg(cdj_status_track_id(cs_pkt))
        .arg(QString::fromUtf8(cdj_status_model(cs_pkt)));
    PacketLogger::instance().log(QStringLiteral("UPDATE"), detail);

    // the lib's update handler has already folded this packet into the backline (sync counter, master id): we now know what
    // the link agrees on. Real players start their status within a second of joining (capture prolink-3-boots), so do not wait
    // for a master to appear; an idle link has none.
    if (v->backline) nm->startStatusThread();
    if (nm->isJoining()) return;
    emit nm->statusUpdated(pid, bpm, pitch, flags, masterState, active);
    // only report a master when someone is one: ourselves after a handoff (v->master), or a deck flagging itself
    if (v->master) emit nm->masterChanged(v->player_id);
    else if (flags & CDJ_STAT_FLAG_MASTER) emit nm->masterChanged(pid);
}

static void onBeat(vdj_t* v, cdj_beat_packet_t* b_pkt)
{
    NetworkManager* nm = instanceFrom(v);
    if (!nm) return;
    const double sequencerOffset = nm->sequencerOffsetNow();   // read first: the delivery to the GUI thread adds delay
    int pid = cdj_beat_player_id(b_pkt);
    double bpm = cdj_beat_calculated_bpm(b_pkt);
    int barPos = cdj_beat_bar_pos(b_pkt);

    PacketLogger::instance().log(QStringLiteral("BEAT"),
        QStringLiteral("p%1 bpm=%2 bar=%3")
            .arg(pid).arg(bpm, 0, 'f', 2).arg(barPos));

    emit nm->beatReceived(pid, bpm, barPos, sequencerOffset);
}

static void onBeatUnicast(vdj_t* v, cdj_beat_packet_t* b_pkt)
{
    NetworkManager* nm = instanceFrom(v);
    if (!nm) return;
    // master handoff DM (request or response); the outcome arrives in following status packets
    PacketLogger::instance().log(QStringLiteral("MASTER"),
        QStringLiteral("handoff message from p%1 ok=%2 master_req=%3 master=%4")
            .arg(cdj_beat_player_id(b_pkt)).arg(cdj_beat_master_ok(b_pkt)).arg(int(v->master_req)).arg(int(v->master)));
}

void onDiscovery(vdj_t* v, cdj_discovery_packet_t* d_pkt)
{
    NetworkManager* nm = instanceFrom(v);
    if (!nm) return;
    int pid = cdj_discovery_player_id(d_pkt);
    if (pid == 0)
        return;

    QString ip;
    if (v->backline && pid <= VDJ_MAX_BACKLINE) {
        vdj_link_member_t* m = v->backline->link_members[pid];
        if (m && m->ip_addr) {
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &m->ip_addr->sin_addr, buf, sizeof(buf));
            ip = QString::fromLatin1(buf);
        }
    }

    QString model = QString::fromUtf8(cdj_discovery_model(d_pkt));
    QString detail = QStringLiteral("p%1 model=%2 ip=%3")
        .arg(pid)
        .arg(model)
        .arg(ip.isEmpty() ? QStringLiteral("?") : ip);
    PacketLogger::instance().log(QStringLiteral("DISCOVERY"), detail);

    // Not while we join: the GUI only sets its deck rows up in the connected() handler, which would throw these away. The next
    // keepalive of the deck (2 s) announces it.
    if (nm->isJoining()) return;
    if (!nm->knownMembers_.contains(pid)) {
        nm->knownMembers_.insert(pid);
        emit nm->deckJoined(pid, ip, model);
    }
}

NetworkManager::NetworkManager(QObject* parent)
    : QObject(parent)
{
}

NetworkManager::~NetworkManager()
{
    teardown();
}

bool NetworkManager::connectToLink(const QString& iface, uint32_t flags)
{
    teardown();

    v_ = iface.isEmpty()
             ? vdj_init(flags)
             : vdj_init_iface(const_cast<char*>(iface.toUtf8().constData()), flags);
    if (!v_) {
        emit errorOccurred(QString("vdj_init failed on iface '%1'").arg(iface));
        return false;
    }

    v_->client = this;
    PacketLogger::instance().log(QStringLiteral("USER"), QStringLiteral("announcing as %1DJ-1000").arg(QChar(v_->model)));

    if (vdj_open_sockets(v_) != VDJ_OK) {
        emit errorOccurred("Failed to open ProLink sockets");
        teardown();
        return false;
    }

    vdj_init_managed_discovery_thread(v_, onDiscovery);
    vdj_init_managed_update_thread(v_, onUpdate);
    vdj_init_managed_beat_thread(v_, onBeat);
    if (vdj_init_managed_beat_unicast_thread(v_, onBeatUnicast) != CDJ_OK)
        emit errorOccurred("Failed to start master handoff (beat unicast) thread");
    v_->progress_h = onProgress;
    joining_ = true;
    vdj_exec_discovery(v_);
    v_->progress_h = nullptr;
    // Our status (master flag, handoff, sync counter) must reach the players or they never see us become master,
    // but sending it before we have seen the link's sync counter and master upsets the players ("older firmware"), and so
    // does staying silent for several seconds after joining. onUpdate starts it with the first deck status (about half a
    // second after our first keepalive); this is the fallback for a link where no deck sends status.
    statusStarted_ = false;
    QTimer::singleShot(kStatusStartTimeoutMs, this, [this]() { startStatusThread(); });

    joining_ = false;
    emit connected();
    return true;
}

void NetworkManager::startStatusThread()
{
    if (!v_ || statusStarted_.exchange(true)) return;
    if (vdj_init_status_thread(v_) != CDJ_OK)
        emit errorOccurred("Failed to start status thread");
    else
        PacketLogger::instance().log(QStringLiteral("USER"), QStringLiteral("status thread started"));
}

void NetworkManager::disconnectFromLink()
{
    teardown();
}

void NetworkManager::teardown()
{
    if (v_) {
        vdj_stop_managed_discovery_thread(v_);
        vdj_stop_managed_beat_unicast_thread(v_);
        vdj_stop_threads(v_);
        QThread::msleep(300);   // let the unicast thread leave its poll before the sockets close
        vdj_close_sockets(v_);
        v_->client = nullptr;
        vdj_destroy(v_);
        v_ = nullptr;
        knownMembers_.clear();
        joining_ = false;
        statusStarted_ = true;   // nothing to start until the next connect
    }
}

uint8_t NetworkManager::localPlayerId() const
{
    return v_ ? v_->player_id : 0;
}

QString NetworkManager::ipForPlayer(int playerId) const
{
    if (!v_) return QString();

    // Our own address lives on the local VDJ struct as a string.
    if (playerId == int(v_->player_id))
        return v_->ip_address ? QString::fromLatin1(v_->ip_address) : QString();

    // Other players are tracked in the backline keyed by player id.
    if (playerId >= 0 && playerId <= VDJ_MAX_BACKLINE) {
        vdj_link_member_t* m = v_->backline ? v_->backline->link_members[playerId] : nullptr;
        if (m && m->ip_addr) {
            char buf[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &m->ip_addr->sin_addr, buf, sizeof(buf));
            return QString::fromLatin1(buf);
        }
    }
    return QString();
}

void NetworkManager::setLocalBpm(double bpm)
{
    if (v_) {
        vdj_set_bpm(v_, static_cast<float>(bpm));
        PacketLogger::instance().log(QStringLiteral("USER"),
            QStringLiteral("set bpm=%1").arg(bpm, 0, 'f', 2));
    }
}

void NetworkManager::setLocalPitch(double percentage)
{
    if (!v_) return;
    // raw pitch encoding: 0x100000 == 0.00% (see cdj_pitch_to_multiplier)
    double multiplier = 1.0 + percentage / 100.0;
    v_->pitch = static_cast<int32_t>(multiplier * 0x100000);
    PacketLogger::instance().log(QStringLiteral("USER"),
        QStringLiteral("set pitch=%1%").arg(percentage, 0, 'f', 2));
}

void NetworkManager::setLocalPlaying(bool playing)
{
    if (v_) {
        vdj_set_playing(v_, playing ? 1 : 0);
        PacketLogger::instance().log(QStringLiteral("USER"),
            playing ? QStringLiteral("play") : QStringLiteral("pause"));
    }
}

void NetworkManager::broadcastBeat(double bpm, int barPos)
{
    if (v_) {
        vdj_broadcast_beat(v_, static_cast<float>(bpm), static_cast<uint8_t>(barPos));
        PacketLogger::instance().log(QStringLiteral("USER"),
            QStringLiteral("broadcast beat bpm=%1 bar=%2").arg(bpm, 0, 'f', 2).arg(barPos));
    }
}

void NetworkManager::requestMaster()
{
    if (!v_) return;

    PacketLogger::instance().log(QStringLiteral("USER"), QStringLiteral("request master"));

    if (!v_->backline) return;

    // with no existing master vdj_request_master claims it immediately, otherwise it DMs the current master
    const bool claimedNow = v_->backline->master_id == 0;
    vdj_request_master(v_);
    if (claimedNow) emit masterChanged(v_->player_id);
}

void NetworkManager::sendKeepalive()
{
    if (v_) vdj_send_keepalive(v_);
}

} // namespace vdj
