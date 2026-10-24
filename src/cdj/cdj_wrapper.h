#ifndef CDJ_WRAPPER_H
#define CDJ_WRAPPER_H

#include <cstdint>
#include <string>

extern "C" {
#include <cdj/cdj.h>
}

namespace vdj {

struct BeatInfo {
    uint8_t  playerId;
    float    bpm;
    uint32_t pitch;
    uint8_t  barPos;
    uint32_t nextBeatMs;
};

struct StatusInfo {
    uint8_t  playerId;
    float    bpm;
    uint32_t pitch;
    uint8_t  flags;
    uint8_t  masterState;
    uint8_t  active;
};

class CdjWrapper {
public:
    static std::string typeToString(uint16_t port, uint8_t type, uint8_t subType = 0);
    static std::string bpmToString(float bpm);
    static std::string pitchToString(uint32_t pitch);
    static double pitchToPercentage(uint32_t pitch);
    static double pitchToMultiplier(uint32_t pitch);
    static float calculatedBpm(uint16_t trackBpm, uint32_t pitch);
    static uint32_t beatMillis(float bpm);

    static BeatInfo parseBeat(const cdj_beat_packet_t* pkt);
    static StatusInfo parseStatus(const cdj_cdj_status_packet_t* pkt);
};

} // namespace vdj

#endif // CDJ_WRAPPER_H
