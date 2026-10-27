#ifndef TRAKCRATE_AUDIO_PROBE_H
#define TRAKCRATE_AUDIO_PROBE_H

#include "fsutil.h"

#include <map>
#include <string>

namespace trakcrate {

// What we learn about an audio file without decoding it (ffmpeg's libavformat). No Qt.
struct AudioInfo {
    std::string container;                       // "mp3", "wav", ... (the demuxer's name)
    double durationSec = 0;                      // 0 when the file does not say
    int sampleRate = 0;
    int channels = 0;
    int bitDepth = 0;                            // 16 for lossy formats
    long bitrate = 0;                            // bits per second
    std::map<std::string, std::string> tags;     // lower case keys: title, artist, album, genre, date, track, comment, ...
    std::string picture;                         // embedded cover bytes, empty when none
    std::string pictureExt;                      // "jpg" or "png" (or empty)
};

// False (with err) when the file cannot be opened or has no audio stream. Never throws, never crashes on garbage.
bool probeAudio(const fs::path& path, AudioInfo* out, std::string* err = nullptr);

} // namespace trakcrate

#endif
