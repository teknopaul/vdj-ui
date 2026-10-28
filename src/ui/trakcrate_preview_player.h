#ifndef TRAKCRATE_PREVIEW_PLAYER_H
#define TRAKCRATE_PREVIEW_PLAYER_H

#include <QStringList>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace vdj {

class AudioPlayer;
class ConfigManager;

// A mini player for listening to a whole list at home: play, pause, stop, previous and next, and it carries on through the list by itself. Tracks
// play at their natural tempo, or all at one BPM (the usual way a DJ plays a set), in which case they are time stretched in memory only: no file
// is ever changed. One list plays at a time. It is not available while the app is on the Pro DJ Link network (the blocker says why), and its stop
// button gives the audio device back to the DJ decks.
class TrakcratePreviewPlayer : public QWidget {
    Q_OBJECT

public:
    explicit TrakcratePreviewPlayer(ConfigManager* config = nullptr, QWidget* parent = nullptr);

    // Returns the reason playing is not allowed right now, or an empty string. Asked before every start.
    void setBlocker(std::function<QString()> blocker) { blocker_ = std::move(blocker); updateButtons(); }
    // The list is the track folders of the master, in the order to play. ownerKey names the view they came from. False (with err) when blocked or empty.
    bool playList(const QString& ownerKey, const QStringList& trackDirs, int startIndex, QString* err = nullptr);
    void stop();                                       // stops and forgets the list
    void togglePause();
    void next();
    void previous();

    bool isActive() const { return !dirs_.isEmpty(); }
    bool isPlaying() const;
    QString ownerKey() const { return owner_; }
    int currentIndex() const { return index_; }
    QString currentDir() const { return index_ >= 0 && index_ < dirs_.size() ? dirs_[index_] : QString(); }
    double currentTempo() const { return tempo_; }     // source speed / output speed of the current track (1 = natural)

    // Natural BPM, or one BPM for every track.
    void setFixedBpm(bool on, double bpm = 0);
    bool fixedBpm() const;
    double targetBpm() const;
    static double trackBpm(const QString& trackDir);   // beatgrid.txt, else the BPM tag; 0 when unknown
    static double tempoFor(double trackBpm, double targetBpm);   // the stretch ratio, within what the stretcher can do

    void setAllowedHint(bool listPlayable);            // the current view can (or cannot) be played; only changes the buttons
    void refreshAllowed() { updateButtons(); }

    AudioPlayer* player() const { return player_; }
    QPushButton* playButton() const { return play_; }
    QPushButton* stopButton() const { return stop_; }
    QPushButton* nextButton() const { return next_; }
    QPushButton* previousButton() const { return prev_; }
    QCheckBox* continueBox() const { return continue_; }          // go on to the next playlist at the end (off by default)
    QComboBox* modeBox() const { return mode_; }
    QDoubleSpinBox* bpmBox() const { return bpm_; }
    QLabel* label() const { return label_; }

signals:
    void trackStarted(const QString& ownerKey, int index, const QString& trackDir);
    void stopped();
    void endOfList(const QString& ownerKey);           // the list ran out and "next playlist" is ticked: the owner starts the next one or calls stop()
    void playRequested();                              // the play button with nothing playing: the owner decides what to play
    void status(const QString& message);

private:
    bool startTrack(int index, int direction);
    void applyTempo();
    void updateButtons();
    void trackFinished();

    ConfigManager* config_;
    AudioPlayer* player_;
    std::function<QString()> blocker_;
    QStringList dirs_;
    QString owner_;
    int index_ = -1;
    double tempo_ = 1.0;
    bool viewPlayable_ = false;
    QPushButton *prev_, *play_, *stop_, *next_;
    QComboBox* mode_;
    QDoubleSpinBox* bpm_;
    QLabel* label_;
    QCheckBox* continue_;
};

} // namespace vdj

#endif
