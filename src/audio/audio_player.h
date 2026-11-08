#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include "audio_decoder.h"
#include "fx_rack.h"
#include "time_stretcher.h"

namespace vdj {

// Where a pending jump falls inside the next render chunk. posFrame / triggerFrame are source frames, tempo the
// source-per-output ratio. Returns -1 when the trigger is not reached within this chunk, otherwise the number of
// output frames to render before the jump (0 = jump first). The heard position is an estimate (it already subtracts
// buffered output), good to well under a chunk.
int splitAtTrigger(double posFrame, double tempo, double triggerFrame, int chunkFrames);

// The click-safe edges of a loop. A chunk of `frames` stereo frames (interleaved qint16) is scaled in place:
//  - before the wrap the gain falls linearly to 0 over the last fadeOutFrames before the loop end: framesToEnd is the number of output frames from the
//    chunk start to the loop end (< 0 = no loop end ahead, no fade out);
//  - from output frame wrapAt on (-1 = no wrap in this chunk) the audio is the loop start again and the gain rises linearly from 0 over fadeInFrames.
// *fadeInPos counts the frames of the fade in done so far and carries over to the next chunk (>= fadeInFrames or < 0 = not fading in).
constexpr int kLoopFadeOutFrames = 882;      // 20 ms at 44.1 kHz
constexpr int kLoopFadeInFrames = 220;       // 5 ms
void applyLoopFades(qint16* chunk, int frames, int channels, double framesToEnd, int wrapAt, int fadeOutFrames, int fadeInFrames, int* fadeInPos);

// Previews one track on a local output device. A worker thread renders (tempo changed) audio and writes it to
// PulseAudio/PipeWire; the GUI thread only sees the position through a timer.
class AudioPlayer : public QObject {
    Q_OBJECT

public:
    // The effects applied to what this player plays. Call before the first play().
    void setFxRack(const FxRack* rack) { fx_ = std::make_unique<FxRunner>(rack); }
    enum State { Stopped, Playing, Paused };

    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;

    // Decode and load a file (blocks while decoding). Stops playback. Returns false and sets *error on failure.
    bool load(const QString& path, QString* error = nullptr);
    void unload();
    bool isLoaded() const { return frames_ > 0; }
    double lengthMs() const { return decoded_.lengthMs(); }

    State state() const { return state_.load(); }
    double positionMs() const;
    double tempo() const;
    bool hasPendingJump() const;

    void setDevice(const QString& sinkName);     // empty = default; takes effect immediately
    QString device() const;

public slots:
    void play();                                 // from the current position (the start after stop)
    void pause();
    void stop();                                 // pause and go back to the start
    void seekMs(double ms);
    // When the heard (source) position reaches triggerSourceMs, seek to targetMs, sample-accurately inside the render
    // chunk. A trigger already passed (or not playing) seeks at once. A newer request replaces the pending one.
    void seekAtMs(double triggerSourceMs, double targetMs);
    // Loop [startMs, endMs): when the playing position reaches the end it goes back to the start, sample-accurately inside the render chunk, with a
    // 20 ms fade out before the end and a 5 ms fade in after the start. False (and no loop) when it is shorter than 25 ms or outside the track.
    // Seeking does not clear it (the owner decides: a jump into the loop keeps it); pause keeps it; stop, load and unload clear it. Playing from
    // before the start plays on normally and loops once the end is reached; a position well past the end is outside the loop and plays on.
    bool setLoop(double startMs, double endMs);
    void clearLoop();
    bool hasLoop() const;
    void cancelJump();                           // also done by stop, pause, seek, tempo change, load and unload
    void setTempo(double ratio);                 // 1.0 = original speed, key is kept

signals:
    void stateChanged(int state);
    void positionChanged(double ms);             // ~30 times a second while playing
    void jumped(double targetMs);                // a seekAtMs request was executed
    void finished();
    void deviceError(const QString& message);

private:
    std::unique_ptr<FxRunner> fx_;
    void run();
    void tick();

    DecodedAudio decoded_;
    qint64 frames_ = 0;

    mutable std::mutex mutex_;                   // guards stretcher_, device_, deviceChanged_
    std::condition_variable wake_;
    TimeStretcher stretcher_;
    QString device_;
    bool deviceChanged_ = false;
    std::atomic<State> state_{ Stopped };
    std::atomic<bool> quit_{ false };
    std::atomic<bool> ended_{ false };
    std::atomic<bool> failed_{ false };
    std::atomic<bool> jumped_{ false };
    bool pendingJump_ = false;                   // guarded by mutex_
    double jumpTriggerFrame_ = 0.0, jumpTargetFrame_ = 0.0;
    bool looping_ = false;                       // guarded by mutex_
    double loopStartFrame_ = 0.0, loopEndFrame_ = 0.0;
    int fadeInPos_ = -1;                         // guarded by mutex_, see applyLoopFades
    QString failure_;                            // guarded by mutex_
    std::thread thread_;
    QTimer timer_;
    State reported_ = Stopped;
};

} // namespace vdj

#endif // AUDIO_PLAYER_H
