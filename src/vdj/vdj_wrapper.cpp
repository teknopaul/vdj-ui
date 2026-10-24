#include "vdj_wrapper.h"
#include <utility>
#include <cstdio>

namespace vdj {

static void stubDiscoveryPh(vdj_t* /*v*/, cdj_discovery_packet_t* /*d_pkt*/) {}
static void stubDiscoveryUnicastPh(vdj_t* /*v*/, cdj_discovery_packet_t* /*d_pkt*/) {}
static void stubUpdatePh(vdj_t* /*v*/, cdj_cdj_status_packet_t* /*cs_pkt*/) {}
static void stubBeatPh(vdj_t* /*v*/, cdj_beat_packet_t* /*b_pkt*/) {}
static void stubBeatUnicastPh(vdj_t* /*v*/, cdj_beat_packet_t* /*b_pkt*/) {}

VdjWrapper::VdjWrapper() = default;

VdjWrapper::~VdjWrapper()
{
    destroy();
}

VdjWrapper::VdjWrapper(VdjWrapper&& other) noexcept : v_(other.v_)
{
    other.v_ = nullptr;
}

VdjWrapper& VdjWrapper::operator=(VdjWrapper&& other) noexcept
{
    if (this != &other) {
        destroy();
        v_ = other.v_;
        other.v_ = nullptr;
    }
    return *this;
}

bool VdjWrapper::init(const char* interface, uint32_t flags)
{
    destroy();
    if (interface)
        v_ = vdj_init_iface(const_cast<char*>(interface), flags);
    else
        v_ = vdj_init(flags);
    return v_ != nullptr;
}

bool VdjWrapper::openSockets()
{
    return v_ && vdj_open_sockets(v_) == VDJ_OK;
}

bool VdjWrapper::execDiscovery()
{
    return v_ && vdj_exec_discovery(v_) == VDJ_OK;
}

void VdjWrapper::closeSockets()
{
    if (v_) vdj_close_sockets(v_);
}

void VdjWrapper::sendKeepalive()
{
    if (v_) vdj_send_keepalive(v_);
}

int VdjWrapper::sendStatus()
{
    return v_ ? vdj_send_status(v_) : VDJ_ERROR;
}

void VdjWrapper::broadcastBeat(float bpm, uint8_t barPos)
{
    if (v_) vdj_broadcast_beat(v_, bpm, barPos);
}

void VdjWrapper::setPlaying(bool playing)
{
    if (v_) vdj_set_playing(v_, playing ? 1 : 0);
}

void VdjWrapper::setBpm(float bpm)
{
    if (v_) vdj_set_bpm(v_, bpm);
}

void VdjWrapper::startManagedThreads()
{
    if (!v_) return;
    vdj_init_managed_update_thread(v_, stubUpdatePh);
    vdj_init_managed_beat_thread(v_, stubBeatPh);
}

void VdjWrapper::stopThreads()
{
    if (v_) vdj_stop_threads(v_);
}

uint8_t VdjWrapper::playerId() const
{
    return v_ ? v_->player_id : 0;
}

float VdjWrapper::bpm() const
{
    return v_ ? v_->bpm : 0.0f;
}

uint8_t VdjWrapper::masterState() const
{
    return v_ ? v_->master_state : 0;
}

bool VdjWrapper::isActive() const
{
    return v_ && v_->active;
}

void VdjWrapper::destroy()
{
    if (v_) {
        vdj_stop_threads(v_);
        vdj_close_sockets(v_);
        vdj_destroy(v_);
        v_ = nullptr;
    }
}

} // namespace vdj
