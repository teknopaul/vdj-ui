#ifndef PLAYER_PANEL_H
#define PLAYER_PANEL_H

#include <QKeyEvent>
#include <QWidget>
#include <QList>
#include "track_analysis.h"
#include "track_details.h"
#include "audio/beat_clock.h"
#include "audio/beat_source.h"

class QComboBox;
class QEvent;
class QKeyEvent;
class QLabel;
class QPushButton;
class QHBoxLayout;
class QTimer;

namespace vdj {

class AudioPlayer;
class WaveformWidget;
class QuantizeSettings;

// Local preview player: output device, back / play-pause / forward and stop to the left of the track's waveform, a playhead that
// follows the audio, click on the waveform to jump, BPM -/+ (tempo change, key kept) and a button per cue point.
class PlayerPanel : public QWidget {
    Q_OBJECT

public:
    explicit PlayerPanel(QWidget* parent = nullptr);

    // Load info.audioPath for playing and show its waveform / cues. Returns false (and shows why) on failure.
    bool loadTrack(const TrackInfo& info);
    // Load a track of the trakcrate master: the audio file of that folder, with its beat grid, cuemarks and waveform from the master's own files
    // (trakcrate_analysis.h). Nothing is written. False (and a status message) when the folder has no playable audio.
    bool loadTrakcrateTrack(const QString& trackDir);
    // The master's analysis again (its beat grid or cuemarks were edited) without stopping the deck. No-op unless this track is loaded.
    void reloadTrakcrateAnalysis(const QString& trackDir);
    // Keys that operate the player: Space (play / pause), Page Up / Page Down (+-4 beats). No modifiers except the keypad one.
    static bool isPlayerKey(const QKeyEvent* e);
    // The hot cue a/b/c (Qt::Key_A..Key_C) jumps to; null when the key is not one of them or the track has no such cue.
    static const AnalysisCue* hotCueForKey(const QList<AnalysisCue>& cues, int key);
    // True when handlePlayerKey would act on e: a player key and a track loaded (Space then stays the VDJ's play / pause otherwise).
    bool wantsPlayerKey(const QKeyEvent* e) const;
    // Do what e asks of the player; false (e untouched) when wantsPlayerKey is false. The trakcrate track list forwards its keys here.
    bool handlePlayerKey(QKeyEvent* e);
    void togglePlay() { playClicked(); }          // play / pause (queued on the sequencer's beat when Q is on)
    bool isPlaying() const;
    QString trakcrateDir() const { return trakcrateDir_; }       // empty unless the loaded track came from the master

    AudioPlayer* player() const { return player_; }
    WaveformWidget* waveform() const { return waveform_; }
    QComboBox* deviceCombo() const { return device_; }
    QPushButton* playButton() const { return play_; }        // play | pause toggle
    QPushButton* backButton() const { return back_; }
    QPushButton* forwardButton() const { return forward_; }
    QPushButton* quantizeButton() const { return quantize_; }
    QPushButton* stopButton() const { return stop_; }
    QPushButton* bpmDownButton() const { return bpmDown_; }
    QPushButton* bpmUpButton() const { return bpmUp_; }
    QPushButton* bpmResetButton() const { return bpmReset_; }   // back to the track's own BPM
    QPushButton* bpmLockButton() const { return bpmLock_; }     // checked: loading a track keeps the current BPM (not saved, off at start)
    bool bpmLocked() const;
    const QList<QPushButton*>& cueButtons() const { return cueButtons_; }
    double originalBpm() const { return originalBpm_; }
    double targetBpm() const { return targetBpm_; }
    void setTargetBpm(double bpm);               // BPM to play at; the tempo ratio is target / original
    void refreshDevices();

    // Where the play position is inside the track's beat grid. Updated from positionChanged (~30 Hz), so it can be up to
    // ~33 ms stale: it is for display / state only. Jump logic must call clock_.locate(player_->positionMs()) afresh.
    struct BeatPhase {
        bool valid = false;
        int beatIndex = 0;
        int beatNumber = 1;         // 1..4 within the bar
        double sinceBeatMs = 0.0;
        double fraction = 0.0;      // 0..1 through the beat
    };
    bool hasBeatGrid() const { return clock_.isValid(); }
    BeatPhase beatPhase() const { return phase_; }
    // The VDJ's ALSA sequencer (not owned). With Q on and the sequencer running, Play and the cue buttons wait for its next beat (at the
    // global resolution) instead of going at once. This is only a launch on the beat, the player is not beat-synced to it afterwards.
    void setSequencerBeats(const BeatSource* source) { sequencer_ = source; }
    // Wall ms until the sequencer's next grid point; -1 when it does not apply (Q off, no sequencer, not running), 0 when a grid point is just past.
    double sequencerGridWaitMs() const;
    bool playPending() const { return playPending_; }
    void setQuantizeSettings(QuantizeSettings* settings) { quantizeSettings_ = settings; }   // the global resolution
    void setAnalysisForTest(const TrackAnalysis& analysis);   // test-only: set the beat grid without loading a track

    static constexpr double kBpmStep = 0.1;

protected:
    bool event(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

signals:
    void status(const QString& message);
    void trakcrateTrackLoaded(const QString& trackDir);   // a master track was loaded (folder), or empty when a track from elsewhere replaced it
    void focusTrackListRequested();              // Up / Down while this panel has the focus: the owner gives it to the track list
    void beatChanged(int beatNumber);            // the play position entered a new beat of the grid

private:
    bool loadImpl(const TrackInfo& info, const TrackAnalysis* given);
    void updateBpmLabel();
    void updateButtons();
    void rebuildCueButtons();
    void setBeatGrid();
    void jumpToCue(double cueMs);
    bool isCueKey(const QKeyEvent* e) const;     // a, b, c, q with the player focused and a track loaded (not forwarded from the track list)
    void playClicked();
    void cancelPendingPlay();
    void jumpBeats(int beats);                   // pure BPM jump: negative = back, no beat grid needed
    void updatePhase(double ms);

    AudioPlayer* player_;
    WaveformWidget* waveform_;
    QComboBox* device_;
    QPushButton* play_;
    QPushButton* back_;
    QPushButton* forward_;
    QPushButton* quantize_;
    QuantizeSettings* quantizeSettings_ = nullptr;
    bool warnedNoGrid_ = false;
    const BeatSource* sequencer_ = nullptr;
    QTimer* playTimer_ = nullptr;
    bool playPending_ = false;
    QPushButton* stop_;
    QPushButton* bpmDown_;
    QPushButton* bpmUp_;
    QPushButton* bpmReset_;
    QPushButton* bpmLock_;
    QLabel* title_;
    QLabel* time_;
    QLabel* bpm_;
    QHBoxLayout* cueLayout_;
    QList<QPushButton*> cueButtons_;
    QString trakcrateDir_;
    TrackAnalysis analysis_;
    double originalBpm_ = 0.0;
    double targetBpm_ = 0.0;
    double heldBpm_ = 0.0;                        // the last BPM played at: what the BPM lock keeps, also over a track without a BPM
    BeatClock clock_;
    BeatPhase phase_;
};

} // namespace vdj

#endif // PLAYER_PANEL_H
