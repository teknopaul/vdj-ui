#ifndef VDJ_WRAPPER_H
#define VDJ_WRAPPER_H

#include <cstdint>
#include <cstdio>

extern "C" {
#include <cdj/vdj.h>
}

namespace vdj {

class VdjWrapper {
public:
    VdjWrapper();
    ~VdjWrapper();

    VdjWrapper(const VdjWrapper&) = delete;
    VdjWrapper& operator=(const VdjWrapper&) = delete;
    VdjWrapper(VdjWrapper&& other) noexcept;
    VdjWrapper& operator=(VdjWrapper&& other) noexcept;

    bool init(const char* interface, uint32_t flags);
    bool openSockets();
    bool execDiscovery();
    void closeSockets();

    void sendKeepalive();
    int sendStatus();
    void broadcastBeat(float bpm, uint8_t barPos);
    void setPlaying(bool playing);
    void setBpm(float bpm);

    void startManagedThreads();
    void stopThreads();

    vdj_t* get() const { return v_; }
    uint8_t playerId() const;
    float bpm() const;
    uint8_t masterState() const;
    bool isActive() const;

private:
    void destroy();

    vdj_t* v_ = nullptr;
};

} // namespace vdj

#endif // VDJ_WRAPPER_H
