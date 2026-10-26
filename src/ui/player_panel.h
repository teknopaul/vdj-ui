#ifndef PLAYER_PANEL_H
#define PLAYER_PANEL_H

#include <QWidget>
#include <QList>
#include "track_details.h"
#include "audio/beat_clock.h"

class QComboBox;
class QLabel;
class QPushButton;
class QHBoxLayout;

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
    void setQuantizeSettings(QuantizeSettings* settings) { quantizeSettings_ = settings; }   // the global resolution
    void setAnalysisForTest(const TrackAnalysis& analysis);   // test-only: set the beat grid without loading a track

    static constexpr double kBpmStep = 0.1;

signals:
    void status(const QString& message);
    void beatChanged(int beatNumber);            // the play position entered a new beat of the grid

private:
    void updateBpmLabel();
    void updateButtons();
    void rebuildCueButtons();
    void setBeatGrid();
    void jumpToCue(double cueMs);
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
    QPushButton* stop_;
    QPushButton* bpmDown_;
    QPushButton* bpmUp_;
    QLabel* title_;
    QLabel* time_;
    QLabel* bpm_;
    QHBoxLayout* cueLayout_;
    QList<QPushButton*> cueButtons_;
    TrackAnalysis analysis_;
    double originalBpm_ = 0.0;
    double targetBpm_ = 0.0;
    BeatClock clock_;
    BeatPhase phase_;
};

} // namespace vdj

#endif // PLAYER_PANEL_H
