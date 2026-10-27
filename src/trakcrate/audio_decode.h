#ifndef TRAKCRATE_AUDIO_DECODE_H
#define TRAKCRATE_AUDIO_DECODE_H

#include "fsutil.h"

#include <atomic>
#include <string>
#include <vector>

namespace trakcrate {

// A whole track decoded to interleaved stereo float at 44100 Hz (-1..1). A mono file is duplicated to both channels.
struct DecodedPcm {
    static constexpr int kSampleRate = 44100;
    int sampleRate = kSampleRate;
    std::vector<float> stereo;                          // L R L R ...
    size_t frames() const { return stereo.size() / 2; }
    double seconds() const { return double(frames()) / sampleRate; }
};

// ffmpeg (libavformat, libavcodec, libswresample), no Qt. cancel may be null; it is checked between packets.
bool decodeAudio(const fs::path& path, DecodedPcm* out, std::string* err = nullptr, const std::atomic<bool>* cancel = nullptr);

} // namespace trakcrate

#endif
