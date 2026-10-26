#include "fx_rack.h"

#include <algorithm>
#include <cmath>

namespace vdj {

void FxRack::add(const std::shared_ptr<FxTool>& tool)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tools_.push_back(tool);
    }
    ++version_;
}

void FxRack::remove(int toolId)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tools_.erase(std::remove_if(tools_.begin(), tools_.end(),
                                    [&](const std::shared_ptr<FxTool>& t) { return t->id() == toolId; }),
                     tools_.end());
    }
    ++version_;
}

void FxRack::clear()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tools_.clear();
    }
    ++version_;
}

std::vector<std::shared_ptr<FxTool>> FxRack::tools() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tools_;
}

void FxRunner::sync()
{
    const quint64 v = rack_->version();
    if (v == version_)
        return;
    version_ = v;
    std::vector<Slot> next;
    for (const auto& t : rack_->tools()) {
        Slot s;
        auto old = std::find_if(slots_.begin(), slots_.end(), [&](const Slot& o) { return o.tool == t; });
        if (old != slots_.end())
            s = std::move(*old);                     // keeps the running instance (and a reverb's tail)
        else
            s.tool = t;
        next.push_back(std::move(s));
    }
    slots_ = std::move(next);
}

// Make the instance match the tool: the plugin and the knob values
void FxRunner::update(Slot& slot)
{
    const FxTool::Spec spec = slot.tool->spec();
    slot.revision = slot.tool->revision();
    if (slot.plugin != spec.plugin || (!slot.instance && !spec.plugin.isEmpty())) {
        slot.plugin = spec.plugin;
        slot.instance.reset();
        LadspaPluginInfo info;
        if (!spec.plugin.isEmpty() && LadspaHost::find(spec.plugin, &info))
            slot.instance = LadspaInstance::create(info);
    }
    if (!slot.instance)
        return;
    const auto& controls = slot.instance->info().controls;
    for (size_t i = 0; i < controls.size(); ++i) {
        const auto it = spec.values.find(controls[i].name);
        slot.instance->setControl(int(i), it == spec.values.end() ? controls[i].def : it.value());
    }
}

void FxRunner::process(float* stereo, int frames)
{
    sync();
    active_ = 0;
    for (Slot& s : slots_) {
        if (s.tool->bypassed())
            continue;
        if (s.revision != s.tool->revision() || (!s.instance && s.plugin != s.tool->spec().plugin))
            update(s);
        if (!s.instance)
            continue;
        s.instance->process(stereo, frames);
        ++active_;
    }
}

void FxRunner::process(qint16* stereo, int frames)
{
    sync();
    bool any = false;
    for (const Slot& s : slots_)
        if (!s.tool->bypassed()) {
            any = true;
            break;
        }
    if (!any) {
        active_ = 0;
        return;
    }
    const size_t n = size_t(frames) * 2;
    if (buffer_.size() < n)
        buffer_.resize(n);
    for (size_t i = 0; i < n; ++i)
        buffer_[i] = float(stereo[i]) / 32768.0f;
    process(buffer_.data(), frames);
    for (size_t i = 0; i < n; ++i) {
        float v = buffer_[i];
        v = (v == v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f;       // a plugin that blew up gives silence, not noise
        stereo[i] = qint16(std::lrintf(v * 32767.0f));
    }
}

} // namespace vdj
