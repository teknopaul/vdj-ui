#ifndef VOICE_MIXER_H
#define VOICE_MIXER_H

#include <QtGlobal>
#include <memory>
#include <mutex>
#include <vector>
#include "sample_cache.h"

namespace vdj {

// Start `sample` at the moment steadyNs (std::chrono::steady_clock, as the sound is heard).
struct Trigger {
    std::shared_ptr<const Sample> sample;
    qint64 steadyNs = 0;
    float gain = 1.0f;
    bool loop = false;
    double ratio = 1.0;          // playback speed (linear interpolation, the pitch follows); 1 = as recorded
    int tag = 0;                 // owner (a module id), for stopTag(); 0 = none
    qint64 startFrame = 0;       // skip this many frames of the sample (reverse sync that arrives late)
    // Choke: the voice ends this many output frames after it starts (0 = never cut). The last fadeFrames before the cut ramp the gain
    // linearly down to 0, so the sound is silent at the cut point and there is no click.
    qint64 cutAfterFrames = 0;
    qint64 fadeFrames = 4410;    // 100 ms
};

// Mixes triggered samples into 16 bit stereo blocks. No audio device here: the engine (or a test) calls render() with the
// time the block will be heard. Thread safe: post()/stop*() from any thread, render() from one thread.
class VoiceMixer {
public:
    static constexpr int kRate = DecodedAudio::kSampleRate;
    static constexpr qint64 kLateToleranceNs = 20000000;     // a trigger up to 20 ms in the past still plays at once

    void post(const Trigger& t);
    void stopAll();                                          // voices and pending triggers
    void stopTag(int tag);
    // Cut the voices of `tag` that are playing at steadyNs (a voice that starts at or after that moment is left alone,
    // so "stop the old loop, start the new one" can share one time).
    void stopTagAt(int tag, qint64 steadyNs);
    void setMasterGain(float g);

    // Fill `frames` stereo frames (interleaved) that will be heard from blockStartNs on.
    void render(qint16* out, int frames, qint64 blockStartNs);

    // Timing self-test: with it on, every voice start records how far the frame it really starts on is from the time it was
    // scheduled for (rendered - scheduled, ns). Normally within one frame (23 us); a trigger that arrived late shows its lateness.
    struct TimingSample {
        int tag;
        qint64 errorNs;
    };
    void setTimingEnabled(bool on);
    std::vector<TimingSample> drainTiming();         // oldest first; at most 256 are kept between calls

    int activeVoices() const;
    int pendingTriggers() const;
    qint64 droppedLate() const;                              // triggers discarded for being more than kLateToleranceNs late

private:
    struct Voice {
        std::shared_ptr<const Sample> sample;
        double pos = 0.0;                                    // frame in the sample
        double ratio = 1.0;
        float gain = 1.0f;
        bool loop = false;
        int tag = 0;
        qint64 cutAfter = 0;                                 // see Trigger::cutAfterFrames
        qint64 fade = 0;
        qint64 age = 0;                                      // output frames since the voice started
        bool finished = false;                               // ended by its cut
    };
    static void mixVoice(Voice& v, int* acc, int fromFrame, int toFrame);   // acc: interleaved, added in 1/32768 units * gain

    mutable std::mutex mutex_;
    struct Stop {
        int tag;
        qint64 ns;
    };
    std::vector<Trigger> pending_;
    std::vector<Stop> stops_;
    std::vector<TimingSample> timing_;
    bool timingEnabled_ = false;
    std::vector<Voice> voices_;
    float master_ = 1.0f;
    qint64 dropped_ = 0;
};

} // namespace vdj

#endif // VOICE_MIXER_H
