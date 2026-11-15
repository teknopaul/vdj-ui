#ifndef TRAKCRATE_MIX_COMPRESSOR_H
#define TRAKCRATE_MIX_COMPRESSOR_H

#include <deque>
#include <vector>

namespace trakcrate {

// The built-in master compressor of a studio mix: a hard knee peak compressor followed by a brick wall limiter at 0 dBFS, both with 2 ms look-ahead,
// so the output never exceeds 0 dBFS. Stereo interleaved, in place. The audio comes out latencyFrames() late.
class HardKneeCompressor {
public:
    HardKneeCompressor();
    void configure(double thresholdDb, double ratio, double attackMs, double releaseMs, int rate = 44100, double lookaheadMs = 2.0);
    void reset();
    void process(float* stereo, int frames);
    int latencyFrames() const { return 2 * look_; }

private:
    // A delay of look_ frames plus the sliding minimum / boxcar of the limiter.
    struct Delay {
        std::vector<float> buf;       // interleaved ring
        int pos = 0;
    };
    double thr_ = 0.7079, ratio_ = 10, attack_ = 0, release_ = 0;
    int look_ = 88;
    double gain_ = 1;                 // compressor gain, smoothed
    Delay compDelay_, limDelay_;
    std::deque<std::pair<long long, double>> minq_;     // sliding window minimum of the limiter targets
    std::vector<double> boxBuf_;
    double boxSum_ = 0;
    int boxPos_ = 0;
    long long n_ = 0;
};

} // namespace trakcrate

#endif
