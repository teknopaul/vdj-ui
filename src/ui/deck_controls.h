#ifndef DECK_CONTROLS_H
#define DECK_CONTROLS_H

#include <QString>
#include <QWidget>

class QPushButton;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;

namespace vdj {

class DeckControls : public QWidget {
    Q_OBJECT

public:
    explicit DeckControls(int playerId, QWidget* parent = nullptr);

    int playerId() const { return playerId_; }
    void setPlayerId(int id) { playerId_ = id; }
    void setBpm(double bpm);
    // Remote decks: the tempo measured from the beat packets' arrival (3 decimals); 0 = not known yet
    void setMeasuredBpm(double bpm);
    QString measuredText() const;
    // The VDJ deck: the tempo of the ALSA sequencer the drum machines follow (3 decimals); the spinner is the link BPM
    void setSequencerBpm(double bpm);
    QString sequencerText() const;
    double bpm() const;
    void setPlaying(bool playing);
    void setBarPos(int barPos);
    int barPos() const;
    // Remote (non-local) decks are display-only: they cannot be controlled
    // over the link, so they expose no editable fields and only the Copy BPM
    // action (which adopts that deck's tempo into the local VDJ).
    void setRemote(bool remote);
    bool remote() const { return remote_; }
    bool bpmEditable() const;
    // Synced to a real deck: the tempo follows that deck and the spinner cannot change it.
    void setBpmLocked(bool locked, const QString& tip = QString());
    bool barEditable() const;
    bool playVisible() const;
    QPushButton* quantizeButton() const { return quantize_; }
    QPushButton* restartButton() const { return restart_; }
    QPushButton* backButton() const { return back_; }
    QPushButton* forwardButton() const { return forward_; }
    bool quantizeOn() const;
    // The restart / beat jump buttons only make sense while the sequencer runs.
    void setJumpsEnabled(bool enabled);
    bool jumpsVisible() const;
    bool copyBpmVisible() const;
    bool syncVisible() const;
    // What the Sync button shows (see SyncController): the deck followed, the VDJ row for "the master".
    enum class SyncState { Off, Waiting, Syncing, Locked, Suspended };
    void setSyncState(SyncState state);
    SyncState syncState() const { return syncState_; }
    void setSyncLocked(bool locked) { setSyncState(locked ? SyncState::Locked : SyncState::Off); }
    bool syncLocked() const { return syncState_ == SyncState::Syncing || syncState_ == SyncState::Locked; }   // this row is being followed

signals:
    void playPauseClicked(int playerId, bool play);
    void restartClicked(int playerId, bool quantize);   // MIDI stop / start at the start of the bar
    void beatJumpClicked(int playerId, int beats);       // -1 / +1: the bar position moves one beat
    void syncClicked(int playerId);
    void setBpmClicked(int playerId, double bpm);
    void becomeMasterClicked(int playerId);
    void bpmFromMasterClicked(int playerId);
    void broadcastBeatClicked(int playerId, double bpm, int barPos);

private:
    int playerId_;
    bool remote_ = false;
    SyncState syncState_ = SyncState::Off;
    QPushButton* playPause_;
    QPushButton* quantize_;
    QPushButton* restart_;
    QPushButton* back_;
    QPushButton* forward_;
    QPushButton* sync_;
    QPushButton* becomeMaster_;
    QPushButton* copyBpm_;
    QPushButton* broadcastBeat_;
    QDoubleSpinBox* bpmSpin_;
    QSpinBox* barPosSpin_;
    QLabel* bpmValueLabel_;
    QLabel* measuredLabel_;
    QLabel* sequencerLabel_;
};

} // namespace vdj

#endif // DECK_CONTROLS_H