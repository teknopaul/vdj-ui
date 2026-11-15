#ifndef STREAM_PLAYER_H
#define STREAM_PLAYER_H

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace vdj {

// Where a StreamPlayer sends its audio: interleaved stereo float frames at 44.1 kHz. write() may block (that paces the player).
class StreamSink {
public:
    virtual ~StreamSink() = default;
    virtual bool write(const float* stereo, int frames, QString* err) = 0;
    virtual void drain() {}
};
using StreamSinkFactory = std::function<std::unique_ptr<StreamSink>(QString* err)>;

// The PulseAudio sink (also PipeWire's pulse server).
std::unique_ptr<StreamSink> openPulseSink(QString* err);

// Plays whatever a callback produces, in blocks of kBlockFrames, on its own thread. The callback runs on that thread; it returns false after the
// last block (which is still played). finished() / failed() are emitted on the owner's thread.
class StreamPlayer : public QObject {
    Q_OBJECT

public:
    static constexpr int kBlockFrames = 2048;
    using Fill = std::function<bool(float* stereo, int frames)>;

    explicit StreamPlayer(QObject* parent = nullptr, StreamSinkFactory factory = openPulseSink);
    ~StreamPlayer() override;

    void setFactory(StreamSinkFactory f) { if (!playing_) factory_ = std::move(f); }
    bool play(Fill fill);                  // false when already playing
    void stop();                           // blocks until the thread is done; no finished() is emitted
    bool isPlaying() const { return playing_; }
    long long positionFrames() const { return written_; }       // frames handed to the sink since play()

signals:
    void finished();
    void failed(const QString& message);

private:
    void run(Fill fill);

    StreamSinkFactory factory_;
    std::thread thread_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> playing_{false};
    std::atomic<long long> written_{0};
};

} // namespace vdj

#endif
