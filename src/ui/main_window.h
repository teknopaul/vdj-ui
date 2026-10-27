#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include "audio/deck_phase.h"
#include <QFont>
#include <QVector>
#include <QElapsedTimer>
#include <QStringList>
#include <map>
#include "network/beat_tempo.h"
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
class DeckWidget;
class ConfigManager;
class QuantizeSettings;
class JoystickSeqBridge;
class NfsMountManager;
class TrackBrowser;
class DebugPanel;
class AlsaClock;
class PlayerPanel;
class DiffLockBar;
class AutoipStatusWidget;
class BpmRampWidget;
class HudWindow;
class DjtoolzPanel;
class RecordingPanel;
class AudioRecorder;
class InstrumentManager;
class MidiMonitorDialog;
class DjtoolzMidiControl;
class MidiRouter;
class MidiMapInput;
class PendriveRegistry;       // src/export/pendrive_registry.h
class PendriveToolBar;         // src/ui/pendrive_toolbar.h
class VirtualPendrivesDialog;  // src/ui/virtual_pendrives_dialog.h
class RekordboxModePanel;      // rekordbox PC mode (src/ui/rekordbox_mode_panel.h)
class TrakcratePanel;
class TrakcrateTrackEditor;
class TrakcrateTrackEditor;           // trakcrate library pane (src/ui/trakcrate_panel.h)
struct RbModeOptions;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void setDebugModeFromCli();
    void setLinkExport(const QString& folder);        // --link-export DIR: open that rekordbox folder in the browser and export it
    void setLinkExportEnabled(bool on);               // the Network menu checkbox: export the database the browser shows
    void applyLinkExport();
    void startRekordboxMode(const RbModeOptions& opts);   // --rekordbox-mode: open the panel and start serving
    void addDemoDecks();                             // placeholder remote decks (--mock-beat)
    AlsaClock* alsaClock() const { return clock_; }   // the VDJ's ALSA sequencer (MIDI clock)

protected:
    void closeEvent(QCloseEvent* event) override;
    void shutdown();

private slots:
    void connectToNetwork();
    void disconnectFromNetwork();
    // What leaving the link now would do to the sound the audience hears: a deck playing a track off our virtual pendrive, or
    // we are the master the other decks follow. Empty when nothing is affected (or we are not connected).
    QStringList liveImpact();
    bool confirmLeavingLink(const QString& action);   // asks when liveImpact() is not empty; false = the user cancelled

private:
    void openRekordboxFolder();
    // virtual / physical pendrives (src/export/pendrive_registry.h)
    PendriveRegistry* pendrives();
    void showVirtualPendrives();
    void rebuildExportMenu();
    void openPendrive(const QString& folder);
    void exportPendrive(const QString& folder, const QString& name);   // swap the exported pendrive like a deck would
    void ejectPendrive();
    void pickAdHocFolder();
    void setupMenus();
    void setupStatusBar();
    void setupShortcuts();
    void setupDemoDecks();
    void setupDemoTrackBrowser();
    void startDemoClocks();
    void setupDebugPanel();
    void connectDeckSignals();
    void setDebugMode(bool enabled);
    void updateDeckOverlays(int pid, double bpm, int pitch, int flags,
                            int masterState, int active);
    void nudgeLocalBpm(double delta);
    bool setVdjBpm(double bpm);                  // user changes: refused while synced to a real deck
    bool applyVdjBpm(double bpm);                // the one place the tempo is applied (clock, link, rows)
    int bpmLockedTo() const;                     // the deck whose BPM we must follow (difflock), 0 = free
    void followLockedBpm();                      // adopt the locked deck's BPM
    void setVdjPlaying(bool playing);
    void vdjRestart(bool quantize);                 // MIDI stop / start at the bar start; quantized: on the sync deck's next downbeat
    void cancelPendingRestart();
    void cancelPendingStart();
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
    void applyMidiMap();
    void checkDjtoolzControllerConnected();
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
    QAction* autoIpMenuAction_ = nullptr;
    QMenu* deckFilesMenu_ = nullptr;
    NetworkManager* network_;
    AlsaClock* clock_ = nullptr;
    DiffLock difflock_;
    AutoIpManager* autoIp_;
    ConfigManager* config_;
    QTimer* startTimer_ = nullptr;              // single shot: a quantized Play waiting for the sync deck's beat
    int pendingStartPid_ = 0;
    bool staleWarned_ = false;
    QTimer* restartTimer_ = nullptr;            // single shot: a quantized restart waiting for its moment
    DeckPhase deckPhase_;                       // where the other decks are in their beat / bar (fed from their beat packets)
    QuantizeSettings* quantize_ = nullptr;      // the DJ's global quantize resolution
    NfsMountManager* nfs_;
    TrackBrowser* trackBrowser_;
    PlayerPanel* playerPanel_ = nullptr;
    DjtoolzPanel* djtoolzPanel_ = nullptr;
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
    // rekordbox PC mode: the dock is created on first use
    QDockWidget* rekordboxDock();
    RekordboxModePanel* rbPanel_ = nullptr;
    QDockWidget* rbDock_ = nullptr;
    QAction* rbAction_ = nullptr;
    // trakcrate library pane: the dock is created on first use
    QDockWidget* trakcrateDock();
    TrakcratePanel* tcPanel_ = nullptr;
    QDockWidget* tcDock_ = nullptr;
    TrakcrateTrackEditor* tcEditor_ = nullptr;
    QDockWidget* tcEditorDock_ = nullptr;
    QAction* tcAction_ = nullptr;
    QAction* linkExportAction_ = nullptr;
    QMenu* exportMenu_ = nullptr;
    PendriveToolBar* pendriveBar_ = nullptr;
    void updatePendriveBar();                       // exported marker and the USB STOP dimming
    PendriveRegistry* pendrives_ = nullptr;
    VirtualPendrivesDialog* pendrivesDialog_ = nullptr;
    QString explicitExportFolder_;                    // a pendrive picked on purpose; empty = export what the browser shows
    QString explicitExportName_;
    bool linkExportWanted_ = false;
    QAction* connectAction_ = nullptr;
    AudioRecorder* recorder_ = nullptr;
    RecordingPanel* recordingPanel_ = nullptr;
    MidiRouter* midiRouter_ = nullptr;
    InstrumentManager* instruments_ = nullptr;
    MidiMonitorDialog* midiMonitor_ = nullptr;
    DjtoolzMidiControl* djtoolzControl_ = nullptr;
    QElapsedTimer followersClock_;       // throttles the ALSA subscriber query
    QStringList followerNames_;
    std::map<int, BeatTempo> beatTempo_;   // per deck: tempo measured from the beat packets
    MidiMapInput* midiIn_ = nullptr;
    JoystickSeqBridge* joystick_ = nullptr;
    QLabel* statusDecks_;
    QTimer* demoClock_;
    QTimer* sequencerTimer_ = nullptr;
    QElapsedTimer demoElapsed_;
    QVector<int> deckOrder_; // player ids in the deck area, top = VDJ
    int activeDeckIndex_ = 0;
    double masterBpm_ = 0.0;
    int masterPid_ = -1;
    QString currentInterface_;
    void applyAppFont(const QString& family, int pointSize);   // Config > Application Font; saved as [ui] font_family / font_size
    void chooseAppFont();
    void resetAppFont();
    QFont defaultAppFont_;
    void offerPort111Capability();                  // popup: setcap cap_net_bind_service on our binary via pkexec, then restart
    bool port111Offered_ = false;
    void setMasterStatus(int pid);                  // status bar "Master: ..." label, pid < 0 = none
    bool savedInterfaceUsable() const;              // saved NIC exists on this machine: no need to ask
    void forgetInterface();                         // Config menu: clears the saved NIC so the next Connect / AutoIP asks again
    void rememberInterface(const QString& iface);   // sets currentInterface_ and persists it as the default
};

} // namespace vdj

#endif // MAIN_WINDOW_H
