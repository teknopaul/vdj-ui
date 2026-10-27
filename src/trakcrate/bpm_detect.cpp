#include "bpm_detect.h"

#include "audio_decode.h"

#include <algorithm>
#include <cmath>

#ifdef TC_HAVE_AUBIO
#include <aubio/aubio.h>
#endif

namespace trakcrate {

double bpmFromBeats(const std::vector<double>& beats)
{
    if (beats.size() < 4) return 0;
    std::vector<double> d;
    for (size_t i = 1; i < beats.size(); ++i) d.push_back(beats[i] - beats[i - 1]);
    std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
    const double m = d[d.size() / 2];
    return m > 0 ? 60.0 / m : 0;
}

long long firstBeatFromBeats(const std::vector<double>& beats, double bpm)
{
    if (beats.empty() || bpm <= 0) return 0;
    const double period = 60.0 / bpm;
    double sx = 0, sy = 0;
    for (double t : beats) {
        const double a = 2 * M_PI * std::fmod(t, period) / period;
        sx += std::cos(a);
        sy += std::sin(a);
    }
    double phase = std::atan2(sy, sx) / (2 * M_PI) * period;
    if (phase < 0) phase += period;
    return (long long)std::llround(phase * 1e6);
}

#ifdef TC_HAVE_AUBIO

bool bpmDetectAvailable() { return true; }

BpmDetection detectBpm(const fs::path& audioFile, const std::atomic<bool>* cancel)
{
    BpmDetection r;
    DecodedPcm pcm;
    if (!decodeAudio(audioFile, &pcm, &r.error, cancel)) return r;

    const uint_t win = 1024, hop = 512, rate = uint_t(pcm.sampleRate);
    aubio_tempo_t* tempo = new_aubio_tempo("default", win, hop, rate);
    fvec_t* in = new_fvec(hop);
    fvec_t* out = new_fvec(2);
    if (!tempo || !in || !out) {
        r.error = "aubio could not be initialised";
    } else {
        std::vector<float> mono(pcm.frames());
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (pcm.stereo[2 * i] + pcm.stereo[2 * i + 1]);
        for (size_t pos = 0; pos + hop <= mono.size(); pos += hop) {
            if (cancel && cancel->load() && (pos / hop) % 256 == 0) { r.error = "cancelled"; break; }
            for (uint_t i = 0; i < hop; ++i) in->data[i] = mono[pos + i];
            aubio_tempo_do(tempo, in, out);
            if (out->data[0] != 0) r.beatsSeconds.push_back(double(aubio_tempo_get_last_s(tempo)));
        }
        if (r.error.empty()) {
            r.bpm = double(aubio_tempo_get_bpm(tempo));
            if (r.bpm <= 0) r.bpm = bpmFromBeats(r.beatsSeconds);
            r.confidence = double(aubio_tempo_get_confidence(tempo));
            r.firstBeatUs = firstBeatFromBeats(r.beatsSeconds, r.bpm);
            r.ok = true;
        }
    }
    if (tempo) del_aubio_tempo(tempo);
    if (in) del_fvec(in);
    if (out) del_fvec(out);
    aubio_cleanup();
    return r;
}

#else

bool bpmDetectAvailable() { return false; }

BpmDetection detectBpm(const fs::path&, const std::atomic<bool>*)
{
    BpmDetection r;
    r.error = "built without libaubio (install libaubio-dev and rebuild)";
    return r;
}

#endif

} // namespace trakcrate
