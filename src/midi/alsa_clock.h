#ifndef ALSA_CLOCK_H
#define ALSA_CLOCK_H

#include <QString>
#include <QStringList>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include "seq_phase_model.h"

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
    // The sequencer's beat position between ticks: the queue's real time and the tempo history (SeqPhaseModel) instead of the tick counter,
    // so it is exact to microseconds, not 5 ms steps. -1 when not running. *steadyNs, when given, gets the std::chrono::steady_clock time (ns)
    // the position belongs to (read right next to the queue's clock: both are CLOCK_MONOTONIC).
    double positionBeatsExact(int64_t* steadyNs = nullptr) const;
    // The same offset as beatOffsetMs() but from the tick counter (5 ms steps): kept to compare the two in the telemetry.
    bool beatOffsetTickMs(double* ms) const;
    int modelResyncs() const { return resyncs_; }   // times the model had to be reset from the tick counter (a missed anchor)
    double effectiveBpm() const;                 // the tempo the queue really has: ALSA keeps whole microseconds per beat
    double queueBpm() const;                      // read back from ALSA (what the devices actually follow)

    // Nudge like adj: the tempo is changed for exactly one beat, then restored. nudgeMillis(+n) makes that beat n ms
    // longer (the sequencer ends up n ms later), nudgeMillis(-n) shorter. nudgeBpm(m) runs one beat at bpm + 0.1 * m.
    // Only meaningful while running; a setBpm() cancels a nudge in progress.
    void nudgeMillis(int ms);
    // A larger move than a nudge, exact: the next beat is stretched (or squeezed) so that the sequencer ends up `ms` later
    // (earlier when negative). nudgeMillis(n) really moves n*beat/(beat+n); this solves for the tempo that gives `ms`.
    // False when not running or beyond 0.75 of a beat. A setBpm() cancels it, as it does a nudge.
    bool phaseShiftMs(int ms);
    void nudgeBpm(int multiplier);

    // Where "now" falls relative to the nearest beat of the sequencer, in ms: positive when the last beat was
    // that long ago (we are ahead of something that happens now), negative when the next beat is that far off.
    // False when not running. Callable from any thread.
    bool beatOffsetMs(double* ms) const;

    // Bar position as a label on top of the steady clock: beats since the last start() plus a whole-beat offset (0..3), so a beat jump
    // moves the bar position without touching the clock's timing. -1 / 0 when not running. start(), stop() and restart() reset the offset.
    double barBeats() const;
    int barPos() const;                           // 1..4, 0 when not running
    void shiftBeats(int beats);                   // the bar position moves by whole beats, now

    void start();                                 // MIDI start + clock from tick 0
    // MIDI STOP, the queue back to tick 0, MIDI START, on the running clock (the drum machines start their pattern again). Not running: nothing.
    void restart();
    void stop();                                  // MIDI stop, clock ends
    bool running() const { return running_; }

    // Names of the ALSA clients subscribed to the clock port (the drum machines / synths following us).
    QStringList subscriberNames() const;

    // A second output port, "DJ Tools MIDI", for the DJ Tools's MidiMatic notes: notes are scheduled on the same queue as the clock,
    // by absolute tick (96 per beat, beat 0 = the last start()), so they are as steady as the clock. kind: 0 note on,
    // 1 note off, 2 all notes off (CC 123); channel 0..15. To the ALSA address destClient:destPort, or to whoever is subscribed to
    // the port when destClient < 0. False when closed or the event could not be queued.
    bool sendDjtoolzMidi(int kind, int channel, int note, int velocity, qint64 tick, int destClient, int destPort);
    int djtoolzPortId() const { return djtoolzPort_; }

    int clientId() const { return client_; }
    int portId() const { return port_; }

private:
    void loop();
    bool applyTempo(double bpmValue, double extraMicros = 0.0);   // caller holds mutex_
    int64_t queueRealNs() const;                                  // caller holds mutex_; -1 when unreadable
    void anchorModel();                                           // caller holds mutex_; the queue has just been (re)started at tick 0
    void stopLocked();

    snd_seq_t* seq_ = nullptr;
    int client_ = -1;
    int port_ = -1;
    int djtoolzPort_ = -1;
    int queue_ = -1;
    std::atomic<double> bpm_{ 120.0 };
    std::atomic<bool> running_{ false };
    std::atomic<int> beatOffset_{ 0 };
    std::atomic<bool> resetNext_{ false };        // restart(): the clock thread starts queueing pulses from tick 0 again
    std::atomic<bool> quit_{ false };
    mutable std::mutex mutex_;                    // the sequencer handle is shared by the GUI and the clock thread
    std::chrono::steady_clock::time_point nudgeEnd_{};   // when to restore the tempo; epoch = no nudge
    std::thread thread_;
    mutable SeqPhaseModel model_;                 // guarded by mutex_
    double lastMicros_ = 500000.0;                // the microseconds per beat last given to the queue
    mutable std::atomic<int> resyncs_{ 0 };
};

} // namespace vdj

#endif // ALSA_CLOCK_H
