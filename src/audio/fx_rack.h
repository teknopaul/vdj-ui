#ifndef FX_RACK_H
#define FX_RACK_H

#include <QtGlobal>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include "djtoolz_tool.h"
#include "ladspa_host.h"

namespace vdj {

// The FX tools in the order they are applied. Shared by the audio outputs; each output has its own FxRunner (an effect keeps
// state, so one running copy per stream).
class FxRack {
public:
    void add(const std::shared_ptr<FxTool>& tool);
    void remove(int toolId);
    void clear();
    std::vector<std::shared_ptr<FxTool>> tools() const;
    quint64 version() const { return version_.load(); }      // changes when tools are added or removed

private:
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<FxTool>> tools_;
    std::atomic<quint64> version_{ 1 };
};

// Applies the rack's effects to the blocks of one audio output. Called from that output's thread only.
class FxRunner {
public:
    explicit FxRunner(const FxRack* rack) : rack_(rack) {}

    // 16 bit interleaved stereo, in place. Does nothing (and costs nothing) while no effect is switched on.
    void process(qint16* stereo, int frames);
    void process(float* stereo, int frames);                 // -1..1 floats
    int activeEffects() const { return active_; }

private:
    struct Slot {
        std::shared_ptr<FxTool> tool;
        quint64 revision = 0;
        QString plugin;
        std::unique_ptr<LadspaInstance> instance;
    };
    void sync();
    void update(Slot& slot);

    const FxRack* rack_;
    quint64 version_ = 0;
    std::vector<Slot> slots_;
    std::vector<float> buffer_;
    int active_ = 0;
};

} // namespace vdj

#endif // FX_RACK_H
