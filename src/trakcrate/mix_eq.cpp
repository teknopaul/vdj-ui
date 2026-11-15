#include "mix_eq.h"

#include <cmath>

namespace trakcrate {

namespace {

// Butterworth sections of order n (n even, or 1): their Q values.
std::vector<double> butterQ(int n)
{
    std::vector<double> q;
    for (int k = 0; k < n / 2; ++k) q.push_back(1.0 / (2.0 * std::cos(M_PI * (2.0 * k + 1.0) / (2.0 * n))));
    return q;
}

}

void CrossoverEq::Split::design(double fc, int lrOrder, int rate)
{
    lp.clear();
    hp.clear();
    const int bw = lrOrder / 2;                      // Butterworth order, applied twice
    const double w0 = 2 * M_PI * fc / rate, c = std::cos(w0), s = std::sin(w0);
    for (int pass = 0; pass < 2; ++pass) {
        if (bw == 1) {
            const double K = std::tan(w0 / 2);
            Biquad l, h;
            l.b0 = l.b1 = K / (1 + K);
            h.b0 = 1 / (1 + K);
            h.b1 = -h.b0;
            l.a1 = h.a1 = (K - 1) / (1 + K);
            lp.push_back(l);
            hp.push_back(h);
        } else {
            for (double q : butterQ(bw)) {
                const double alpha = s / (2 * q), a0 = 1 + alpha;
                Biquad l, h;
                l.b0 = l.b2 = (1 - c) / 2 / a0;
                l.b1 = (1 - c) / a0;
                h.b0 = h.b2 = (1 + c) / 2 / a0;
                h.b1 = -(1 + c) / a0;
                l.a1 = h.a1 = -2 * c / a0;
                l.a2 = h.a2 = (1 - alpha) / a0;
                lp.push_back(l);
                hp.push_back(h);
            }
        }
    }
    hpSign = (lrOrder % 4 == 2) ? -1 : 1;
}

void CrossoverEq::Split::reset()
{
    for (Biquad& b : lp) b.z1[0] = b.z1[1] = b.z2[0] = b.z2[1] = 0;
    for (Biquad& b : hp) b.z1[0] = b.z1[1] = b.z2[0] = b.z2[1] = 0;
}

void CrossoverEq::configure(double bassHz, double highHz, int slopeDb, int rate)
{
    const int order = slopeDb >= 48 ? 8 : slopeDb >= 24 ? 4 : 2;
    hiSplit_.design(highHz, order, rate);
    loSplit_.design(bassHz, order, rate);
    apHi_.design(bassHz, order, rate);
}

void CrossoverEq::reset()
{
    hiSplit_.reset();
    loSplit_.reset();
    apHi_.reset();
}

void CrossoverEq::process(const float* in, float* out, int frames, const float* bassGain, const float* highGain)
{
    for (int i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            const double x = in[i * 2 + ch];
            const double rest = hiSplit_.low(ch, x), hi = hiSplit_.high(ch, x);
            const double lo = loSplit_.low(ch, rest), mid = loSplit_.high(ch, rest);
            const double hiAp = apHi_.low(ch, hi) + apHi_.high(ch, hi);
            out[i * 2 + ch] = float(lo * bassGain[i] + mid + hiAp * highGain[i]);
        }
    }
}

} // namespace trakcrate
