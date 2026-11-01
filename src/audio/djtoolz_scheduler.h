#ifndef DJTOOLZ_SCHEDULER_H
#define DJTOOLZ_SCHEDULER_H

#include <QObject>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include "beat_source.h"
#include "sample_cache.h"
#include "sample_stretch.h"
#include "djtoolz_tool.h"
#include "voice_mixer.h"
#include "midi/midi_out.h"

namespace vdj {

// Turns the tools' beats into Triggers a little ahead of time: every tick (~5 ms) each active tool is asked for the hits
// in [where it stopped last time, now + its lookahead), they are moved by the tool's offset (and, with reverse sync, by
// the sample's length) and handed to the sink (DjtoolzEngine::post). The tool's current sample is read when the hit is
// scheduled, so changing the sample takes effect at the next hit.
class DjtoolzScheduler {
public:
    using Sink = std::function<void(const Trigger&)>;
    using StopSink = std::function<void(int tag, qint64 steadyNs)>;     // cut the voices of a tool at that time
    static constexpr double kLookaheadMs = 100.0;

    DjtoolzScheduler(BeatSource* source, SampleCache* cache, Sink sink);
    ~DjtoolzScheduler();
    DjtoolzScheduler(const DjtoolzScheduler&) = delete;
    DjtoolzScheduler& operator=(const DjtoolzScheduler&) = delete;

    using MidiSink = std::function<void(const MidiNoteEvent&)>;
    void setMidiSink(MidiSink s) { midiSink_ = std::move(s); }
    // Sounds that start "now" (a one shot that is not quantised, a reverse-sync hit that is already late) cannot be heard before the
    // output latency has passed: they are put this far ahead. 0 in tests, the engine's latency + a margin in the program.
    void setImmediateLeadMs(double ms) { immediateLeadNs_ = qint64(ms * 1e6); }
    // Audio stack latency (ms, + = the sound comes out late): every hit is queued that much earlier so it is heard on the beat; a negative
    // value delays them. DJ Tools sound only: the MIDI clock and the difflock offset are not touched.
    void setLatencyMs(double ms) { latencyNs_ = qint64(ms * 1e6); }
    double latencyMs() const { return double(latencyNs_.load()) / 1e6; }
    void setStopSink(StopSink s) { stopSink_ = std::move(s); }
    StretchCache& stretchCache() { return stretch_; }

    void addTool(std::shared_ptr<DjtoolzTool> m);
    void removeTool(int id);
    std::vector<std::shared_ptr<DjtoolzTool>> tools() const;
    std::shared_ptr<DjtoolzTool> tool(int id) const;

    void start();                                    // runs tick() every ~5 ms on a thread of its own
    void stop();
    bool running() const { return running_.load(); }

    // One scheduling pass at the given steady-clock time. start() calls it from the thread; tests call it directly.
    void tick(qint64 nowNs);

    double currentBeat() const;                      // -1 when the source is not running
    void fireOneBar(int toolId);                   // the one bar button
    // Play a OneShot tool now: on the next beat when it is quantised and the sequencer runs, stretched to the sequencer's BPM when
    // enabled and the sample's BPM is known (until the stretched copy is ready it plays as recorded). nowNs 0 = the clock.
    // False when the sample is not decoded yet (the decode is started: press again).
    bool fireOneShot(int toolId, qint64 nowNs = 0);
    // Wait for the decode threads (tests): true when nothing is loading any more.
    bool waitIdle(int timeoutMs);

private:
    struct State {
        double scheduledTo = 0.0;
        bool active = false;
        std::shared_ptr<const Sample> lastStretched;     // acid loops: the previous copy, played until the new tempo's is ready
    };
    void loaderLoop();
    void requestLoad(const QString& path);
    double windowStart(const State& st, double nowBeat, double lookaheadBeats, DjtoolzTool* m);
    void tickDrums(const std::shared_ptr<DjtoolzTool>& m, const ToolSettings& s, double nowBeat, double bpm);
    void tickMidi(const std::shared_ptr<DjtoolzTool>& m, const ToolSettings& s, bool oneBar, double nowBeat, double bpm);
    void requestStretch(const QString& path, int permille, qint64 exactFrames);
    std::shared_ptr<const Sample> acidSample(const ToolSettings& s, const std::shared_ptr<const Sample>& base, double bpm, State& st);
    void loopBody();

    BeatSource* source_;
    SampleCache* cache_;
    Sink sink_;
    StopSink stopSink_;
    qint64 immediateLeadNs_ = 0;
    std::atomic<qint64> latencyNs_{ 0 };
    qint64 steadyNsAtBeat(double beat) const;       // the source's time for the beat, less the audio latency
    double lookaheadMs() const;                     // the window is wider by a positive latency
    MidiSink midiSink_;
    StretchCache stretch_;

    mutable std::mutex mutex_;                       // tools_, states_
    std::vector<std::shared_ptr<DjtoolzTool>> tools_;
    std::map<int, State> states_;

    std::mutex loadMutex_;
    std::condition_variable loadWake_;
    std::set<QString> loadQueue_;
    std::set<QString> loading_;
    std::set<QString> failed_;
    struct StretchJob {
        int permille;
        qint64 exactFrames;
    };
    std::map<QString, StretchJob> stretchQueue_;     // the latest request per sample wins
    std::set<QString> stretching_;

    std::atomic<bool> running_{ false };
    std::atomic<bool> quit_{ false };
    std::thread thread_;
    std::thread loader_;
};

} // namespace vdj

#endif // DJTOOLZ_SCHEDULER_H
