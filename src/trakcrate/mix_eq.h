#ifndef TRAKCRATE_MIX_EQ_H
#define TRAKCRATE_MIX_EQ_H

#include <vector>

namespace trakcrate {

// Three band split for a studio mix track: low | mid | high with Linkwitz-Riley crossovers (order 2, 4 or 8 = 12, 24 or 48 dB/octave), cascaded
// biquads in double precision. With unity gains the three bands sum back to the input (the high band goes through an all-pass at the lower
// crossover so that the phases of the bands agree). Stereo interleaved; one instance per track.
class CrossoverEq {
public:
    CrossoverEq() { configure(120, 2500, 24, 44100); }
    void configure(double bassHz, double highHz, int slopeDb, int rate);
    void reset();
    // in and out may be the same buffer. The gains are per frame (low band * bassGain[i], high band * highGain[i]).
    void process(const float* in, float* out, int frames, const float* bassGain, const float* highGain);

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[2] = {0, 0}, z2[2] = {0, 0};
        double run(int ch, double x)
        {
            const double y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }
    };
    // One crossover: lowpass and highpass cascades; the high output is inverted for orders 2 and 6 so that low + high is all-pass.
    struct Split {
        std::vector<Biquad> lp, hp;
        double hpSign = 1;
        void design(double fc, int order, int rate);
        double low(int ch, double x) { for (Biquad& b : lp) x = b.run(ch, x); return x; }
        double high(int ch, double x) { for (Biquad& b : hp) x = b.run(ch, x); return x * hpSign; }
        void reset();
    };
    Split hiSplit_, loSplit_, apHi_;
};

} // namespace trakcrate

#endif
