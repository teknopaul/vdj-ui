#include "mix_compressor.h"

#include <algorithm>
#include <cmath>

namespace trakcrate {

HardKneeCompressor::HardKneeCompressor() { configure(-3, 10, 1, 80); }

void HardKneeCompressor::configure(double thresholdDb, double ratio, double attackMs, double releaseMs, int rate, double lookaheadMs)
{
    thr_ = std::pow(10.0, thresholdDb / 20.0);
    ratio_ = std::max(1.0, ratio);
    attack_ = attackMs <= 0 ? 0 : std::exp(-1.0 / (attackMs * 0.001 * rate));
    release_ = releaseMs <= 0 ? 0 : std::exp(-1.0 / (releaseMs * 0.001 * rate));
    look_ = std::max(1, int(lookaheadMs * 0.001 * rate));
    reset();
}

void HardKneeCompressor::reset()
{
    gain_ = 1;
    compDelay_.buf.assign(size_t(look_) * 2, 0.0f);
    compDelay_.pos = 0;
    limDelay_.buf.assign(size_t(look_) * 2, 0.0f);
    limDelay_.pos = 0;
    minq_.clear();
    boxBuf_.assign(size_t(look_) + 1, 1.0);
    boxSum_ = double(look_) + 1;
    boxPos_ = 0;
    n_ = 0;
}

void HardKneeCompressor::process(float* s, int frames)
{
    for (int i = 0; i < frames; ++i) {
        const double l = s[i * 2], r = s[i * 2 + 1];
        // ---- compressor: the gain is computed from the incoming peak and applied to the audio delayed by the look-ahead
        const double peak = std::max(std::fabs(l), std::fabs(r));
        double target = 1;
        if (peak > thr_) target = std::pow(peak / thr_, 1.0 / ratio_ - 1.0);          // gain reduction of (1 - 1/ratio) of the excess in dB
        const double coef = target < gain_ ? attack_ : release_;
        gain_ = target + coef * (gain_ - target);
        float* d = &compDelay_.buf[size_t(compDelay_.pos) * 2];
        const double cl = double(d[0]) * gain_, cr = double(d[1]) * gain_;
        d[0] = float(l);
        d[1] = float(r);
        compDelay_.pos = (compDelay_.pos + 1) % look_;

        // ---- limiter at 0 dBFS: window minimum of the needed gains over the look-ahead, then a boxcar average, applied to the delayed audio
        const double cp = std::max(std::fabs(cl), std::fabs(cr));
        const double need = cp > 1.0 ? 1.0 / cp : 1.0;
        while (!minq_.empty() && minq_.back().second >= need) minq_.pop_back();
        minq_.emplace_back(n_, need);
        while (minq_.front().first <= n_ - look_ - 1) minq_.pop_front();
        const double mn = minq_.front().second;
        boxSum_ += mn - boxBuf_[size_t(boxPos_)];
        boxBuf_[size_t(boxPos_)] = mn;
        boxPos_ = (boxPos_ + 1) % (look_ + 1);
        const double g = boxSum_ / double(look_ + 1);
        float* e = &limDelay_.buf[size_t(limDelay_.pos) * 2];
        const double ol = double(e[0]) * g, or_ = double(e[1]) * g;
        e[0] = float(cl);
        e[1] = float(cr);
        limDelay_.pos = (limDelay_.pos + 1) % look_;
        ++n_;
        s[i * 2] = float(std::max(-1.0, std::min(1.0, ol)));
        s[i * 2 + 1] = float(std::max(-1.0, std::min(1.0, or_)));
    }
}

} // namespace trakcrate
