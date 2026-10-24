#include "main_window.h"
#include "deck_area.h"
#include "deck_widget.h"
#include "beat_grid_widget.h"
#include "toolbar.h"
#include "dark_theme.h"
#include "network/network_manager.h"
#include "network/auto_ip_manager.h"
#include <cdj/cdj.h>
#include "network/network_config_dialog.h"
#include "keyboard_shortcuts.h"
#include "pitch_slider.h"
#include "deck_controls.h"
#include "usb_config_dialog.h"
#include "joystick_config_dialog.h"
#include "config/config_manager.h"
#include "nfs/nfs_mount_manager.h"
#include "track_browser.h"
#include "now_playing.h"
#include "debug_panel.h"
#include "debug/packet_logger.h"
#include "debug_overlay.h"
#include "player_panel.h"
#include "autoip_status_widget.h"
#include "bpm_ramp_widget.h"
#include "hud_window.h"
#include "recording_panel.h"
#include "pendrive_clone_dialog.h"
#include "instrument_config_dialog.h"
#include "audio/audio_recorder.h"
#include "midi/instrument_manager.h"
#include "midi/midi_map_input.h"
#include "midi/midi_router.h"
#include "midi/alsa_clock.h"
#include "diff_lock_bar.h"
#include <cmath>

#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QCoreApplication>
#include <QMenu>
#include <QStatusBar>
#include <QLabel>
#include <QKeySequence>
#include <QAction>
#include <QShortcut>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QSplitter>
#include <QVBoxLayout>
#include <QDir>
#include <QDockWidget>
#include <QCloseEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QNetworkInterface>
#include <QFile>

namespace vdj {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("VDJ UI");
    setMinimumWidth(1400);
    // Open at a height that fits the available screen: with the title bar,
    // menu, toolbar and status bar the full window must not overflow the
    // display (a 768px high laptop panel would clip the status bar).
    QScreen* screen = QGuiApplication::primaryScreen();
    int maxH = 750;
    if (screen) {
        const int availH = screen->availableGeometry().height();
        maxH = qMax(500, qMin(750, availH - 100));
    }
    resize(1400, maxH);
    setStyleSheet(darkThemeStylesheet());

    clock_ = new AlsaClock;
    {
        QString clockErr;
        if (!clock_->open(QStringLiteral("vdj-ui"), &clockErr))
            qWarning() << "ALSA clock:" << clockErr;
        else
            clock_->setBpm(128.0);
    }
    network_ = new NetworkManager(this);
    // time every deck beat against the sequencer's beat as the packet arrives (for difflock)
    network_->setBeatProbe([this](double* ms) { return clock_ && clock_->beatOffsetMs(ms); });
    autoIp_ = new AutoIpManager(this);
    config_ = new ConfigManager(this);
    currentInterface_ = config_->loadValue(QStringLiteral("network"), QStringLiteral("interface"));   // last NIC used for prolink
    nfs_ = new NfsMountManager(this);
    trackBrowser_ = nullptr; // created in setupDemoDecks
    nowPlaying_ = nullptr;
    QObject::connect(network_, &NetworkManager::connected, this, [this]() {
        statusNetwork_->setText("Network: connected");
        setToolbarConnected(true);
        updateRampEnabled();
        // The VDJ row takes the player id the link gave us; placeholder decks go, real ones announce themselves.
        const int localId = int(network_->localPlayerId());
        for (int i = deckArea_->deckCount() - 1; i >= 0; --i) {
            DeckWidget* d = deckArea_->deckAt(i);
            if (d && !isVdjRow(d)) deckArea_->removeDeck(d->playerId());
        }
        for (int i = 0; i < deckArea_->deckCount(); ++i)
            if (deckArea_->deckAt(i)->model() == QLatin1String("VDJ")) {
                deckArea_->deckAt(i)->setPlayerId(localId);
                nowPlaying_->setDeck(localId);
            }
        deckOrder_ = { localId };
        statusDecks_->setText(QString("Decks: %1").arg(deckArea_->deckCount()));
        // Show our own address on the local VDJ deck row.
        if (auto* local = deckAt(int(network_->localPlayerId())))
            local->setIp(network_->ipForPlayer(int(network_->localPlayerId())));
    });
    QObject::connect(network_, &NetworkManager::disconnected, this, [this]() {
        statusNetwork_->setText("Network: down");
        setToolbarConnected(false);
        updateRampEnabled();
        for (int i = deckArea_->deckCount() - 1; i >= 0; --i) {      // the other decks are no longer visible
            DeckWidget* d = deckArea_->deckAt(i);
            if (d && !d->isRemote() && d->model() == QLatin1String("VDJ")) d->setPlayerId(5);
            else if (d) deckArea_->removeDeck(d->playerId());
        }
        deckOrder_ = { 5 };
        statusDecks_->setText(QString("Decks: %1").arg(deckArea_->deckCount()));
        if (autoIp_ && autoIp_->active())
            autoIp_->stop();
    });
    QObject::connect(network_, &NetworkManager::errorOccurred, this,
                     [this](const QString& msg) {
        statusNetwork_->setText("Network: " + msg);
    });
    QObject::connect(autoIp_, &AutoIpManager::started, this,
                     [this](const QString& iface, const QString& ip) {
        statusBar()->showMessage(
            QStringLiteral("AutoIP claimed %1 on %2").arg(ip).arg(iface), 5000);
    });
    QObject::connect(autoIp_, &AutoIpManager::stopped, this,
                     [this]() {
        setAutoIpChecked(false);
    });
    QObject::connect(autoIp_, &AutoIpManager::errorOccurred, this,
                     [this](const QString& msg) {
        statusBar()->showMessage(msg, 5000);
        setAutoIpChecked(false);
        QMessageBox::warning(this, QStringLiteral("AutoIP"), msg);
    });
    QObject::connect(network_, &NetworkManager::masterChanged, this, [this](int pid) {
        statusMaster_->setText(QString("Master: %1").arg(pid));
        masterPid_ = pid;
        // statusUpdated only carries other players' packets, so the local deck's master state comes from here
        if (auto* local = deckAt(int(network_->localPlayerId())))
            local->setMaster(pid == int(network_->localPlayerId()));
        updateRampEnabled();
    });
    QObject::connect(network_, &NetworkManager::statusUpdated, this,
                     [this](int pid, double bpm, int pitch, int flags, int masterState, int active) {
        auto* deck = deckAt(pid);
        if (deck) {
            bool playing = (flags & CDJ_STAT_FLAG_PLAY) != 0;
            deck->setPlaying(playing);
            deck->setMaster(masterState != 0);
            deck->setSync((flags & CDJ_STAT_FLAG_SYNC) != 0);
            if (bpm > 0)
                deck->setBpm(bpm);
            if (playing && bpm > 0 && masterState)
                masterBpm_ = bpm;
        }
        updateDeckOverlays(pid, bpm, pitch, flags, masterState, active);
        if (flags & CDJ_STAT_FLAG_MASTER) {
            difflock_.masterChanged(pid);              // follows the master when that is on
            refreshDiffLock();
        }
    });
    QObject::connect(network_, &NetworkManager::beatReceived, this,
                     [this](int pid, double bpm, int barPos, double sequencerOffsetMs) {
        if (!std::isnan(sequencerOffsetMs) && !isVdjDeck(pid) && clock_ && clock_->running()) {
            const int nudge = difflock_.onBeat(pid, barPos, sequencerOffsetMs);
            if (nudge) clock_->nudgeMillis(nudge);
            if (pid == difflock_.player()) refreshDiffLock();
        }
        for (int i = 0; i < deckArea_->deckCount(); ++i) {
            auto* deck = deckArea_->deckAt(i);
            if (deck && deck->playerId() == pid) {
                deck->setBpm(bpm);
                deck->setBarPos(barPos);
                // Record the beat's arrival time so the playhead sweeps
                // across the bar between beats. Without this reference the
                // grid would only step between beat cells as bar_pos changes
                // and the timeline never moves.
                deck->setBeatTimestamp(QDateTime::currentMSecsSinceEpoch());
            }
        }
        if (nowPlaying_)
            nowPlaying_->setBpm(bpm);
    });
    QObject::connect(network_, &NetworkManager::deckJoined, this,
                     [this](int pid, const QString& ip, const QString& model) {
        if (isVdjDeck(pid) || pid == int(network_->localPlayerId()))
            return;                          // our own announcement: the VDJ row already stands for us
        auto* deck = deckArea_->addDeck(pid);
        if (deck) {
            deck->setModel(model);
            deck->setIp(ip);
            deck->setRemote(!isLocalDeck(pid));
        }
        statusDecks_->setText(QString("Decks: %1").arg(deckArea_->deckCount()));
    });
    QObject::connect(network_, &NetworkManager::deckLeft, this, [this](int pid) {
        deckArea_->removeDeck(pid);
        if (difflock_.player() == pid) { difflock_.off(); }
        refreshDiffLock();
        statusDecks_->setText(QString("Decks: %1").arg(deckArea_->deckCount()));
        if (nfs_->isMounted(pid)) {
            nfs_->unmountPlayer(pid);
        }
        if (nowPlaying_ && nowPlaying_->deck() == pid) {
            nowPlaying_->setNfsStatus(false);
        }
        statusBar()->showMessage(QStringLiteral("Deck %1 left the network").arg(pid), 3000);
    });

    QObject::connect(nfs_, &NfsMountManager::mounted, this, [this](int pid, const QString& mp) {
        if (trackBrowser_) {
            trackBrowser_->setPlayerId(pid);
            trackBrowser_->setMount(mp);
        }
        if (nowPlaying_) nowPlaying_->setNfsStatus(true);
        statusBar()->showMessage(QStringLiteral("NFS mounted for deck %1").arg(pid), 3000);
    });
    QObject::connect(nfs_, &NfsMountManager::mountFailed, this, [this](int pid, const QString& err) {
        statusBar()->showMessage(QStringLiteral("NFS mount failed (deck %1): %2").arg(pid).arg(err), 5000);
        if (nowPlaying_) nowPlaying_->setNfsStatus(false);
    });

    setupMenus();
    setupToolbar();
    setupStatusBar();
    setupShortcuts();
    setupDemoDecks();
    setupDemoTrackBrowser();
    connectDeckSignals();
    startDemoClocks();
    sequencerTimer_ = new QTimer(this);
    sequencerTimer_->setInterval(16);
    connect(sequencerTimer_, &QTimer::timeout, this, &MainWindow::updateSequencerPlayhead);
    sequencerTimer_->start();
    setupKeepalive();
    setupDebugPanel();
    setupRecording();
    setupMidi();
}

MainWindow::~MainWindow()
{
    if (midiIn_) midiIn_->stop();
    delete clock_;
}

void MainWindow::connectDeckSignals()
{
    QObject::connect(trackBrowser_, &TrackBrowser::trackSelected, this,
                     [this](int pid, const QString& title, const QString& artist,
                            const QString& bpm, const QString& key) {
        if (nowPlaying_) {
            nowPlaying_->setDeck(pid);
            nowPlaying_->setTrack(title, artist, bpm, key);
        }
        if (hud_) hud_->setTrackInfo(pid, title, artist);
        statusBar()->showMessage(QStringLiteral("Now playing: %1").arg(title), 3000);
    });
    QObject::connect(trackBrowser_, &TrackBrowser::previewRequested, playerPanel_, &PlayerPanel::loadTrack);
    QObject::connect(playerPanel_, &PlayerPanel::status, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 5000);
    });
    QObject::connect(deckArea_, &DeckArea::playPauseClicked, this, [this](int pid, bool play) {
        // Reflect transport state so the demo clock / beat grid actually runs
        // for the local deck; on a live link the next status packet keeps it
        // in sync anyway.
        if (auto* deck = deckAt(pid))
            deck->setPlaying(play);
        network_->setLocalPlaying(play);
        if (isVdjDeck(pid)) setVdjPlaying(play);
        applyPitchFromDeck(pid);
    });
    QObject::connect(deckArea_, &DeckArea::syncClicked, this, [this](int pid) {
        if (isVdjDeck(pid)) syncToMaster();
        else lockDiffLockTo(pid);                      // Sync on another deck: difflock the sequencer to it
    });
    QObject::connect(deckArea_, &DeckArea::setBpmClicked, this, [this](int pid, double bpm) {
        setVdjBpm(bpm);
        markDeckBpm(pid, bpm);
    });
    QObject::connect(deckArea_, &DeckArea::becomeMasterClicked, this, [this](int) {
        network_->requestMaster();
    });
    QObject::connect(deckArea_, &DeckArea::bpmFromMasterClicked, this, [this](int pid) {
        copyBpmFromMaster(pid);
    });
    QObject::connect(deckArea_, &DeckArea::broadcastBeatClicked, this,
                     [this](int pid, double bpm, int barPos) {
        network_->broadcastBeat(bpm, barPos);
        auto* deck = deckAt(pid);
        if (deck) deck->setBarPos(barPos);
    });
    QObject::connect(deckArea_, &DeckArea::deckClicked, this, [this](int pid) {
        for (int i = 0; i < deckArea_->deckCount(); ++i) {
            if (deckArea_->deckAt(i)->playerId() == pid) {
                activeDeckIndex_ = i;
                break;
            }
        }
    });
    QObject::connect(deckArea_, &DeckArea::pitchChanged, this, [this](int pid, double pct) {
        network_->setLocalPitch(pct);
        applyPitchFromDeck(pid);
    });
}

void MainWindow::openRekordboxFolder()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select rekordbox folder (contains Contents and PIONEER)"),
        QStringLiteral("/var/tmp"));
    if (dir.isEmpty())
        return;
    QString err;
    if (!trackBrowser_->openRekordboxFolder(dir, &err)) {
        QMessageBox::warning(this, QStringLiteral("Open Rekordbox Folder"), err);
        return;
    }
    statusBar()->showMessage(trackBrowser_->hasPdb()
        ? QStringLiteral("Loaded rekordbox folder %1").arg(dir)
        : QStringLiteral("Loaded %1 (no PIONEER/rekordbox/export.pdb found; showing Contents only)").arg(dir), 6000);
}

void MainWindow::setupMenus()
{
    QMenu* file = menuBar()->addMenu("&File");
    QAction* openDb = file->addAction("&Open Rekordbox Folder...");
    openDb->setShortcut(QKeySequence::Open);
    connect(openDb, &QAction::triggered, this, &MainWindow::openRekordboxFolder);
    QAction* cloneAction = file->addAction("&Clone Pendrive...");
    connect(cloneAction, &QAction::triggered, this, [this]() {
        PendriveCloneDialog dlg(this);
        dlg.exec();
    });
    file->addSeparator();
    QAction* quit = file->addAction("&Quit");
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QMainWindow::close);

    QMenu* view = menuBar()->addMenu("&View");
    QAction* fullscr = view->addAction("Full&screen");
    fullscr->setShortcut(Qt::Key_F11);
    connect(fullscr, &QAction::triggered, this, [this]() {
        if (isFullScreen()) showNormal();
        else showFullScreen();
    });

    hudAction_ = view->addAction("&HUD Window");
    hudAction_->setShortcut(Qt::CTRL | Qt::Key_H);
    hudAction_->setCheckable(true);
    connect(hudAction_, &QAction::toggled, this, &MainWindow::showHud);

    recordAction_ = view->addAction("&Recording Panel");
    recordAction_->setCheckable(true);

    QAction* debugAction = view->addAction("&Debug Panel");
    debugAction->setShortcut(Qt::CTRL | Qt::Key_D);
    debugAction->setCheckable(true);
    connect(debugAction, &QAction::toggled, this, &MainWindow::setDebugMode);

    QMenu* network = menuBar()->addMenu("&Network");
    QAction* connectAction = network->addAction("&Connect");
    QAction* disconnectAction = network->addAction("&Disconnect");
    connect(connectAction, &QAction::triggered, this, &MainWindow::connectToNetwork);
    connect(disconnectAction, &QAction::triggered, this, &MainWindow::disconnectFromNetwork);

    network->addSeparator();
    // The players serve NFS v2 over UDP, which the kernel cannot mount: files are read with libcdj's own client
    deckFilesMenu_ = network->addMenu("&Browse Deck Files");
    connect(deckFilesMenu_, &QMenu::aboutToShow, this, &MainWindow::populateDeckFilesMenu);
    network->addSeparator();
    autoIpMenuAction_ = network->addAction("&AutoIP...");
    autoIpMenuAction_->setCheckable(true);
    autoIpMenuAction_->setToolTip("Claim a link-local address on the interface connected to the decks");
    connect(autoIpMenuAction_, &QAction::triggered, this, &MainWindow::setAutoIp);

    QMenu* configMenu = menuBar()->addMenu("&Config");
    QAction* usbAction = configMenu->addAction("&USB / MIDI...");
    QAction* instAction = configMenu->addAction("&Instruments...");
    QAction* mapAction = configMenu->addAction("MIDI &Map...");
    QAction* joyAction = configMenu->addAction("&Joystick...");
    connect(instAction, &QAction::triggered, this, [this]() {
        if (!instruments_) return;
        InstrumentConfigDialog dlg(instruments_, config_, this);
        dlg.exec();
        instruments_->autowire();
    });
    connect(mapAction, &QAction::triggered, this, &MainWindow::chooseMidiMap);
    connect(usbAction, &QAction::triggered, this, [this]() {
        UsbConfigDialog dlg(config_, this);
        dlg.exec();
    });
    connect(joyAction, &QAction::triggered, this, [this]() {
        JoystickConfigDialog dlg(config_, this);
        dlg.exec();
    });

    QMenu* help = menuBar()->addMenu("&Help");
    QAction* aboutAction = help->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, QStringLiteral("About VDJ UI"),
            QStringLiteral("<h3>VDJ UI %1</h3>"
                           "<p>A Qt GUI for a virtual DJ on the Pioneer ProLink network: beat grids of all decks, "
                           "rekordbox track browsing and a local track preview player.</p>")
                .arg(QCoreApplication::applicationVersion()));
    });
}

void MainWindow::setupToolbar()
{
    toolbar_ = new Toolbar(this);
    addToolBar(Qt::TopToolBarArea, toolbar_);
    connect(toolbar_, &Toolbar::connectTriggered, this, &MainWindow::connectToNetwork);
    connect(toolbar_, &Toolbar::disconnectTriggered, this, &MainWindow::disconnectFromNetwork);
    connect(toolbar_, &Toolbar::playPauseTriggered, this, [this]() {
        auto* deck = deckArea_->deckAt(activeDeckIndex_);
        if (!deck) return;
        bool playing = !deck->isPlaying();
        deck->setPlaying(playing);
        network_->setLocalPlaying(playing);
        if (isVdjDeck(deck->playerId())) setVdjPlaying(playing);
    });
    connect(toolbar_, &Toolbar::syncTriggered, this, [this]() {
        syncToMaster();
    });
    connect(toolbar_, &Toolbar::bpmUpTriggered, this, [this]() {
        nudgeLocalBpm(1.0);
    });
    connect(toolbar_, &Toolbar::bpmDownTriggered, this, [this]() {
        nudgeLocalBpm(-1.0);
    });
}

void MainWindow::setAutoIp(bool on)
{
    if (!on) {
        autoIp_->stop();
        setAutoIpChecked(false);
        return;
    }
    // AutoIP must run on the NIC that is cabled to the decks, so always ask
    const QString iface = NetworkConfigDialog::chooseInterface(this, QStringLiteral("AutoIP"), activeInterface());
    if (iface.isEmpty()) {
        setAutoIpChecked(false);
        return;
    }
    rememberInterface(iface);
    setAutoIpChecked(true);
    statusBar()->showMessage(QStringLiteral("AutoIP: waiting for an address on %1...").arg(iface), 5000);
    autoIp_->start(iface);
}

void MainWindow::setAutoIpChecked(bool on)
{
    if (autoIpMenuAction_) autoIpMenuAction_->setChecked(on);
}

void MainWindow::populateDeckFilesMenu()
{
    deckFilesMenu_->clear();
    int count = 0;
    for (int i = 0; i < deckArea_->deckCount(); ++i) {
        auto* deck = deckArea_->deckAt(i);
        if (!deck || isLocalDeck(deck->playerId()))
            continue;
        const int pid = deck->playerId();
        const QString ip = network_->ipForPlayer(pid);
        if (ip.isEmpty())
            continue;
        QAction* a = deckFilesMenu_->addAction(QStringLiteral("Deck %1  %2  (%3)").arg(pid).arg(deck->model(), ip));
        connect(a, &QAction::triggered, this, [this, pid, ip]() { browseDeck(pid, ip); });
        ++count;
    }
    if (count == 0) {
        QAction* none = deckFilesMenu_->addAction(QStringLiteral("No connected decks"));
        none->setEnabled(false);
    }
}

void MainWindow::browseDeck(int playerId, const QString& ip)
{
    statusBar()->showMessage(QStringLiteral("Reading the rekordbox database of deck %1 (%2)...").arg(playerId).arg(ip));
    QString err;
    if (!trackBrowser_->openRemoteDeck(playerId, ip, &err)) {
        if (err.isEmpty()) {
            statusBar()->clearMessage();
            return;
        }
        statusBar()->showMessage(QStringLiteral("Deck %1: %2").arg(playerId).arg(err), 8000);
        QMessageBox::warning(this, QStringLiteral("Browse Deck Files"), QStringLiteral("Deck %1 (%2): %3").arg(playerId).arg(ip, err));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Showing the tracks of deck %1 (%2)").arg(playerId).arg(ip), 6000);
}

DiffLockBar* MainWindow::vdjDiffLockBar() const
{
    for (int i = 0; i < deckArea_->deckCount(); ++i)
        if (isVdjRow(deckArea_->deckAt(i)))
            return deckArea_->deckAt(i)->diffLockBar();
    return nullptr;
}

void MainWindow::lockDiffLockTo(int playerId)
{
    DeckWidget* deck = deckAt(playerId);
    if (!deck || isVdjRow(deck))
        return;
    if (auto* bar = vdjDiffLockBar())
        difflock_.setTargetMs(bar->targetMs());
    difflock_.lock(playerId);
    refreshDiffLock();
    statusBar()->showMessage(QStringLiteral("Difflock: sequencer locked to deck %1, %2 ms%3")
        .arg(playerId).arg(difflock_.targetMs())
        .arg(clock_ && clock_->running() ? QString() : QStringLiteral(" (applies once the VDJ is playing)")), 4000);
}

void MainWindow::diffLockOff()
{
    difflock_.off();
    refreshDiffLock();
    statusBar()->showMessage(QStringLiteral("Difflock off"), 3000);
}

void MainWindow::setDiffLockFollowMaster(bool on)
{
    difflock_.setFollowMaster(on);
    refreshDiffLock();
    statusBar()->showMessage(on ? QStringLiteral("Difflock follows the master") : QStringLiteral("Difflock no longer follows the master"), 3000);
}

// adj_nudge_millis: the next beat is that much longer / shorter. While locked the target moves with the nudge,
// otherwise the lock would take it straight back out.
void MainWindow::nudgeSequencer(int ms)
{
    if (!clock_ || !clock_->running()) {
        statusBar()->showMessage(QStringLiteral("Nudge: the VDJ sequencer is not running"), 3000);
        return;
    }
    clock_->nudgeMillis(ms);
    if (difflock_.locked()) {
        difflock_.nudgeTarget(-ms);
        if (auto* bar = vdjDiffLockBar()) bar->setTargetMs(difflock_.targetMs());
    }
    refreshDiffLock();
}

void MainWindow::captureDiffLockTarget()
{
    if (!difflock_.locked() || !difflock_.hasAverage(difflock_.player())) {
        statusBar()->showMessage(QStringLiteral("Capture: lock to a playing deck first, the offset needs a few beats to measure"), 4000);
        return;
    }
    difflock_.setTargetMs(int(std::lround(difflock_.avgDiff(difflock_.player()))));
    if (auto* bar = vdjDiffLockBar()) bar->setTargetMs(difflock_.targetMs());
    refreshDiffLock();
}

void MainWindow::refreshDiffLock()
{
    for (int i = 0; i < deckArea_->deckCount(); ++i) {
        DeckWidget* d = deckArea_->deckAt(i);
        if (d && !isVdjRow(d))
            d->controls()->setSyncLocked(difflock_.locked() && d->playerId() == difflock_.player());
    }
    DiffLockBar* bar = vdjDiffLockBar();
    if (!bar) return;
    bar->setFollowMaster(difflock_.followMaster());
    if (bar->targetMs() != difflock_.targetMs()) bar->setTargetMs(difflock_.targetMs());
    QString text;
    if (!difflock_.locked()) {
        text = difflock_.followMaster() ? QStringLiteral("off, waiting for a master") : QStringLiteral("off");
    } else {
        const int p = difflock_.player();
        text = QStringLiteral("%1 deck %2").arg(difflock_.followMaster() ? QStringLiteral("master") : QStringLiteral("locked to")).arg(p);
        text += difflock_.hasAverage(p)
            ? QStringLiteral("  %1 ms (avg %2)").arg(difflock_.lastDiff(p), 0, 'f', 0).arg(difflock_.avgDiff(p), 0, 'f', 1)
            : QStringLiteral("  measuring...");
        if (!clock_ || !clock_->running()) text += QStringLiteral("  (VDJ stopped)");
    }
    bar->setStatus(text);
}

void MainWindow::setToolbarConnected(bool connected)
{
    if (toolbar_)
        toolbar_->setConnectedState(connected);
}

QString MainWindow::activeInterface() const
{
    if (!currentInterface_.isEmpty())
        return currentInterface_;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const auto& iface : ifaces) {
        if (iface.flags().testFlag(QNetworkInterface::IsUp) &&
            !iface.flags().testFlag(QNetworkInterface::IsLoopBack))
            return iface.name();
    }
    return QString();
}

void MainWindow::setupStatusBar()
{
    statusNetwork_ = new QLabel("Network: down", this);
    statusMaster_ = new QLabel("Master: none", this);
    statusAutoIp_ = new AutoipStatusWidget(autoIp_, this);
    statusDecks_ = new QLabel("Decks: 0", this);
    statusBar()->addPermanentWidget(statusAutoIp_);
    statusBar()->addPermanentWidget(statusNetwork_);
    statusBar()->addPermanentWidget(statusMaster_);
    statusBar()->addPermanentWidget(statusDecks_);
    statusBar()->showMessage("Ready");
}

void MainWindow::setupShortcuts()
{
    auto* shortcuts = new KeyboardShortcuts(this, this);
    connect(shortcuts, &KeyboardShortcuts::playPauseRequested, this, [this]() {
        auto* deck = deckArea_->deckAt(activeDeckIndex());
        if (deck) {
            bool playing = !deck->isPlaying();
            deck->setPlaying(playing);
            network_->setLocalPlaying(playing);
            if (isVdjDeck(deck->playerId())) setVdjPlaying(playing);
        }
    });
    connect(shortcuts, &KeyboardShortcuts::syncRequested, this, [this]() {
        syncToMaster();
    });
    connect(shortcuts, &KeyboardShortcuts::becomeMasterRequested, this, [this]() {
        network_->requestMaster();
    });
    connect(shortcuts, &KeyboardShortcuts::diffLockPlayerRequested, this, &MainWindow::lockDiffLockTo);
    connect(shortcuts, &KeyboardShortcuts::diffLockOffRequested, this, &MainWindow::diffLockOff);
    connect(shortcuts, &KeyboardShortcuts::diffLockFollowMasterRequested, this,
            [this]() { setDiffLockFollowMaster(!difflock_.followMaster()); });
    connect(shortcuts, &KeyboardShortcuts::diffLockTargetNudgeRequested, this, [this](int d) {
        difflock_.nudgeTarget(d);
        refreshDiffLock();
    });
    connect(shortcuts, &KeyboardShortcuts::copyBpmRequested, this, [this]() {
        copyBpmFromMaster(activeDeckId());
    });
    connect(shortcuts, &KeyboardShortcuts::cycleFocusRequested, this, [this]() {
        cycleActiveDeck();
    });
    connect(shortcuts, &KeyboardShortcuts::pitchNudgeRequested, this, [this](double delta) {
        int idx = activeDeckIndex();
        DeckWidget* deck = deckArea_->deckAt(idx);
        if (!deck) return;
        double pct = deck->pitchPercentage() + delta;
        deck->setPitchPercentage(pct);
        network_->setLocalPitch(pct);
        applyPitchFromDeck(activeDeckId());
    });
}

void MainWindow::setupKeepalive()
{
    keepaliveTimer_ = new QTimer(this);
    keepaliveTimer_->setInterval(1000);
    connect(keepaliveTimer_, &QTimer::timeout, this, [this]() {
        if (network_ && network_->isConnected()) {
            network_->sendKeepalive();
        }
    });
    keepaliveTimer_->start();
}

void MainWindow::setupDemoDecks()
{
    deckArea_ = new DeckArea(this);
    deckArea_->setMinimumWidth(1000);
    trackBrowser_ = new TrackBrowser(this);
    nowPlaying_ = new NowPlaying(this);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(deckArea_);
    splitter->addWidget(trackBrowser_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>() << 1000 << 400);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    playerPanel_ = new PlayerPanel(this);
    centralLayout->addWidget(playerPanel_);
    bpmRamp_ = new BpmRampWidget(this);
    centralLayout->addWidget(bpmRamp_);
    connect(bpmRamp_, &BpmRampWidget::rampRequested, this, &MainWindow::startBpmRamp);
    connect(bpmRamp_, &BpmRampWidget::bpmNudged, this, [this](double d) { nudgeLocalBpm(d); });
    centralLayout->addWidget(splitter, 1);
    centralLayout->addWidget(nowPlaying_);
    setCentralWidget(central);

    auto* master = deckArea_->addDeck(5);
    master->setModel(QStringLiteral("VDJ"));
    master->setBpm(128.00);
    // Paused at boot: clicking Play starts the demo clock/timeline for it.
    master->setPlaying(false);
    if (auto* bar = master->diffLockBar()) {
        bar->setTargetMs(difflock_.targetMs());
        connect(bar, &DiffLockBar::targetChanged, this, [this](int ms) { difflock_.setTargetMs(ms); });
        connect(bar, &DiffLockBar::followMasterToggled, this, &MainWindow::setDiffLockFollowMaster);
        connect(bar, &DiffLockBar::offClicked, this, &MainWindow::diffLockOff);
        connect(bar, &DiffLockBar::captureClicked, this, &MainWindow::captureDiffLockTarget);
        connect(bar, &DiffLockBar::nudgeClicked, this, &MainWindow::nudgeSequencer);
    }
    // Only the VDJ is always there; other decks appear when they join the link.
    deckOrder_ = { 5 };

    statusDecks_->setText("Decks: 1");
    statusMaster_->setText("Master: none");

    nowPlaying_->setDeck(5);

    demoClock_ = new QTimer(this);
    demoClock_->setInterval(16); // ~60fps
    connect(demoClock_, &QTimer::timeout, this, [this]() {
        // Only simulate a beat stream when not connected to a real ProLink
        // network; otherwise real deck timestamps/bar positions drive the
        // grid and the demo would overwrite them.
        if (network_ && network_->isConnected())
            return;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 elapsedTotal = demoElapsed_.elapsed();
        for (int i = 0; i < deckArea_->deckCount(); ++i) {
            auto* deck = deckArea_->deckAt(i);
            if (!deck || !deck->isPlaying())
                continue;
            double beatMs = 60000.0 / deck->bpm();
            qint64 barMs = qint64(4.0 * beatMs);
            if (barMs <= 0)
                continue;
            // Simulate a beat-accurate stream: drive bar position from
            // elapsed time so the marker crosses all 4 beats per bar.
            double frac = (elapsedTotal % barMs) / double(barMs); // 0..1 in bar
            int barIndex = int(frac * 4.0);                        // 0..3
            double beatPhase = (frac * 4.0) - barIndex;            // 0..1 in beat
            deck->setBarPos(barIndex + 1);                          // 1..4
            deck->setBeatTimestamp(now - qint64(beatPhase * beatMs));
        }
    });
}

void MainWindow::setupDemoTrackBrowser()
{
    // browse a local folder so the panel renders without a CDJ connected
    QString music = QDir::homePath() + "/Music";
    if (!QDir(music).exists()) {
        music = QDir::homePath();
    }
    trackBrowser_->setPlayerId(5);
    trackBrowser_->setMount(music);
}

// Placeholder decks for trying the layout without a network (--mock-beat).
void MainWindow::addDemoDecks()
{
    for (int pid : { 1, 2 }) {
        auto* d = deckArea_->addDeck(pid);
        d->setRemote(true);
        d->setBpm(128.00);
        d->beatGrid()->startClock();
        deckOrder_.append(pid);
    }
    statusDecks_->setText(QString("Decks: %1").arg(deckArea_->deckCount()));
}

void MainWindow::startDemoClocks()
{
    demoElapsed_.start();
    demoClock_->start();
    for (int i = 0; i < deckArea_->deckCount(); ++i) {
        auto* deck = deckArea_->deckAt(i);
        if (deck) deck->beatGrid()->startClock();
    }
}

void MainWindow::connectToNetwork()
{
    NetworkConfigDialog dialog(this);
    if (!currentInterface_.isEmpty())
        dialog.setInterface(currentInterface_);   // e.g. the one AutoIP just ran on
    if (dialog.exec() != QDialog::Accepted)
        return;
    rememberInterface(dialog.interface());
    network_->connectToLink(currentInterface_);
}

void MainWindow::rememberInterface(const QString& iface)
{
    currentInterface_ = iface;
    if (!iface.isEmpty())
        config_->saveValue(QStringLiteral("network"), QStringLiteral("interface"), iface);
}

void MainWindow::disconnectFromNetwork()
{
    network_->disconnectFromLink();
}

DeckWidget* MainWindow::deckAt(int playerId) const
{
    for (int i = 0; i < deckArea_->deckCount(); ++i) {
        auto* deck = deckArea_->deckAt(i);
        if (deck && deck->playerId() == playerId) return deck;
    }
    return nullptr;
}

bool MainWindow::isLocalDeck(int playerId) const
{
    // In the demo (not connected) our own deck is the "DECK 5 VDJ" row;
    // once on a real link the auto-assigned player id identifies us.
    if (network_ && network_->isConnected())
        return playerId == int(network_->localPlayerId());
    return playerId == 5;
}

int MainWindow::activeDeckId() const
{
    auto* deck = deckArea_->deckAt(activeDeckIndex_);
    return deck ? deck->playerId() : 0;
}

void MainWindow::cycleActiveDeck()
{
    if (deckArea_->deckCount() == 0) return;
    activeDeckIndex_ = (activeDeckIndex_ + 1) % deckArea_->deckCount();
}

void MainWindow::applyPitchFromDeck(int playerId)
{
    if (playerId != network_->localPlayerId() && network_->isConnected())
        return;
    // adjust displayed BPM for the local deck by its pitch slider
    auto* deck = deckAt(playerId);
    if (!deck) return;
    double adjusted = deck->bpm() * (1.0 + deck->pitchPercentage() / 100.0);
    if (adjusted > 0) setVdjBpm(adjusted);
}

void MainWindow::markDeckBpm(int playerId, double bpm)
{
    auto* deck = deckAt(playerId);
    if (deck) deck->setBpm(bpm);
}

void MainWindow::nudgeLocalBpm(double delta)
{
    if (!network_) return;
    int pid = int(network_->isConnected() ? network_->localPlayerId() : 5);
    auto* deck = deckAt(pid);
    double bpm = deck ? deck->bpm() : 128.0;
    bpm = qBound(60.0, bpm + delta, 200.0);
    setVdjBpm(bpm);
    if (deck) deck->setBpm(bpm);
    statusBar()->showMessage(QStringLiteral("BPM %1").arg(bpm, 0, 'f', 2), 2000);
}

void MainWindow::syncToMaster()
{
    int pid = activeDeckId();
    if (!pid) return;
    auto* deck = deckAt(pid);
    if (!deck) return;
    // set the local decks bpm to the master bpm (from status packets)
    setVdjBpm(masterBpm_);
    deck->setBpm(masterBpm_);
    network_->broadcastBeat(masterBpm_, deck->controls() ? deck->controls()->barPos() : 0);
}

void MainWindow::copyBpmFromMaster(int playerId)
{
    auto* deck = deckAt(playerId);
    if (!deck) return;

    // Remote deck: adopt THAT deck's BPM into our VDJ. Local deck: adopt the master's.
    const bool remote = !isLocalDeck(playerId);
    const double bpm = remote ? deck->bpm() : masterBpm_;
    if (bpm <= 0.0) {
        statusBar()->showMessage(remote ? QStringLiteral("No BPM known yet for deck %1").arg(playerId)
                                        : QStringLiteral("No master BPM known yet"), 3000);
        return;
    }
    if (!setVdjBpm(bpm)) {
        statusBar()->showMessage(QStringLiteral("BPM %1 is out of range").arg(bpm, 0, 'f', 2), 4000);
        return;
    }
    statusBar()->showMessage(remote ? QStringLiteral("Copied BPM %1 from deck %2 to the VDJ and the ALSA clock")
                                          .arg(bpm, 0, 'f', 2).arg(playerId)
                                    : QStringLiteral("Copied master BPM %1").arg(bpm, 0, 'f', 2), 4000);
}

// The one place the VDJ's tempo changes: the ProLink beats we send, the ALSA sequencer queue that drum machines
// follow, and the VDJ's own deck row.
bool MainWindow::setVdjBpm(double bpm)
{
    if (!(bpm > 0.0))
        return false;
    network_->setLocalBpm(bpm);
    const bool clockOk = clock_ && clock_->setBpm(bpm);
    // Every row that represents our VDJ: the one with our player id on a live link, and the "VDJ" row that was
    // there from the start (its id need not match the id the link assigned us).
    for (int i = 0; i < deckArea_->deckCount(); ++i) {
        DeckWidget* d = deckArea_->deckAt(i);
        if (isVdjRow(d))
            d->setBpm(bpm);
    }
    return clockOk || !clock_;
}

bool MainWindow::isVdjRow(const DeckWidget* d) const
{
    if (!d) return false;
    const int localId = network_->isConnected() ? int(network_->localPlayerId()) : 5;
    return d->playerId() == localId || d->model() == QLatin1String("VDJ");
}

bool MainWindow::isVdjDeck(int playerId) const
{
    return isVdjRow(deckAt(playerId));
}

void MainWindow::setVdjPlaying(bool playing)
{
    if (!clock_) return;
    if (playing) clock_->start();
    else clock_->stop();
    updateSequencerPlayhead();
}

// While the ALSA sequencer runs, the VDJ rows' four beat grids show its queue position instead of the network's.
void MainWindow::updateSequencerPlayhead()
{
    const double beats = clock_ ? clock_->positionBeats() : -1.0;
    for (int i = 0; i < deckArea_->deckCount(); ++i) {
        DeckWidget* d = deckArea_->deckAt(i);
        if (isVdjRow(d))
            d->beatGrid()->setSequencerBeats(beats);
    }
}

void MainWindow::broadcastFromActiveDeck()
{
    auto* deck = deckArea_->deckAt(activeDeckIndex_);
    if (!deck) return;
    if (deck->controls()) {
        network_->broadcastBeat(deck->controls()->bpm(), deck->controls()->barPos());
    }
}

void MainWindow::setDebugModeFromCli()
{
    setDebugMode(true);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    shutdown();
    QMainWindow::closeEvent(event);
}

void MainWindow::shutdown()
{
    stopBpmRamp();
    if (recorder_ && recorder_->isRecording())
        recorder_->stop();
    if (midiIn_)
        midiIn_->stop();
    if (hud_)
        hud_->close();
    if (clock_) {
        clock_->stop();
    }
    // Graceful shutdown: stop timers, disconnect network (stops managed
    // threads and closes sockets) and release any active NFS mounts.
    if (demoClock_) {
        demoClock_->stop();
    }
    if (keepaliveTimer_) {
        keepaliveTimer_->stop();
    }
    if (autoIp_ && autoIp_->active()) {
        autoIp_->stop();
    }
    if (network_ && network_->isConnected()) {
        network_->disconnectFromLink();
    }
    if (nfs_) {
        const auto players = nfs_->mountedPlayerIds();
        for (int pid : players) {
            nfs_->unmountPlayer(pid);
        }
    }
    PacketLogger::instance().log(QStringLiteral("SYS"),
        QStringLiteral("shutdown complete"));
}

void MainWindow::setupDebugPanel()
{
    debugDock_ = new QDockWidget(QStringLiteral("Debug"), this);
    debugDock_->setObjectName(QStringLiteral("debugDock"));
    debugPanel_ = new DebugPanel(&PacketLogger::instance(), debugDock_);
    debugDock_->setWidget(debugPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, debugDock_);
    debugDock_->hide();
    debugMode_ = false;

    connect(&PacketLogger::instance(), &PacketLogger::entryAdded, this, [this]() {
        if (debugMode_) {
            statusBar()->showMessage(
                QStringLiteral("Log: %1 entries").arg(PacketLogger::instance().entries().size()));
        }
    });
}

void MainWindow::setDebugMode(bool enabled)
{
    debugMode_ = enabled;
    if (debugDock_)
        debugDock_->setVisible(enabled);
    PacketLogger::instance().setEnabled(enabled);
    if (enabled) {
        PacketLogger::instance().log(QStringLiteral("SYS"),
            QStringLiteral("debug mode enabled, log: %1")
                .arg(PacketLogger::instance().logFilePath()));
    }
    for (int i = 0; i < deckArea_->deckCount(); ++i) {
        auto* deck = deckArea_->deckAt(i);
        if (deck) deck->setDebugMode(enabled);
    }
}

void MainWindow::updateDeckOverlays(int pid, double bpm, int pitch, int flags,
                                    int masterState, int active)
{
    if (!debugMode_)
        return;
    auto* deck = deckAt(pid);
    if (!deck) return;
    QStringList lines;
    lines << QStringLiteral("pid=%1").arg(pid)
          << QStringLiteral("bpm=%1").arg(bpm, 0, 'f', 2)
          << QStringLiteral("pitch=%1").arg(pitch)
          << QStringLiteral("flags=0x%1").arg(flags, 0, 16)
          << QStringLiteral("master=%1 active=%2").arg(masterState).arg(active);
    deck->setDebugOverlay(lines);
    deck->setDebugMode(true);
}

int MainWindow::localVdjId() const
{
    return network_->isConnected() ? int(network_->localPlayerId()) : 5;
}

// Play / stop the VDJ the way its own Play button does: deck row, ProLink status and the ALSA sequencer.
void MainWindow::setVdjTransport(bool playing)
{
    if (auto* deck = deckAt(localVdjId()))
        deck->setPlaying(playing);
    network_->setLocalPlaying(playing);
    setVdjPlaying(playing);
}

// ---- MIDI controllers (instrument profiles + .adjmm maps) ----

QString MainWindow::defaultMidiMapPath() const
{
    const QString configured = config_->loadValue(QStringLiteral("midi"), QStringLiteral("map"));
    if (!configured.isEmpty() && QFile::exists(configured))
        return configured;
    const QStringList candidates = {
        QDir::homePath() + QStringLiteral("/.config/vdj-ui/default.adjmm"),
        QStringLiteral("/etc/vdj-ui/default.adjmm"),
        QStringLiteral("/usr/share/vdj-ui/default.adjmm"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/vdj-ui/default.adjmm"),
    };
    for (const QString& c : candidates)
        if (QFile::exists(c)) return c;
    return QString();
}

void MainWindow::setupMidi()
{
    midiRouter_ = new MidiRouter(this);
    midiIn_ = new MidiMapInput(this);
    instruments_ = new InstrumentManager(config_, this);

    connect(midiIn_, &MidiMapInput::errorOccurred, this,
            [this](const QString& msg) { statusBar()->showMessage(msg, 6000); });
    connect(midiIn_, &MidiMapInput::startRequested, this, [this]() { setVdjTransport(true); });
    connect(midiIn_, &MidiMapInput::stopRequested, this, [this]() { setVdjTransport(false); });
    connect(midiIn_, &MidiMapInput::toggleRequested, this, [this]() {
        setVdjTransport(!(clock_ && clock_->running()));
    });
    connect(midiIn_, &MidiMapInput::restartRequested, this, [this]() {
        if (!clock_) return;
        clock_->stop();
        clock_->start();                           // MIDI start + clock from tick 0
        updateSequencerPlayhead();
    });
    connect(midiIn_, &MidiMapInput::nudgeRequested, this, &MainWindow::nudgeSequencer);
    connect(midiIn_, &MidiMapInput::tempoAdjustRequested, this, &MainWindow::nudgeLocalBpm);
    connect(midiIn_, &MidiMapInput::copyMasterRequested, this,
            [this]() { copyBpmFromMaster(localVdjId()); });
    connect(midiIn_, &MidiMapInput::becomeMasterRequested, this,
            [this]() { network_->requestMaster(); });
    connect(instruments_, &InstrumentManager::wired, this,
            [this](const QString& d) { statusBar()->showMessage(QStringLiteral("MIDI: ") + d, 6000); });

    const QString map = defaultMidiMapPath();
    if (!map.isEmpty())
        startMidiMap(map);

    instruments_->setSequencer(clock_);
    instruments_->setRouter(midiRouter_);
    instruments_->setInputPort(midiIn_->clientId(), midiIn_->portId());
    instruments_->loadProfiles();
    instruments_->autowire();
}

void MainWindow::startMidiMap(const QString& path)
{
    if (midiIn_->start(path))
        statusBar()->showMessage(QStringLiteral("MIDI map: %1").arg(path), 5000);
}

void MainWindow::chooseMidiMap()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Select MIDI map"),
        QDir::homePath() + QStringLiteral("/.config/vdj-ui"), QStringLiteral("adj MIDI maps (*.adjmm);;All files (*)"));
    if (path.isEmpty())
        return;
    config_->saveValue(QStringLiteral("midi"), QStringLiteral("map"), path);
    startMidiMap(path);
    instruments_->setInputPort(midiIn_->clientId(), midiIn_->portId());
    instruments_->autowire();
}

// ---- Recording ----

void MainWindow::setupRecording()
{
    recorder_ = new AudioRecorder(this);
    recordingPanel_ = new RecordingPanel(recorder_);
    recordDock_ = new QDockWidget(QStringLiteral("Recording"), this);
    recordDock_->setObjectName(QStringLiteral("recordDock"));
    recordDock_->setWidget(recordingPanel_);
    addDockWidget(Qt::RightDockWidgetArea, recordDock_);
    recordDock_->hide();
    // the action and the dock's close button stay in step
    connect(recordAction_, &QAction::toggled, recordDock_, &QDockWidget::setVisible);
    connect(recordDock_, &QDockWidget::visibilityChanged, recordAction_, &QAction::setChecked);
    connect(recorder_, &AudioRecorder::recordingStarted, this,
            [this](const QString& p) { statusBar()->showMessage(QStringLiteral("Recording to %1").arg(p), 5000); });
    connect(recorder_, &AudioRecorder::recordingStopped, this,
            [this](const QString& p) { statusBar()->showMessage(QStringLiteral("Recording saved: %1").arg(p), 5000); });
}

// ---- HUD ----

void MainWindow::showHud(bool show)
{
    if (show && !hud_) {
        hud_ = new HudWindow(network_, config_);
        hud_->setAttribute(Qt::WA_DeleteOnClose);
        connect(hud_, &QObject::destroyed, this, [this]() {
            hud_ = nullptr;
            if (hudAction_) {
                QSignalBlocker b(hudAction_);
                hudAction_->setChecked(false);
            }
        });
    }
    if (!hud_) return;
    if (show) hud_->show();
    else hud_->close();
}

// ---- BPM ramp ----

// Ramping needs the VDJ to be in charge of the tempo: on a live link it must be the ProLink master.
void MainWindow::updateRampEnabled()
{
    if (!bpmRamp_) return;
    const bool ok = !network_->isConnected()
        || masterPid_ == localVdjId();
    bpmRamp_->setEnabled(ok);
    if (!ok) stopBpmRamp();
}

void MainWindow::startBpmRamp(double targetBpm, int bars)
{
    auto* deck = deckAt(localVdjId());
    const double from = deck ? deck->bpm() : 128.0;
    if (!(from > 0.0) || bars < 1) return;
    // duration of the ramp at the average of the two tempos: 4 beats per bar
    const double avg = (from + targetBpm) / 2.0;
    rampFrom_ = from;
    rampTo_ = targetBpm;
    rampDurationMs_ = qint64(bars * 4 * 60000.0 / avg);
    rampElapsed_.start();
    if (!rampTimer_) {
        rampTimer_ = new QTimer(this);
        rampTimer_->setInterval(100);
        connect(rampTimer_, &QTimer::timeout, this, [this]() {
            const double t = qMin(1.0, double(rampElapsed_.elapsed()) / double(rampDurationMs_));
            const double bpm = rampFrom_ + (rampTo_ - rampFrom_) * t;
            setVdjBpm(bpm);
            if (t >= 1.0) {
                stopBpmRamp();
                statusBar()->showMessage(QStringLiteral("BPM ramp finished at %1").arg(bpm, 0, 'f', 2), 4000);
            }
        });
    }
    rampTimer_->start();
    statusBar()->showMessage(QStringLiteral("Ramping %1 -> %2 BPM over %3 bars")
        .arg(from, 0, 'f', 2).arg(targetBpm, 0, 'f', 2).arg(bars), 4000);
}

void MainWindow::stopBpmRamp()
{
    if (rampTimer_) rampTimer_->stop();
}

} // namespace vdj
