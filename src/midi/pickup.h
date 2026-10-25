#pragma once

#include <cstdlib>

namespace vdj {

// Soft takeover for a controller fader / knob: after reset() the hardware value is ignored until it reaches the
// software value (within 2 steps, or it crossed it between two events); then it follows until the next reset().
class Pickup {
public:
    void setEnabled(bool on) { enabled_ = on; reset(); }
    // Same, but only re-arms when the setting actually changes
    void setEnabledKeepState(bool on) { if (on != enabled_) setEnabled(on); }
    bool enabled() const { return enabled_; }
    void reset() { caught_ = false; last_ = -1; }

    // True when *out should be applied.
    bool filter(int hardwareValue, int softwareValue, int* out)
    {
        if (!enabled_ || caught_) {
            *out = hardwareValue;
            return true;
        }
        const bool near = std::abs(hardwareValue - softwareValue) <= 2;
        const bool crossed = last_ >= 0 && (last_ - softwareValue) * (hardwareValue - softwareValue) <= 0;
        last_ = hardwareValue;
        if (near || crossed) {
            caught_ = true;
            *out = hardwareValue;
            return true;
        }
        return false;
    }

private:
    bool enabled_ = true;
    bool caught_ = false;
    int  last_ = -1;
};

} // namespace vdj
