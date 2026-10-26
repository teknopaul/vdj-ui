#ifndef DJTOOLZ_ENGINE_H
#define DJTOOLZ_ENGINE_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "fx_rack.h"
#include "voice_mixer.h"

namespace vdj {

// Plays the DJ Tools's triggers on a PulseAudio/PipeWire output of its own (the server mixes it with the track preview).
// A worker thread renders 256 frame blocks through a VoiceMixer; the blocking write paces it. The time a block will be
// heard is "now + the stream's latency": kOutputLatencyMs, plus the correction measured from the server.
class DjtoolzEngine : public QObject {
    Q_OBJECT

public:
    static constexpr int kBlockFrames = 256;                 // ~5.8 ms
    static constexpr double kOutputLatencyMs = 40.0;         // pa_buffer_attr tlength; tune by ear with setLatencyMs

    explicit DjtoolzEngine(QObject* parent = nullptr);
    ~DjtoolzEngine() override;

    void start();                                            // opens the output and starts rendering silence
    void stop();                                             // closes the output, drops voices and pending triggers
    bool running() const { return running_.load(); }

    void post(const Trigger& t) { mixer_.post(t); }
    void stopAll() { mixer_.stopAll(); }
    void stopTag(int tag) { mixer_.stopTag(tag); }
    void stopTagAt(int tag, qint64 steadyNs) { mixer_.stopTagAt(tag, steadyNs); }
    void setMasterGain(float g) { mixer_.setMasterGain(g); }

    void setDevice(const QString& sinkName);                 // empty = default; takes effect immediately
    QString device() const;
    void setLatencyMs(double ms);                            // also re-opens the output so the buffer size follows
    double latencyMs() const { return latencyMs_.load(); }

    // The effects applied to everything this output plays. Call before start().
    void setFxRack(const FxRack* rack) { fx_ = std::make_unique<FxRunner>(rack); }

    VoiceMixer& mixer() { return mixer_; }
    void setTimingEnabled(bool on) { mixer_.setTimingEnabled(on); }
    std::vector<VoiceMixer::TimingSample> drainTiming() { return mixer_.drainTiming(); }

signals:
    void deviceError(const QString& message);

private:
    void run();
    void tick();

    VoiceMixer mixer_;
    std::unique_ptr<FxRunner> fx_;
    mutable std::mutex mutex_;                               // guards device_, deviceChanged_, failure_
    std::condition_variable wake_;
    QString device_;
    bool deviceChanged_ = false;
    QString failure_;
    std::atomic<bool> running_{ false };
    std::atomic<bool> quit_{ false };
    std::atomic<bool> failed_{ false };
    std::atomic<double> latencyMs_{ kOutputLatencyMs };
    std::thread thread_;
    QTimer timer_;
};

} // namespace vdj

#endif // DJTOOLZ_ENGINE_H
