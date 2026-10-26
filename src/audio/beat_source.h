#ifndef BEAT_SOURCE_H
#define BEAT_SOURCE_H

#include <QtGlobal>
#include <chrono>

namespace vdj {

class AlsaClock;

// What the DJ Tools keeps time to: a running beat counter that can be mapped to and from std::chrono::steady_clock
// nanoseconds. Beat 0 is the start of the source (the sequencer's MIDI start), beats % 4 give the bar position.
// All methods are callable from any thread.
class BeatSource {
public:
    virtual ~BeatSource() = default;

    virtual bool running() const = 0;
    virtual double bpm() const = 0;                          // beats per minute now; 0 when not running
    virtual double beatsAtSteadyNs(qint64 ns) const = 0;     // -1 when not running
    virtual qint64 steadyNsAtBeat(double beat) const = 0;    // 0 when not running

    static qint64 steadyNowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// The VDJ's own ALSA sequencer. The beat position is read from the queue and extrapolated with the tempo, so the map is
// re-derived on every call (cheap: one snd_seq_get_queue_status). The queue counts whole ticks (96 per beat), so the
// position is quantised to ~5 ms at 120 BPM; the DJ Tools's lookahead absorbs that.
class AlsaClockBeatSource : public BeatSource {
public:
    explicit AlsaClockBeatSource(const AlsaClock* clock) : clock_(clock) {}

    bool running() const override;
    double bpm() const override;
    double beatsAtSteadyNs(qint64 ns) const override;
    qint64 steadyNsAtBeat(double beat) const override;

private:
    const AlsaClock* clock_;
};

// Fixed tempo from a chosen moment, for tests.
class FakeBeatSource : public BeatSource {
public:
    bool isRunning = true;
    double tempo = 120.0;
    qint64 startNs = 0;                                      // steady ns of beat 0

    bool running() const override { return isRunning; }
    double bpm() const override { return isRunning ? tempo : 0.0; }
    double beatsAtSteadyNs(qint64 ns) const override
    {
        return isRunning ? double(ns - startNs) * tempo / 60e9 : -1.0;
    }
    qint64 steadyNsAtBeat(double beat) const override
    {
        return isRunning ? startNs + qint64(beat * 60e9 / tempo) : 0;
    }
};

} // namespace vdj

#endif // BEAT_SOURCE_H
