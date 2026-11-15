#include "stretch_probe.h"

#include <algorithm>
#include <cmath>

namespace trakcrate {

namespace {

constexpr double kTone = 110;

double rms(const DecodedPcm& p)
{
    double s = 0;
    for (float v : p.stereo) s += double(v) * v;
    return p.stereo.empty() ? 0 : std::sqrt(s / double(p.stereo.size()));
}

// Magnitude of the tone at `hz` over the middle part of the signal (left channel).
double goertzel(const DecodedPcm& p, double hz, size_t from, size_t n)
{
    const double w = 2 * M_PI * hz / p.sampleRate, c = 2 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n));       // clicks leak into a plain rectangular window
        const double s0 = double(p.stereo[(from + i) * 2]) * hann + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2);
}

double tonePitch(const DecodedPcm& p)
{
    const size_t n = std::min<size_t>(p.frames() / 2, size_t(p.sampleRate) * 4);
    if (n < 4096) return 0;
    const size_t from = (p.frames() - n) / 2;
    double best = 0, bestHz = 0;
    for (double hz = 50; hz <= 240; hz += 0.5) {
        const double m = goertzel(p, hz, from, n);
        if (m > best) {
            best = m;
            bestHz = hz;
        }
    }
    // refine around the peak
    for (double hz = bestHz - 0.5; hz <= bestHz + 0.5; hz += 0.05) {
        const double m = goertzel(p, hz, from, n);
        if (m > best) {
            best = m;
            bestHz = hz;
        }
    }
    return bestHz;
}

// Position of the loudest high-passed sample within +-window frames of `center` (smoothed over 1 ms).
double clickPeak(const DecodedPcm& p, double center, double window)
{
    const long long n = (long long)p.frames();
    const long long a = std::max<long long>(1, (long long)(center - window)), b = std::min<long long>(n - 1, (long long)(center + window));
    const int sm = std::max(1, p.sampleRate / 1000);
    double best = -1, pos = center;
    double acc = 0;
    std::vector<double> win(size_t(sm), 0.0);
    for (long long i = a; i < b; ++i) {
        const double d = std::fabs(double(p.stereo[size_t(i) * 2]) - double(p.stereo[size_t(i - 1) * 2]));
        acc += d - win[size_t(i % sm)];
        win[size_t(i % sm)] = d;
        if (acc > best) {
            best = acc;
            pos = double(i) - sm / 2.0;
        }
    }
    return pos;
}

} // namespace

DecodedPcm probeMaterial(double seconds, double bpm)
{
    DecodedPcm p;
    const size_t n = size_t(seconds * p.sampleRate);
    p.stereo.assign(n * 2, 0.0f);
    const double beat = 60.0 / bpm * p.sampleRate;
    for (size_t i = 0; i < n; ++i) {
        const float v = float(0.3 * std::sin(2 * M_PI * kTone * double(i) / p.sampleRate));
        p.stereo[i * 2] = p.stereo[i * 2 + 1] = v;
    }
    // a 3 ms decaying 2.5 kHz burst on every beat
    const int len = p.sampleRate * 3 / 1000;
    for (double b = beat; b + len < double(n); b += beat) {
        const size_t s = size_t(b);
        for (int k = 0; k < len; ++k) {
            const float v = float(0.6 * std::exp(-double(k) / (len / 3.0)) * std::sin(2 * M_PI * 2500 * k / p.sampleRate));
            p.stereo[(s + size_t(k)) * 2] += v;
            p.stereo[(s + size_t(k)) * 2 + 1] += v;
        }
    }
    return p;
}

StretchQuality probeEngine(StretchEngine& engine, const StretchOptions& opts, double ratio)
{
    StretchQuality q;
    const double bpm = 120;
    const DecodedPcm in = probeMaterial(12, bpm);
    DecodedPcm out;
    if (!engine.stretch(in, ratio, opts, &out, nullptr, nullptr, &q.error)) return q;
    q.ok = true;
    const double expected = double(in.frames()) / ratio;
    q.durationError = (double(out.frames()) - expected) / expected;
    q.rmsRatio = rms(in) > 0 ? rms(out) / rms(in) : 0;
    // The pitch is measured on the tone alone: the clicks bias the estimate by up to a percent.
    DecodedPcm tone;
    tone.stereo.assign(size_t(8 * tone.sampleRate) * 2, 0.0f);
    for (size_t i = 0; i < tone.frames(); ++i) tone.stereo[i * 2] = tone.stereo[i * 2 + 1] = float(0.3 * std::sin(2 * M_PI * kTone * double(i) / tone.sampleRate));
    DecodedPcm toneOut;
    std::string terr;
    const double f = engine.stretch(tone, ratio, opts, &toneOut, nullptr, nullptr, &terr) ? tonePitch(toneOut) : 0;
    q.pitchRatio = f > 0 ? f / kTone : 0;

    const double beat = 60.0 / bpm * in.sampleRate;
    double sum = 0;
    int count = 0;
    for (int i = 3; i * beat < double(in.frames()) - 3 * beat; ++i) {
        const double inPeak = clickPeak(in, i * beat + 40, 200);
        const double expectOut = inPeak / ratio;
        const double got = clickPeak(out, expectOut, 0.06 * out.sampleRate);
        sum += std::fabs(got - expectOut) * 1000.0 / out.sampleRate;
        ++count;
    }
    q.onsetErrorMs = count ? sum / count : 0;
    return q;
}

} // namespace trakcrate
