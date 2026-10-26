#ifndef DECK_WIDGET_H
#define DECK_WIDGET_H

#include <QWidget>
#include <QStringList>

class QLabel;

namespace vdj {

class BeatGridWidget;
class DeckControls;
class DiffLockBar;
class PitchSlider;
class DebugOverlay;

class DeckWidget : public QWidget {
    Q_OBJECT

public:
    explicit DeckWidget(int playerId, QWidget* parent = nullptr);

    int playerId() const { return playerId_; }
    void setPlayerId(int id);
    void setModel(const QString& model);
    QString model() const { return model_; }
    void setIp(const QString& ip);
    QString ip() const { return ip_; }
    void setRemote(bool remote);
    bool isRemote() const { return remote_; }
    QString titleText() const;
    void setBpm(double bpm);
    void setMeasuredBpm(double bpm);             // remote decks: tempo from the beat timings, 0 = unknown
    void setSequencerBpm(double bpm);            // VDJ deck: tempo of the ALSA sequencer
    void setMaster(bool master);
    void setSync(bool sync);
    void setPlaying(bool playing);
    void setBeatTimestamp(qint64 millisSinceEpoch);
    void setBarPos(int barPos);

    BeatGridWidget* beatGrid() const;
    DeckControls* controls() const;
    DiffLockBar* diffLockBar() const { return diffBar_; }   // VDJ (local) decks only
    bool isPlaying() const { return playing_; }
    double bpm() const { return bpm_; }
    double pitchPercentage() const;
    void setPitchPercentage(double percentage);
    void setDebugMode(bool enabled);
    void setDebugOverlay(const QStringList& lines);
    DebugOverlay* overlay() const { return overlay_; }

signals:
    void deckClicked(int playerId);
    void playPauseClicked(int playerId, bool play);
    void restartClicked(int playerId, bool quantize);
    void beatJumpClicked(int playerId, int beats);
    void syncClicked(int playerId);
    void setBpmClicked(int playerId, double bpm);
    void becomeMasterClicked(int playerId);
    void bpmFromMasterClicked(int playerId);
    void broadcastBeatClicked(int playerId, double bpm, int barPos);
    void pitchChanged(int playerId, double percentage);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    int playerId_;
    QString model_;
    QString ip_;
    bool remote_ = false;
    double bpm_ = 0.0;
    bool master_ = false;
    bool sync_ = false;
    bool playing_ = false;
    double pitch_ = 0.0;
    QLabel* titleLabel_;
    BeatGridWidget* beatGrid_;
    DeckControls* controls_;
    DiffLockBar* diffBar_;
    PitchSlider* pitchSlider_;
    DebugOverlay* overlay_;
    void updateTitle();
};

} // namespace vdj

#endif // DECK_WIDGET_H
