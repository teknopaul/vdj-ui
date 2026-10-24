#ifndef PLAYER_PANEL_H
#define PLAYER_PANEL_H

#include <QWidget>
#include <QList>
#include "track_details.h"

class QComboBox;
class QLabel;
class QPushButton;
class QHBoxLayout;

namespace vdj {

class AudioPlayer;
class WaveformWidget;

// Local preview player: output device, play / pause / stop to the left of the track's waveform, a playhead that
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
    QPushButton* playButton() const { return play_; }
    QPushButton* pauseButton() const { return pause_; }
    QPushButton* stopButton() const { return stop_; }
    QPushButton* bpmDownButton() const { return bpmDown_; }
    QPushButton* bpmUpButton() const { return bpmUp_; }
    const QList<QPushButton*>& cueButtons() const { return cueButtons_; }
    double originalBpm() const { return originalBpm_; }
    double targetBpm() const { return targetBpm_; }
    void setTargetBpm(double bpm);               // BPM to play at; the tempo ratio is target / original
    void refreshDevices();

    static constexpr double kBpmStep = 0.1;

signals:
    void status(const QString& message);

private:
    void updateBpmLabel();
    void updateButtons();
    void rebuildCueButtons();

    AudioPlayer* player_;
    WaveformWidget* waveform_;
    QComboBox* device_;
    QPushButton* play_;
    QPushButton* pause_;
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
};

} // namespace vdj

#endif // PLAYER_PANEL_H
