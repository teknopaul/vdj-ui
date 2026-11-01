#include "analysis_wave.h"

#include <algorithm>
#include <cmath>

namespace trakcrate {

int heightFromPeak(double peak)
{
    if (peak <= 0) return 0;
    return std::max(0, std::min(31, int(std::lround(31.0 * std::min(1.0, peak)))));
}

int bandLevel(double peak)
{
    return std::max(0, std::min(7, int(std::lround(7.0 * std::min(1.0, peak * 2.0)))));
}

Waveform analyseWaveform(const DecodedPcm& pcm)
{
    Waveform w;
    const int colLen = pcm.sampleRate / Waveform::kColumnsPerSecond;          // 294 samples at 44.1 kHz
    const size_t frames = pcm.frames();
    const size_t cols = (frames + size_t(colLen) - 1) / size_t(colLen);
    w.pwv3.assign(cols, 0);
    w.pwv5.assign(cols, 0);
    std::vector<uint8_t> height(cols, 0), white(cols, 0);

    const double aLow = 1.0 - std::exp(-2.0 * M_PI * 250.0 / pcm.sampleRate);
    const double aMid = 1.0 - std::exp(-2.0 * M_PI * 2500.0 / pcm.sampleRate);
    double l1 = 0, l2 = 0, m1 = 0, m2 = 0;
    for (size_t c = 0; c < cols; ++c) {
        double peak = 0, pLow = 0, pMid = 0, pHigh = 0;
        const size_t end = std::min(frames, (c + 1) * size_t(colLen));
        for (size_t f = c * size_t(colLen); f < end; ++f) {
            double L = pcm.stereo[2 * f], R = pcm.stereo[2 * f + 1];
            peak = std::max(peak, std::max(std::fabs(L), std::fabs(R)));
            double x = 0.5 * (L + R);
            l1 += aLow * (x - l1);  l2 += aLow * (l1 - l2);               // below ~250 Hz
            m1 += aMid * (x - m1);  m2 += aMid * (m1 - m2);               // below ~2.5 kHz
            pLow = std::max(pLow, std::fabs(l2));
            pMid = std::max(pMid, std::fabs(m2 - l2));
            pHigh = std::max(pHigh, std::fabs(x - m2));
        }
        height[c] = uint8_t(heightFromPeak(peak));
        double sum = pLow + pMid + pHigh;
        white[c] = uint8_t(sum > 1e-6 ? std::max(0, std::min(7, int(std::lround(7.0 * pHigh / sum)))) : 0);
        w.pwv3[c] = uint8_t((white[c] << 5) | height[c]);
        w.pwv5[c] = uint16_t((bandLevel(pLow) << 13) | (bandLevel(pMid) << 10) | (bandLevel(pHigh) << 7) | (height[c] << 2));
    }

    auto slice = [&](int count, bool maxWhite, std::vector<uint8_t>* hOut, std::vector<uint8_t>* wOut) {
        hOut->assign(size_t(count), 0);
        wOut->assign(size_t(count), 0);
        if (cols == 0) return;
        for (int i = 0; i < count; ++i) {
            size_t a = size_t(i) * cols / size_t(count), b = std::max(a + 1, size_t(i + 1) * cols / size_t(count));
            b = std::min(b, cols);
            if (a >= cols) a = cols - 1;
            int hMax = 0, wSum = 0;
            for (size_t k = a; k < b; ++k) { hMax = std::max<int>(hMax, height[k]); wSum += white[k]; }
            (*hOut)[size_t(i)] = uint8_t(hMax);
            (*wOut)[size_t(i)] = uint8_t(maxWhite ? wSum / int(b - a) : 0);
        }
    };
    std::vector<uint8_t> h400, w400, h100, w100;
    slice(Waveform::kPreviewColumns, true, &h400, &w400);
    slice(Waveform::kTinyColumns, false, &h100, &w100);
    w.pwav.resize(h400.size());
    for (size_t i = 0; i < h400.size(); ++i) w.pwav[i] = uint8_t((w400[i] << 5) | h400[i]);
    w.pwv2.resize(h100.size());
    for (size_t i = 0; i < h100.size(); ++i) w.pwv2[i] = uint8_t(h100[i] >> 1);
    return w;
}

// ---- file --------------------------------------------------------------------------------------------------------------------------

static void put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xFF); }
static uint32_t get32(const std::string& s, size_t at) { return uint32_t(uint8_t(s[at])) | uint32_t(uint8_t(s[at + 1])) << 8 | uint32_t(uint8_t(s[at + 2])) << 16 | uint32_t(uint8_t(s[at + 3])) << 24; }

static void section(std::string& out, const char* tag, const std::string& bytes)
{
    out.append(tag, 4);
    put32(out, uint32_t(bytes.size()));
    out += bytes;
}

std::string serialiseWaveform(const Waveform& w)
{
    std::string out = "TCWV";
    put32(out, 1);
    put32(out, uint32_t(w.columns()));
    section(out, "PWAV", std::string(w.pwav.begin(), w.pwav.end()));
    section(out, "PWV2", std::string(w.pwv2.begin(), w.pwv2.end()));
    section(out, "PWV3", std::string(w.pwv3.begin(), w.pwv3.end()));
    std::string v5;
    for (uint16_t e : w.pwv5) { v5 += char(e >> 8); v5 += char(e & 0xFF); }
    section(out, "PWV5", v5);
    return out;
}

bool parseWaveform(const std::string& s, Waveform* out)
{
    if (s.size() < 12 || s.compare(0, 4, "TCWV") != 0 || get32(s, 4) != 1) return false;
    Waveform w;
    size_t at = 12;
    while (at + 8 <= s.size()) {
        std::string tag = s.substr(at, 4);
        uint32_t len = get32(s, at + 4);
        at += 8;
        if (len > s.size() - at) return false;
        std::string body = s.substr(at, len);
        at += len;
        if (tag == "PWAV") w.pwav.assign(body.begin(), body.end());
        else if (tag == "PWV2") w.pwv2.assign(body.begin(), body.end());
        else if (tag == "PWV3") w.pwv3.assign(body.begin(), body.end());
        else if (tag == "PWV5") {
            if (len % 2) return false;
            for (size_t i = 0; i < body.size(); i += 2) w.pwv5.push_back(uint16_t((uint8_t(body[i]) << 8) | uint8_t(body[i + 1])));
        }                                                         // unknown sections are skipped
    }
    if (w.pwv3.size() != get32(s, 8) || w.pwv5.size() < w.pwv3.size()) return false;      // a PWV5 longer than PWV3 is kept as rekordbox wrote it
    *out = w;
    return true;
}

Loudness measureLoudness(const DecodedPcm& pcm)
{
    Loudness l;
    if (pcm.stereo.empty()) return l;
    double peak = 0, sum = 0;
    for (float v : pcm.stereo) {
        peak = std::max(peak, double(std::fabs(v)));
        sum += double(v) * double(v);
    }
    double rms = std::sqrt(sum / double(pcm.stereo.size()));
    l.peakDb = peak > 1e-6 ? 20.0 * std::log10(peak) : -120.0;
    l.rmsDb = rms > 1e-6 ? 20.0 * std::log10(rms) : -120.0;
    return l;
}

} // namespace trakcrate
