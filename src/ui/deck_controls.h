#ifndef DECK_CONTROLS_H
#define DECK_CONTROLS_H

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
    bool barEditable() const;
    bool playVisible() const;
    bool copyBpmVisible() const;
    bool syncVisible() const;
    // Mark the Sync button of the deck the VDJ is difflocked to.
    void setSyncLocked(bool locked);
    bool syncLocked() const { return syncLocked_; }

signals:
    void playPauseClicked(int playerId, bool play);
    void syncClicked(int playerId);
    void setBpmClicked(int playerId, double bpm);
    void becomeMasterClicked(int playerId);
    void bpmFromMasterClicked(int playerId);
    void broadcastBeatClicked(int playerId, double bpm, int barPos);

private:
    int playerId_;
    bool remote_ = false;
    bool syncLocked_ = false;
    QPushButton* playPause_;
    QPushButton* sync_;
    QPushButton* becomeMaster_;
    QPushButton* copyBpm_;
    QPushButton* broadcastBeat_;
    QDoubleSpinBox* bpmSpin_;
    QSpinBox* barPosSpin_;
    QLabel* bpmValueLabel_;
};

} // namespace vdj

#endif // DECK_CONTROLS_H