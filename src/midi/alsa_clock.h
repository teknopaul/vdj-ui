#ifndef ALSA_CLOCK_H
#define ALSA_CLOCK_H

#include <QString>
#include <QStringList>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

typedef struct _snd_seq snd_seq_t;

namespace vdj {

// The VDJ's ALSA sequencer: a client "vdj-ui" with one subscribable output port ("MIDI clock") that drum machines
// and synths can be connected to (aconnect, or the USB / MIDI dialog). The sequencer queue's tempo is the VDJ's BPM;
// while running it emits MIDI clock (24 pulses per beat) scheduled on the queue, plus MIDI start / stop.
// Same approach as the `adj` tool: events are queued ahead in ticks, so a tempo change takes effect immediately.
class AlsaClock {
public:
    static constexpr int kPpq = 96;               // sequencer ticks per beat
    static constexpr int kClocksPerBeat = 24;     // MIDI spec
    static constexpr double kMinBpm = 20.0;
    static constexpr double kMaxBpm = 300.0;

    AlsaClock();
    ~AlsaClock();
    AlsaClock(const AlsaClock&) = delete;
    AlsaClock& operator=(const AlsaClock&) = delete;

    bool open(const QString& clientName = QStringLiteral("vdj-ui"), QString* error = nullptr);
    void close();
    bool isOpen() const { return seq_ != nullptr; }

    bool setBpm(double bpm);                      // sets the queue tempo; false when closed or out of range
    double bpm() const { return bpm_; }
    // Beats since the last start() as the sequencer queue counts them (fractional); -1 when not running.
    double positionBeats() const;
    double effectiveBpm() const;                 // the tempo the queue really has: ALSA keeps whole microseconds per beat
    double queueBpm() const;                      // read back from ALSA (what the devices actually follow)

    // Nudge like adj: the tempo is changed for exactly one beat, then restored. nudgeMillis(+n) makes that beat n ms
    // longer (the sequencer ends up n ms later), nudgeMillis(-n) shorter. nudgeBpm(m) runs one beat at bpm + 0.1 * m.
    // Only meaningful while running; a setBpm() cancels a nudge in progress.
    void nudgeMillis(int ms);
    void nudgeBpm(int multiplier);

    // Where "now" falls relative to the nearest beat of the sequencer, in ms: positive when the last beat was
    // that long ago (we are ahead of something that happens now), negative when the next beat is that far off.
    // False when not running. Callable from any thread.
    bool beatOffsetMs(double* ms) const;

    void start();                                 // MIDI start + clock from tick 0
    void stop();                                  // MIDI stop, clock ends
    bool running() const { return running_; }

    // Names of the ALSA clients subscribed to the clock port (the drum machines / synths following us).
    QStringList subscriberNames() const;

    // A second output port, "Sampler MIDI", for the sampler's MidiMatic notes: notes are scheduled on the same queue as the clock,
    // by absolute tick (96 per beat, beat 0 = the last start()), so they are as steady as the clock. kind: 0 note on,
    // 1 note off, 2 all notes off (CC 123); channel 0..15. To the ALSA address destClient:destPort, or to whoever is subscribed to
    // the port when destClient < 0. False when closed or the event could not be queued.
    bool sendSamplerMidi(int kind, int channel, int note, int velocity, qint64 tick, int destClient, int destPort);
    int samplerPortId() const { return samplerPort_; }

    int clientId() const { return client_; }
    int portId() const { return port_; }

private:
    void loop();
    bool applyTempo(double bpmValue, double extraMicros = 0.0);   // caller holds mutex_
    void stopLocked();

    snd_seq_t* seq_ = nullptr;
    int client_ = -1;
    int port_ = -1;
    int samplerPort_ = -1;
    int queue_ = -1;
    std::atomic<double> bpm_{ 120.0 };
    std::atomic<bool> running_{ false };
    std::atomic<bool> quit_{ false };
    mutable std::mutex mutex_;                    // the sequencer handle is shared by the GUI and the clock thread
    std::chrono::steady_clock::time_point nudgeEnd_{};   // when to restore the tempo; epoch = no nudge
    std::thread thread_;
};

} // namespace vdj

#endif // ALSA_CLOCK_H
