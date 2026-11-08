#ifndef TRAKCRATE_EDIT_PREVIEWER_H
#define TRAKCRATE_EDIT_PREVIEWER_H

#include <QObject>
#include <QTemporaryDir>

#include <atomic>
#include <memory>

#include "audio/audio_player.h"
#include "trakcrate/edit_job.h"

class QThread;

namespace vdj {

// Plays the edit without any window: toggle() renders the regions on a worker thread (once per change; the render is cached), plays it from a place in
// the original (the play marker) with a player of its own, and reports the position as a position in the ORIGINAL (positionChanged), so the owner can move
// a time line across the original's waveform. The deck and the track in the editor are untouched.
class EditPreviewer : public QObject {
    Q_OBJECT

public:
    EditPreviewer(trakcrate::EditRenderCache* cache, QObject* parent = nullptr);
    ~EditPreviewer() override;

    // Playing: stop. Otherwise play `req` from startMs of the original (-1: the start) and pause after the last place the edit plays stopMs (-1: to the end).
    void toggle(const trakcrate::EditRequest& req, double startMs = -1, double stopMs = -1);
    void start(const trakcrate::EditRequest& req, double startMs = -1, double stopMs = -1);
    void stop();                                                 // also cancels a render in progress

    bool isPlaying() const { return playing_; }
    bool isBusy() const { return thread_ != nullptr; }          // rendering
    AudioPlayer* player() { return &player_; }

signals:
    void positionChanged(double sourceMs);                       // while playing
    void stateChanged(bool playing);
    void status(const QString& message);

private slots:
    void rendered(bool ok, const QString& error);

private:
    void setPlaying(bool on);

    trakcrate::EditRenderCache* cache_;
    trakcrate::EditRequest req_;
    QTemporaryDir tmp_;
    AudioPlayer player_;
    std::shared_ptr<trakcrate::DecodedPcm> loaded_;              // the render the player holds
    std::atomic<bool> cancel_{false};
    QThread* thread_ = nullptr;
    bool playing_ = false;
    double startMs_ = -1, stopMs_ = -1;
    double stopOutMs_ = -1;
    bool discard_ = false;                                       // stop() was pressed while rendering: do not play when it is done
};

} // namespace vdj

#endif
