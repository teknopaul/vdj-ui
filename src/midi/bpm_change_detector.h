#ifndef BPM_CHANGE_DETECTOR_H
#define BPM_CHANGE_DETECTOR_H

namespace vdj {

// A deck gives no sign that its BPM "is being changed", only the reported value, which moves every few beats while the pitch fader is
// moved. This remembers when it last moved: recentlyChanged() is true for kHoldBeats beats (2 bars) after a change, so the difflock can
// correct often, in small steps, while the tempo is moving and rarely, in bigger ones, when it is not.
class BpmChangeDetector {
public:
    static constexpr int kHoldBeats = 8;
    static constexpr double kMinChange = 0.004;   // BPM: a smaller move is the deck's rounding, not the fader

    // The reported BPM at a beat of the deck.
    void onBeat(double reportedBpm)
    {
        if (reportedBpm <= 0.0)
            return;
        if (last_ > 0.0 && (reportedBpm - last_ > kMinChange || last_ - reportedBpm > kMinChange))
            since_ = 0;
        else if (since_ < kHoldBeats)
            ++since_;
        last_ = reportedBpm;
    }
    bool recentlyChanged() const { return since_ < kHoldBeats; }
    void reset() { last_ = 0.0; since_ = kHoldBeats; }

private:
    double last_ = 0.0;
    int since_ = kHoldBeats;
};

} // namespace vdj

#endif // BPM_CHANGE_DETECTOR_H
