#ifndef TRAKCRATE_TEST_WAV_H
#define TRAKCRATE_TEST_WAV_H

#include "trakcrate/audio_decode.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>

// Helpers for the trakcrate analysis tests: synthetic audio and a minimal 16 bit PCM WAV writer.
namespace tcwav {

inline void put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xFF); }
inline void put16(std::string& s, uint16_t v) { s += char(v & 0xFF); s += char(v >> 8); }

// Writes mono 16 bit PCM at `rate`.
inline void writeWavMono(const trakcrate::fs::path& p, const std::vector<float>& mono, int rate)
{
    std::string data;
    for (float v : mono) put16(data, uint16_t(int16_t(std::lround(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f))));
    std::string fmt;
    put16(fmt, 1); put16(fmt, 1); put32(fmt, uint32_t(rate)); put32(fmt, uint32_t(rate * 2)); put16(fmt, 2); put16(fmt, 16);
    std::string out = "RIFF";
    put32(out, uint32_t(4 + 8 + fmt.size() + 8 + data.size()));
    out += "WAVEfmt ";
    put32(out, uint32_t(fmt.size()));
    out += fmt + "data";
    put32(out, uint32_t(data.size()));
    out += data;
    trakcrate::fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << out;
}

inline trakcrate::DecodedPcm silence(double seconds)
{
    trakcrate::DecodedPcm p;
    p.stereo.assign(size_t(seconds * p.sampleRate) * 2, 0.0f);
    return p;
}

// A short 1 kHz burst (2 ms, decaying) at every beat: first at t0, then every 60/bpm seconds.
inline trakcrate::DecodedPcm clicks(double bpm, double t0, double seconds, float amp = 0.8f)
{
    trakcrate::DecodedPcm p = silence(seconds);
    const double period = 60.0 / bpm;
    for (double t = t0; t < seconds - 0.01; t += period) {
        size_t s = size_t(t * p.sampleRate);
        for (int i = 0; i < 88; ++i) {
            float v = amp * float(std::sin(2 * M_PI * 1000.0 * i / p.sampleRate) * std::exp(-i / 25.0));
            if ((s + size_t(i)) * 2 + 1 < p.stereo.size()) { p.stereo[(s + size_t(i)) * 2] += v; p.stereo[(s + size_t(i)) * 2 + 1] += v; }
        }
    }
    return p;
}

// A kick (decaying 60 Hz sine) on every beat and a noise hat on every off-beat: closer to a real drum loop.
inline trakcrate::DecodedPcm drumLoop(double bpm, double t0, double seconds)
{
    trakcrate::DecodedPcm p = silence(seconds);
    const double period = 60.0 / bpm;
    uint32_t rng = 12345;
    auto add = [&](double t, bool kick) {
        size_t s = size_t(t * p.sampleRate);
        int len = kick ? 8000 : 2000;
        for (int i = 0; i < len; ++i) {
            float v;
            if (kick) v = 0.9f * float(std::sin(2 * M_PI * 60.0 * i / p.sampleRate) * std::exp(-i / 2500.0));
            else { rng = rng * 1664525u + 1013904223u; v = 0.25f * (float(rng >> 8) / 8388608.0f - 1.0f) * float(std::exp(-i / 400.0)); }
            size_t k = (s + size_t(i)) * 2;
            if (k + 1 < p.stereo.size()) { p.stereo[k] += v; p.stereo[k + 1] += v; }
        }
    };
    for (double t = t0; t < seconds - 0.3; t += period) { add(t, true); add(t + period / 2, false); }
    return p;
}

inline trakcrate::DecodedPcm sine(double freq, float amp, double seconds)
{
    trakcrate::DecodedPcm p = silence(seconds);
    for (size_t i = 0; i < p.frames(); ++i) {
        float v = amp * float(std::sin(2 * M_PI * freq * double(i) / p.sampleRate));
        p.stereo[2 * i] = p.stereo[2 * i + 1] = v;
    }
    return p;
}

inline trakcrate::DecodedPcm noise(double seconds, float amp = 0.5f)
{
    trakcrate::DecodedPcm p = silence(seconds);
    uint32_t rng = 987654321u;
    for (size_t i = 0; i < p.stereo.size(); ++i) {
        rng = rng * 1664525u + 1013904223u;
        p.stereo[i] = amp * (float(rng >> 8) / 8388608.0f - 1.0f);
    }
    return p;
}

inline std::vector<float> mono(const trakcrate::DecodedPcm& p)
{
    std::vector<float> m(p.frames());
    for (size_t i = 0; i < m.size(); ++i) m[i] = p.stereo[2 * i];
    return m;
}

} // namespace tcwav

#endif
