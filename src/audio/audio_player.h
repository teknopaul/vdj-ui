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
#include "time_stretcher.h"

namespace vdj {

// Previews one track on a local output device. A worker thread renders (tempo changed) audio and writes it to
// PulseAudio/PipeWire; the GUI thread only sees the position through a timer.
class AudioPlayer : public QObject {
    Q_OBJECT

public:
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

    void setDevice(const QString& sinkName);     // empty = default; takes effect immediately
    QString device() const;

public slots:
    void play();                                 // from the current position (the start after stop)
    void pause();
    void stop();                                 // pause and go back to the start
    void seekMs(double ms);
    void setTempo(double ratio);                 // 1.0 = original speed, key is kept

signals:
    void stateChanged(int state);
    void positionChanged(double ms);             // ~30 times a second while playing
    void finished();
    void deviceError(const QString& message);

private:
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
    QString failure_;                            // guarded by mutex_
    std::thread thread_;
    QTimer timer_;
    State reported_ = Stopped;
};

} // namespace vdj

#endif // AUDIO_PLAYER_H
