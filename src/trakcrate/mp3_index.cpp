#include "mp3_index.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace trakcrate {

namespace {

constexpr uint32_t kRekordboxBackoffFrames = 8;     // rekordbox seeks 8 frames (about 0.2 s) before the frame at the time asked for

const int kBitrate1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};      // MPEG 1 layer III
const int kBitrate2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};          // MPEG 2 / 2.5 layer III
const uint32_t kRate1[3] = {44100, 48000, 32000};

bool readAll(const fs::path& p, std::string* out)
{
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    char buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->append(buf, n);
    std::fclose(f);
    return true;
}

} // namespace

uint32_t Mp3Index::offsetAtMs(uint32_t ms) const
{
    if (!ok || frameStart.empty() || sampleRate == 0) return 0;
    const uint64_t frame = uint64_t(ms) * sampleRate / (1000ull * samplesPerFrame);
    const uint64_t at = frame > kRekordboxBackoffFrames ? frame - kRekordboxBackoffFrames : 0;
    return frameStart[std::min<uint64_t>(at, frameStart.size() - 1)];
}

std::vector<uint32_t> Mp3Index::pvbr() const
{
    std::vector<uint32_t> w(401, 0);
    if (!ok || frameStart.empty()) return w;
    const uint64_t n = frameStart.size();
    for (uint64_t i = 0; i < 400; ++i) {
        const uint64_t frame = (i + 1) * n / 400;
        const uint64_t at = frame > kRekordboxBackoffFrames ? frame - kRekordboxBackoffFrames : 0;
        w[i] = frameStart[std::min<uint64_t>(at, n - 1)];
    }
    w[400] = uint32_t(n * samplesPerFrame);
    return w;
}

Mp3Index indexMp3(const fs::path& file)
{
    Mp3Index ix;
    std::string d;
    if (!readAll(file, &d)) return ix;
    const auto* b = reinterpret_cast<const unsigned char*>(d.data());
    size_t o = 0;
    if (d.size() > 10 && d.compare(0, 3, "ID3") == 0) o = 10 + ((size_t(b[6] & 0x7f) << 21) | (size_t(b[7] & 0x7f) << 14) | (size_t(b[8] & 0x7f) << 7) | (b[9] & 0x7f));
    size_t first = std::string::npos;
    size_t pos = o;
    while (pos + 4 <= d.size()) {
        if (b[pos] != 0xFF || (b[pos + 1] & 0xE0) != 0xE0 || ((b[pos + 1] >> 1) & 3) != 1 || (b[pos + 2] >> 4) == 0xF || (b[pos + 2] >> 4) == 0 || ((b[pos + 2] >> 2) & 3) == 3) {
            ++pos;
            continue;
        }
        const int ver = (b[pos + 1] >> 3) & 3;                  // 3 = MPEG 1, 2 = MPEG 2, 0 = MPEG 2.5
        if (ver == 1) { ++pos; continue; }
        const bool mpeg1 = ver == 3;
        const int kbps = (mpeg1 ? kBitrate1 : kBitrate2)[b[pos + 2] >> 4];
        const uint32_t rate = kRate1[(b[pos + 2] >> 2) & 3] / (mpeg1 ? 1 : (ver == 2 ? 2 : 4));
        const uint32_t spf = mpeg1 ? 1152 : 576;
        const size_t len = size_t(spf / 8 * uint32_t(kbps) * 1000 / rate) + ((b[pos + 2] >> 1) & 1);
        if (len < 24 || pos + len > d.size() + 1) { ++pos; continue; }
        if (first == std::string::npos) {
            first = pos;
            ix.sampleRate = rate;
            ix.samplesPerFrame = spf;
            const size_t side = mpeg1 ? (((b[pos + 3] >> 6) & 3) == 3 ? 17 : 32) : (((b[pos + 3] >> 6) & 3) == 3 ? 9 : 17);
            const size_t x = pos + 4 + side;
            ix.vbr = (x + 4 <= d.size() && (d.compare(x, 4, "Xing") == 0)) || (pos + 36 <= d.size() && d.compare(pos + 36, 4, "VBRI") == 0);
        }
        ix.frameStart.push_back(uint32_t(pos - first));
        pos += len;
    }
    ix.ok = first != std::string::npos;
    return ix;
}

} // namespace trakcrate
