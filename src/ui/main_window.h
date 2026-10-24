#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QElapsedTimer>
#include "midi/diff_lock.h"

class QLabel;
class QTimer;
class QDockWidget;
class QAction;
class QMenu;
class QCloseEvent;

namespace vdj {

class NetworkManager;
class AutoIpManager;
class DeckArea;
class Toolbar;
class DeckWidget;
class ConfigManager;
class NfsMountManager;
class TrackBrowser;
class NowPlaying;
class DebugPanel;
class AlsaClock;
class PlayerPanel;
class DiffLockBar;
class AutoipStatusWidget;
class BpmRampWidget;
class HudWindow;
class RecordingPanel;
class AudioRecorder;
class InstrumentManager;
class MidiRouter;
class MidiMapInput;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void setDebugModeFromCli();
    void addDemoDecks();                             // placeholder remote decks (--mock-beat)
    AlsaClock* alsaClock() const { return clock_; }   // the VDJ's ALSA sequencer (MIDI clock)

protected:
    void closeEvent(QCloseEvent* event) override;
    void shutdown();

private slots:
    void connectToNetwork();
    void disconnectFromNetwork();

private:
    void openRekordboxFolder();
    void setupMenus();
    void setupToolbar();
    void setupStatusBar();
    void setupShortcuts();
    void setupDemoDecks();
    void setupDemoTrackBrowser();
    void startDemoClocks();
    void setupKeepalive();
    void setupDebugPanel();
    void connectDeckSignals();
    void setDebugMode(bool enabled);
    void updateDeckOverlays(int pid, double bpm, int pitch, int flags,
                            int masterState, int active);
    void nudgeLocalBpm(double delta);
    void setToolbarConnected(bool connected);
    bool setVdjBpm(double bpm);
    void setVdjPlaying(bool playing);
    void updateSequencerPlayhead();
    // difflock (see DiffLock): the sequencer keeps a fixed offset from one deck's beat
    void lockDiffLockTo(int playerId);
    void diffLockOff();
    void setDiffLockFollowMaster(bool on);
    void nudgeSequencer(int ms);
    void captureDiffLockTarget();
    void refreshDiffLock();
    DiffLockBar* vdjDiffLockBar() const;
    bool isVdjRow(const DeckWidget* d) const;
    bool isVdjDeck(int playerId) const;
    void setupMidi();
    void setupRecording();
    void startMidiMap(const QString& path);
    void chooseMidiMap();
    QString defaultMidiMapPath() const;
    void setVdjTransport(bool playing);
    void startBpmRamp(double targetBpm, int bars);
    void stopBpmRamp();
    void updateRampEnabled();
    int localVdjId() const;
    void showHud(bool show);
    void setAutoIp(bool on);
    void populateDeckFilesMenu();
    void browseDeck(int playerId, const QString& ip);
    void setAutoIpChecked(bool on);
    QString activeInterface() const;

    DeckWidget* deckAt(int playerId) const;
    bool isLocalDeck(int playerId) const;
    void applyPitchFromDeck(int playerId);
    void markDeckBpm(int playerId, double bpm);
    void syncToMaster();
    void copyBpmFromMaster(int playerId);
    void broadcastFromActiveDeck();
    int activeDeckIndex() const { return activeDeckIndex_; }
    int activeDeckId() const;
    void cycleActiveDeck();

    DeckArea* deckArea_;
    Toolbar* toolbar_;
    QAction* autoIpMenuAction_ = nullptr;
    QMenu* deckFilesMenu_ = nullptr;
    NetworkManager* network_;
    AlsaClock* clock_ = nullptr;
    DiffLock difflock_;
    AutoIpManager* autoIp_;
    ConfigManager* config_;
    NfsMountManager* nfs_;
    TrackBrowser* trackBrowser_;
    NowPlaying* nowPlaying_;
    PlayerPanel* playerPanel_ = nullptr;
    DebugPanel* debugPanel_;
    QDockWidget* debugDock_;
    bool debugMode_ = false;
    QLabel* statusNetwork_;
    QLabel* statusMaster_;
    AutoipStatusWidget* statusAutoIp_ = nullptr;
    BpmRampWidget* bpmRamp_ = nullptr;
    QTimer* rampTimer_ = nullptr;
    QElapsedTimer rampElapsed_;
    double rampFrom_ = 0.0;
    double rampTo_ = 0.0;
    qint64 rampDurationMs_ = 0;
    HudWindow* hud_ = nullptr;
    QAction* hudAction_ = nullptr;
    QAction* recordAction_ = nullptr;
    QDockWidget* recordDock_ = nullptr;
    AudioRecorder* recorder_ = nullptr;
    RecordingPanel* recordingPanel_ = nullptr;
    MidiRouter* midiRouter_ = nullptr;
    InstrumentManager* instruments_ = nullptr;
    MidiMapInput* midiIn_ = nullptr;
    QLabel* statusDecks_;
    QTimer* demoClock_;
    QTimer* sequencerTimer_ = nullptr;
    QTimer* keepaliveTimer_;
    QElapsedTimer demoElapsed_;
    QVector<int> deckOrder_; // player ids in the deck area, top = VDJ
    int activeDeckIndex_ = 0;
    double masterBpm_ = 0.0;
    int masterPid_ = -1;
    QString currentInterface_;
    void rememberInterface(const QString& iface);   // sets currentInterface_ and persists it as the default
};

} // namespace vdj

#endif // MAIN_WINDOW_H
