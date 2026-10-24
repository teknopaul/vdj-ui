#include "cdj_wrapper.h"
#include <cstdlib>
#include <cstring>

namespace vdj {

std::string CdjWrapper::typeToString(uint16_t port, uint8_t type, uint8_t subType)
{
    const char* s = cdj_type_to_string(port, type, subType);
    return std::string(s ? s : "unknown");
}

std::string CdjWrapper::bpmToString(float bpm)
{
    char* s = cdj_bpm_to_string(bpm);
    std::string result(s ? s : "0.00");
    free(s);
    return result;
}

std::string CdjWrapper::pitchToString(uint32_t pitch)
{
    char* s = cdj_pitch_to_string(pitch);
    std::string result(s ? s : "+0.00%");
    free(s);
    return result;
}

double CdjWrapper::pitchToPercentage(uint32_t pitch)
{
    return cdj_pitch_to_percentage(pitch);
}

double CdjWrapper::pitchToMultiplier(uint32_t pitch)
{
    return cdj_pitch_to_multiplier(pitch);
}

float CdjWrapper::calculatedBpm(uint16_t trackBpm, uint32_t pitch)
{
    return cdj_calculated_bpm(trackBpm, pitch);
}

uint32_t CdjWrapper::beatMillis(float bpm)
{
    return cdj_beat_millis(bpm);
}

BeatInfo CdjWrapper::parseBeat(const cdj_beat_packet_t* pkt)
{
    BeatInfo info{};
    if (!pkt) return info;
    auto* p = const_cast<cdj_beat_packet_t*>(pkt);
    info.playerId = cdj_beat_player_id(p);
    info.bpm      = cdj_beat_calculated_bpm(p);
    info.pitch    = cdj_beat_pitch(p);
    info.barPos   = cdj_beat_bar_pos(p);
    info.nextBeatMs = cdj_beat_next(p);
    return info;
}

StatusInfo CdjWrapper::parseStatus(const cdj_cdj_status_packet_t* pkt)
{
    StatusInfo info{};
    if (!pkt) return info;
    auto* p = const_cast<cdj_cdj_status_packet_t*>(pkt);
    info.playerId   = cdj_status_player_id(p);
    info.bpm        = cdj_status_calculated_bpm(p);
    info.pitch      = cdj_status_pitch(p);
    info.flags      = cdj_status_flags(p);
    info.masterState = cdj_status_master_state(p);
    info.active     = cdj_status_active(p);
    return info;
}

} // namespace vdj
