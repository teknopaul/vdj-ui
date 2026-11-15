#ifndef MIX_PREVIEWER_H
#define MIX_PREVIEWER_H

#include "audio/stream_player.h"
#include "trakcrate/mix_engine.h"

#include <QObject>
#include <QTimer>

#include <atomic>
#include <memory>

namespace vdj {

// Plays a studio mix through the sound card (plan studio-mix_PLAN.md, phase 9): the tracks are prepared (stretched and edited) in the background,
// the engine renders blocks on the audio thread. A track that is not ready yet plays as silence and prefetching goes on.
class MixPreviewer : public QObject {
    Q_OBJECT

public:
    enum State { Stopped, Preparing, Playing };

    MixPreviewer(trakcrate::StretchCache* cache, QObject* parent = nullptr, StreamSinkFactory factory = openPulseSink);
    ~MixPreviewer() override;

    void setSinkFactory(StreamSinkFactory f) { player_->setFactory(std::move(f)); }
    void setMix(const trakcrate::Mix& mix, const std::vector<trakcrate::TrackFacts>& facts);
    State state() const { return state_; }
    bool isActive() const { return state_ != Stopped; }
    int underruns() const { return underruns_; }

    void toggle(double fromMs);          // stopped: start at fromMs; otherwise stop
    void start(double fromMs);
    void stop();

signals:
    void positionChanged(double ms);
    void stateChanged(int state);
    void status(const QString& text);

private:
    class Source;
    void tick();
    void begin();
    void setState(State s);
    void teardown();

    trakcrate::StretchCache* stretchCache_;
    trakcrate::Mix mix_;
    std::vector<trakcrate::TrackFacts> facts_;
    StreamPlayer* player_;
    QTimer* timer_;
    State state_ = Stopped;
    std::unique_ptr<trakcrate::TrackCache> cache_;
    std::unique_ptr<Source> source_;
    std::unique_ptr<trakcrate::MixEngine> engine_;
    long long startFrame_ = 0;
    std::atomic<long long> pos_{0};            // next mix frame the audio thread renders
    std::atomic<int> missing_{0};              // 1 + index of a track the audio thread found not ready, 0 = none
    int underruns_ = 0;
    int preparingFor_ = -1;
    int waited_ = 0;
};

} // namespace vdj

#endif
