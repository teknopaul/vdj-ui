#include "main_window.h"
#include "audio/audio_player.h"
#include "deck_area.h"
#include "deck_widget.h"
#include "beat_grid_widget.h"
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
#include "debug_panel.h"
#include "debug/packet_logger.h"
#include "debug_overlay.h"
#include "player_panel.h"
#include "autoip_status_widget.h"
#include "bpm_ramp_widget.h"
#include "hud_window.h"
#include "djtoolz_panel.h"
#include "recording_panel.h"
#include "pendrive_clone_dialog.h"
#include "virtual_pendrives_dialog.h"
#include "pendrive_toolbar.h"
#include "export/pendrive_registry.h"
#include "network/link_export.h"
#include "instrument_config_dialog.h"
#include "midi_monitor_dialog.h"
#include "audio/audio_recorder.h"
#include "audio/quantize_settings.h"
#include "midi/joystick_seq_bridge.h"
#include <QActionGroup>
#include "midi/instrument_manager.h"
#include "midi/midi_map_input.h"
#include "midi/djtoolz_midi_control.h"
#include "controller_dialog.h"
#include "djtoolz_controller.h"
#include "midi/midi_router.h"
#include "midi/alsa_clock.h"
#include "diff_lock_bar.h"
#include <cmath>
#include <chrono>

#include <QMenuBar>
#include <QToolButton>
#include <QIcon>
#include <QFileDialog>
#include <QMessageBox>
#include <QFontDialog>
#include <QDialogButtonBox>
#include <QProcess>
#include <QFileInfo>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QPushButton>
#include <QApplication>
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
#include "ui/rekordbox_mode_panel.h"
#include "ui/trakcrate_panel.h"
#include "ui/trakcrate_track_editor.h"
#include "ui/trakcrate_scripts.h"
#include "ui/trakcrate_preview_player.h"
#include "dbserver/rb_mode_options.h"

namespace vdj {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("🎧 VDJ UI");
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
    quantize_ = new QuantizeSettings(config_, this);
    restartTimer_ = new QTimer(this);
    restartTimer_->setSingleShot(true);
    restartTimer_->setTimerType(Qt::PreciseTimer);
    startTimer_ = new QTimer(this);
    startTimer_->setSingleShot(true);
    startTimer_->setTimerType(Qt::PreciseTimer);
    connect(startTimer_, &QTimer::timeout, this, [this]() {
        const int pid = pendingStartPid_;
        pendingStartPid_ = 0;
        if (auto* deck = deckAt(pid))
            deck->setPlaying(true);
        network_->setLocalPlaying(true);
        setVdjPlaying(true);
    });
    connect(restartTimer_, &QTimer::timeout, this, [this]() {
        if (!clock_) return;
        clock_->restart();
        updateSequencerPlayhead();
        statusBar()->showMessage(QStringLiteral("Restarted"), 2000);
    });
    applyAppFont(config_->loadValue(QStringLiteral("ui"), QStringLiteral("font_family")),
                 config_->loadValue(QStringLiteral("ui"), QStringLiteral("font_size")).toInt());
    currentInterface_ = config_->loadValue(QStringLiteral("network"), QStringLiteral("interface"));   // last NIC used for prolink
    nfs_ = new NfsMountManager(this);
    trackBrowser_ = nullptr; // created in setupDemoDecks
    QObject::connect(network_, &NetworkManager::connected, this, [this]() {
        if (tcPanel_) { tcPanel_->stopPreview(); tcPanel_->preview()->refreshAllowed(); }
        statusNetwork_->setText("Prolink: connected");
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
            }
        deckOrder_ = { localId };
        statusDecks_->setText(QString("Decks: %1").arg(deckArea_->deckCount()));
        // Show our own address on the local VDJ deck row.
        if (auto* local = deckAt(int(network_->localPlayerId())))
            local->setIp(network_->ipForPlayer(int(network_->localPlayerId())));
    });
    QObject::connect(network_, &NetworkManager::disconnected, this, [this]() {
        if (tcPanel_) tcPanel_->preview()->refreshAllowed();
        statusNetwork_->setText("Prolink: down");
        updateRampEnabled();
        for (int i = deckArea_->deckCount() - 1; i >= 0; --i) {      // the other decks are no longer visible
            DeckWidget* d = deckArea_->deckAt(i);
            if (d && !d->isRemote() && d->model() == QLatin1String("VDJ")) d->setPlayerId(5);
            else if (d) deckArea_->removeDeck(d->playerId());
        }
        deckOrder_ = { 5 };
        statusDecks_->setText(QString("Decks: %1").arg(deckArea_->deckCount()));
        // Everything that came from the link: the master, the sync to a deck, ramps, NFS shares, the track being shown.
        stopBpmRamp();
        masterPid_ = -1;
        masterBpm_ = 0.0;
        setMasterStatus(-1);
        difflock_.off();                       // also unlocks the BPM spinner
        refreshDiffLock();
        for (DeckWidget* d : deckArea_->findChildren<DeckWidget*>())
            if (isVdjRow(d)) d->setMaster(false);
        if (nfs_) {
            const auto players = nfs_->mountedPlayerIds();
            for (int pid : players) nfs_->unmountPlayer(pid);
        }
        updateRampEnabled();
        statusBar()->showMessage(QStringLiteral("Disconnected from the link"), 4000);
        if (autoIp_ && autoIp_->active())
            autoIp_->stop();
    });
    QObject::connect(network_, &NetworkManager::discoveryPhase, this, [this](const QString& text) {
        statusBar()->showMessage(text, text.startsWith(QLatin1String("Joined")) ? 6000 : 0);
        statusNetwork_->setText(QStringLiteral("Prolink: joining"));
    });
    QObject::connect(network_, &NetworkManager::errorOccurred, this,
                     [this](const QString& msg) {
        statusNetwork_->setText("Prolink: " + msg);
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
    QObject::connect(autoIp_, &AutoIpManager::noAddress, this,
                     [this](const QString& iface) {
        setAutoIpChecked(false);
        QMessageBox box(QMessageBox::Question, QStringLiteral("AutoIP"),
            QStringLiteral("No 169.254.x.x address appeared on %1 (is the cable connected?).\n\n"
                           "Let NetworkManager do it instead? This runs:\n"
                           "  nmcli con mod <profile> ipv4.link-local enabled\n"
                           "  nmcli con up <profile>\n"
                           "and leaves link-local enabled on that connection.").arg(iface),
            QMessageBox::NoButton, this);
        QPushButton* nm = box.addButton(QStringLiteral("Use NetworkManager"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == nm) {
            setAutoIpChecked(true);
            statusBar()->showMessage(QStringLiteral("AutoIP: asking NetworkManager for a link-local address on %1...").arg(iface), 8000);
            autoIp_->startViaNetworkManager(iface);
        }
    });
    QObject::connect(network_, &NetworkManager::masterChanged, this, [this](int pid) {
        setMasterStatus(pid);
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
            if (!playing && !isVdjDeck(pid)) {       // a stopped deck sends no beats: do not leave a stale reading
                beatTempo_[pid].reset();
                deck->setMeasuredBpm(0.0);
            }
            deck->setMaster(masterState != 0);
            deck->setSync((flags & CDJ_STAT_FLAG_SYNC) != 0);
            if (bpm > 0)
                deck->setBpm(bpm);
            if (playing && bpm > 0 && masterState)
                masterBpm_ = bpm;
        }
        updateDeckOverlays(pid, bpm, pitch, flags, masterState, active);
        if (pid == bpmLockedTo()) followLockedBpm();
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
        if (!isVdjDeck(pid))
            deckPhase_.setBpm(pid, bpm);
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
        if (pid == bpmLockedTo()) followLockedBpm();
    });
    QObject::connect(network_, &NetworkManager::beatTimed, this, [this](int pid, int barPos, qint64 steadyNs) {
        if (isVdjDeck(pid)) return;                  // ours: the ALSA sequencer's tempo is shown instead
        deckPhase_.onBeat(pid, double(steadyNs) / 1e6, barPos);   // the packet's arrival, steady clock
        BeatTempo& tempo = beatTempo_[pid];
        tempo.addBeat(barPos, steadyNs);
        if (auto* deck = deckAt(pid))
            deck->setMeasuredBpm(tempo.bpm());
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
        beatTempo_.erase(pid);
        deckArea_->removeDeck(pid);
        if (difflock_.player() == pid) { difflock_.off(); }
        refreshDiffLock();
        statusDecks_->setText(QString("Decks: %1").arg(deckArea_->deckCount()));
        if (nfs_->isMounted(pid)) {
            nfs_->unmountPlayer(pid);
        }
        statusBar()->showMessage(QStringLiteral("Deck %1 left the network").arg(pid), 3000);
    });

    QObject::connect(nfs_, &NfsMountManager::mounted, this, [this](int pid, const QString& mp) {
        if (trackBrowser_) {
            trackBrowser_->setPlayerId(pid);
            trackBrowser_->setMount(mp);
        }
        statusBar()->showMessage(QStringLiteral("NFS mounted for deck %1").arg(pid), 3000);
    });
    QObject::connect(nfs_, &NfsMountManager::mountFailed, this, [this](int pid, const QString& err) {
        statusBar()->showMessage(QStringLiteral("NFS mount failed (deck %1): %2").arg(pid).arg(err), 5000);
    });

    setupMenus();
    QObject::connect(network_, &NetworkManager::linkExportNeedsPort111, this, [this]() {
        // The players mount a player's USB stick first (portmapper on UDP 111, then mountd) and only then ask for its media and library:
        // without port 111 they never get further, so say it plainly and once.
        offerPort111Capability();
    });
    QObject::connect(network_, &NetworkManager::linkExportLog, this, [this](const QString& line) { statusBar()->showMessage(line, 8000); });
    if (config_->loadValue(QStringLiteral("link_export"), QStringLiteral("enabled")) == QLatin1String("1")) {
        linkExportWanted_ = true;                       // exports the database once one is loaded
        if (linkExportAction_) linkExportAction_->setChecked(true);
    }
    setupStatusBar();
    setupShortcuts();
    setupDemoDecks();
    setupDemoTrackBrowser();
    QObject::connect(trackBrowser_, &TrackBrowser::databaseLoaded, this, [this](const QString&) {
        if (trackBrowser_->isRemote()) return;          // a deck's database is not ours to export
        if (linkExportWanted_ && explicitExportFolder_.isEmpty()) applyLinkExport();
    });
    connectDeckSignals();
    startDemoClocks();
    sequencerTimer_ = new QTimer(this);
    sequencerTimer_->setInterval(16);
    connect(sequencerTimer_, &QTimer::timeout, this, &MainWindow::updateSequencerPlayhead);
    sequencerTimer_->start();
    setupDebugPanel();
    setupRecording();
    setupMidi();
}

MainWindow::~MainWindow()
{
    if (midiIn_) midiIn_->stop();
    if (djtoolzPanel_) djtoolzPanel_->shutdown();   // its threads read the clock
    delete clock_;
}

void MainWindow::connectDeckSignals()
{
    QObject::connect(trackBrowser_, &TrackBrowser::trackSelected, this,
                     [this](int pid, const QString& title, const QString& artist,
                            const QString& bpm, const QString& key) {
        statusBar()->showMessage(QStringLiteral("Selected: %1").arg(title), 3000);
    });
    QObject::connect(trackBrowser_, &TrackBrowser::previewRequested, playerPanel_, &PlayerPanel::loadTrack);
    QObject::connect(playerPanel_, &PlayerPanel::status, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 5000);
    });
    QObject::connect(deckArea_, &DeckArea::playPauseClicked, this, [this](int pid, bool play) {
        const bool vdj = isVdjDeck(pid);
        if (vdj) cancelPendingStart();                    // pressing the button again cancels a Play that is waiting
        if (vdj && play) {
            // Q on and synced to a deck: start on that deck's next grid point (the global resolution); otherwise at once
            const double nowMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
            DeckControls* ctl = deckAt(pid) ? deckAt(pid)->controls() : nullptr;
            bool stale = false;
            const double wait = playWaitMs(ctl && ctl->quantizeOn(), difflock_.player(), deckPhase_, nowMs, quantize_->subdivisions(), &stale);
            if (stale && !staleWarned_) {
                staleWarned_ = true;
                statusBar()->showMessage(QStringLiteral("Quantize: sync deck not seen"), 4000);
            } else if (!stale) {
                staleWarned_ = false;
            }
            if (wait > 0.0) {
                pendingStartPid_ = pid;
                startTimer_->start(int(std::lround(wait)));
                statusBar()->showMessage(QStringLiteral("Play queued (%1 ms)").arg(qRound(wait)), 4000);
                applyPitchFromDeck(pid);
                return;                                   // the timer does the rest
            }
        }
        // Reflect transport state so the demo clock / beat grid actually runs
        // for the local deck; on a live link the next status packet keeps it
        // in sync anyway.
        if (auto* deck = deckAt(pid))
            deck->setPlaying(play);
        network_->setLocalPlaying(play);
        if (vdj) setVdjPlaying(play);
        applyPitchFromDeck(pid);
    });
    QObject::connect(deckArea_, &DeckArea::restartClicked, this, [this](int pid, bool quantize) {
        if (isVdjDeck(pid)) vdjRestart(quantize);
    });
    QObject::connect(deckArea_, &DeckArea::beatJumpClicked, this, [this](int pid, int beats) {
        // the bar position moves, the clock does not: inherently on the beat, Q makes no difference
        if (!isVdjDeck(pid) || !clock_ || !clock_->running()) return;
        clock_->shiftBeats(beats);
        updateSequencerPlayhead();
    });
    QObject::connect(deckArea_, &DeckArea::syncClicked, this, [this](int pid) {
        if (isVdjDeck(pid)) syncToMaster();
        else lockDiffLockTo(pid);                      // Sync on another deck: difflock the sequencer to it
    });
    QObject::connect(deckArea_, &DeckArea::setBpmClicked, this, [this](int pid, double bpm) {
        if (bpmLockedTo()) {                    // synced to a real deck: the tempo is theirs, put the spinner back
            setVdjBpm(bpm);                     // says so in the status bar
            if (auto* d = deckAt(pid); d && clock_ && std::abs(d->controls()->bpm() - clock_->bpm()) > 0.004)
                d->controls()->setBpm(clock_->bpm());
            return;
        }
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

// Menu icons are all 22x22 so the icon column lines up whatever the source is
static QIcon glyphIcon(const QString& glyph)
{
    QIcon icon;
    for (int scale : {1, 2}) {
        QPixmap pm(22 * scale, 22 * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::TextAntialiasing);
        QFont f = QApplication::font();
        f.setPixelSize(17);
        p.setFont(f);
        p.setPen(QColor(0xe0, 0xe0, 0xe0));
        p.drawText(QRect(0, 0, 22, 22), Qt::AlignCenter, glyph);
        icon.addPixmap(pm);
    }
    return icon;
}

// A freedesktop themed icon scaled into the same 22x22 cell, falling back to a glyph when the theme lacks it
static QIcon themedIcon(const QString& name, const QString& fallbackGlyph)
{
    const QIcon themed = QIcon::fromTheme(name);
    if (themed.isNull())
        return glyphIcon(fallbackGlyph);
    QIcon icon;
    for (int scale : {1, 2}) {
        QPixmap pm(22 * scale, 22 * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        themed.paint(&p, QRect(0, 0, 22, 22));
        icon.addPixmap(pm);
    }
    return icon;
}

static QIcon imageIcon(const QString& resource)
{
    QIcon icon;
    icon.addFile(resource, QSize(22, 22));
    return icon;
}

void MainWindow::setupMenus()
{
    qApp->setAttribute(Qt::AA_DontShowIconsInMenus, false);
    if (!QIcon::hasThemeIcon(QStringLiteral("drive-harddisk")))
        QIcon::setThemeName(QStringLiteral("Yaru"));

    QMenu* file = menuBar()->addMenu("&File");
    QAction* openDb = file->addAction("&Open Rekordbox Folder...");
    openDb->setIcon(glyphIcon(QStringLiteral("📂")));
    openDb->setShortcut(QKeySequence::Open);
    connect(openDb, &QAction::triggered, this, &MainWindow::openRekordboxFolder);
    QAction* pendrivesAction = file->addAction("&Pendrives...");
    pendrivesAction->setIcon(themedIcon(QStringLiteral("drive-harddisk"), QStringLiteral("💾")));
    pendrivesAction->setToolTip("Named folders and the pendrives plugged into this PC: open one in the browser or export it to the decks");
    connect(pendrivesAction, &QAction::triggered, this, &MainWindow::showVirtualPendrives);
    QAction* cloneAction = file->addAction("&Clone Pendrive...");
    cloneAction->setIcon(themedIcon(QStringLiteral("media-removable"), QStringLiteral("⎘")));
    connect(cloneAction, &QAction::triggered, this, [this]() {
        PendriveCloneDialog dlg(this);
        dlg.exec();
    });
    file->addSeparator();
    QAction* quit = file->addAction("&Quit");
    quit->setIcon(glyphIcon(QStringLiteral("⏻")));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QMainWindow::close);

    QMenu* view = menuBar()->addMenu("&View");
    QAction* fullscr = view->addAction("Full&screen");
    fullscr->setIcon(glyphIcon(QStringLiteral("⛶")));
    fullscr->setShortcut(Qt::CTRL | Qt::Key_F11);   // F1..F12 switch the DJ Tools tools
    connect(fullscr, &QAction::triggered, this, [this]() {
        if (isFullScreen()) showNormal();
        else showFullScreen();
    });

    hudAction_ = view->addAction("&HUD Window");
    hudAction_->setShortcut(Qt::CTRL | Qt::Key_H);
    hudAction_->setCheckable(true);
    connect(hudAction_, &QAction::toggled, this, &MainWindow::showHud);

    QAction* browserAction = view->addAction("Rekordbox &Folder View");
    browserAction->setIcon(glyphIcon(QStringLiteral("☰")));
    browserAction->setCheckable(true);
    browserAction->setChecked(config_->loadValue(QStringLiteral("ui"), QStringLiteral("rekordbox_view")) != QLatin1String("0"));
    connect(browserAction, &QAction::toggled, this, [this](bool on) {
        config_->saveValue(QStringLiteral("ui"), QStringLiteral("rekordbox_view"), on ? QStringLiteral("1") : QStringLiteral("0"));
        if (trackBrowser_) trackBrowser_->setVisible(on);
    });

    QAction* playerAction = view->addAction("Local Track &Player");
    playerAction->setIcon(glyphIcon(QStringLiteral("▶")));
    playerAction->setCheckable(true);
    playerAction->setChecked(config_->loadValue(QStringLiteral("ui"), QStringLiteral("local_player")) != QLatin1String("0"));
    connect(playerAction, &QAction::toggled, this, [this](bool on) {
        config_->saveValue(QStringLiteral("ui"), QStringLiteral("local_player"), on ? QStringLiteral("1") : QStringLiteral("0"));
        if (!playerPanel_) return;
        if (!on && playerPanel_->stopButton()->isEnabled())
            playerPanel_->stopButton()->click();   // don't leave a hidden player making noise
        playerPanel_->setVisible(on);
    });

    QAction* djtoolzAction = view->addAction("DJ &Tools");
    djtoolzAction->setIcon(glyphIcon(QStringLiteral("🎛")));
    djtoolzAction->setCheckable(true);
    djtoolzAction->setToolTip("The live DJ Tools (bombomatic, stabs, acid loops, one shots, MIDI notes) in time with the VDJ sequencer. "
                              "Hiding the panel does not stop it.");
    djtoolzAction->setChecked(config_->loadValue(QStringLiteral("ui"), QStringLiteral("djtoolz")) == QLatin1String("1"));
    connect(djtoolzAction, &QAction::toggled, this, [this](bool on) {
        config_->saveValue(QStringLiteral("ui"), QStringLiteral("djtoolz"), on ? QStringLiteral("1") : QStringLiteral("0"));
        if (djtoolzPanel_)
            djtoolzPanel_->setVisible(on);
    });

    tcAction_ = view->addAction("&trakcrate Library");
    tcAction_->setIcon(imageIcon(QStringLiteral(":/img/trakcrate-128x128.png")));
    tcAction_->setCheckable(true);
    tcAction_->setToolTip("Import audio folders into the trakcrate library and prepare pendrives");
    connect(tcAction_, &QAction::toggled, this, [this](bool on) { trakcrateDock()->setVisible(on); });

    recordAction_ = view->addAction("&Recording Panel");
    recordAction_->setIcon(glyphIcon(QStringLiteral("🔴")));
    recordAction_->setCheckable(true);

    QAction* debugAction = view->addAction("&Debug Panel");
    debugAction->setIcon(glyphIcon(QStringLiteral("🐞")));
    debugAction->setShortcut(Qt::CTRL | Qt::Key_D);
    debugAction->setCheckable(true);
    connect(debugAction, &QAction::toggled, this, &MainWindow::setDebugMode);
    QAction* logPacketsAction = view->addAction("Log &Packets (debug panel)");
    logPacketsAction->setCheckable(true);
    logPacketsAction->setToolTip("Record every status, beat and discovery packet in the debug log. Turn it off to keep only the other lines (USER, LINK, NFS, SYS): "
                                 "less work while connected to busy decks. Only has an effect in debug mode.");
    {
        const bool on = config_->loadValue(QStringLiteral("debug"), QStringLiteral("log_packets")) != QLatin1String("0");
        logPacketsAction->setChecked(on);
        PacketLogger::instance().setPacketLevel(on);
    }
    connect(logPacketsAction, &QAction::toggled, this, [this](bool on) {
        PacketLogger::instance().setPacketLevel(on);
        config_->saveValue(QStringLiteral("debug"), QStringLiteral("log_packets"), on ? QStringLiteral("1") : QStringLiteral("0"));
    });

    QMenu* network = menuBar()->addMenu("&Network");
    connectAction_ = network->addAction("&Connect");
    connectAction_->setIcon(imageIcon(QStringLiteral(":/img/connect-22.png")));
    QAction* disconnectAction = network->addAction("&Disconnect");
    disconnectAction->setIcon(glyphIcon(QStringLiteral("⚡")));
    connect(connectAction_, &QAction::triggered, this, &MainWindow::connectToNetwork);
    connect(disconnectAction, &QAction::triggered, this, &MainWindow::disconnectFromNetwork);

    // Connect: plain push button at the left of the menu bar (same action as Network > Connect)
    auto* connectBtn = new QToolButton(menuBar());
    connectBtn->setDefaultAction(connectAction_);
    connectBtn->setIcon(QIcon(QStringLiteral(":/img/connect-22.png")));
    connectBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    connectBtn->setToolTip(QStringLiteral("Connect to the ProLink network"));
    connectBtn->setAutoRaise(true);
    connectBtn->setFixedSize(22, 22);
    menuBar()->setCornerWidget(connectBtn, Qt::TopLeftCorner);

    network->addSeparator();
    linkExportAction_ = network->addAction("E&xport the Loaded Database as a LINK Player");
    linkExportAction_->setCheckable(true);
    linkExportAction_->setToolTip("Serve the rekordbox database that is open in the browser (File > Open Rekordbox Folder) to the other decks like a "
                                  "player with a USB stick in it (experimental, off by default). Takes effect while connected.");
    connect(linkExportAction_, &QAction::triggered, this, [this](bool on) { setLinkExportEnabled(on); });
    exportMenu_ = network->addMenu("Exported &Pendrive");
    exportMenu_->setToolTip("Choose which pendrive the decks see; picking another one while exporting swaps it like a deck does (refused while a deck uses it)");
    connect(exportMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildExportMenu);
    QAction* ejectAction = network->addAction("E&ject Exported Pendrive (USB STOP)");
    ejectAction->setIcon(glyphIcon(QStringLiteral("⏏")));
    connect(ejectAction, &QAction::triggered, this, &MainWindow::ejectPendrive);
    network->addSeparator();
    rbAction_ = network->addAction("&Rekordbox PC Mode...");
    rbAction_->setIcon(glyphIcon(QStringLiteral("📦")));
    rbAction_->setCheckable(true);
    rbAction_->setToolTip("Pretend to be a rekordbox PC and serve a local export to the decks (experimental)");
    connect(rbAction_, &QAction::toggled, this, [this](bool on) { rekordboxDock()->setVisible(on); });

    network->addSeparator();
    // The players serve NFS v2 over UDP, which the kernel cannot mount: files are read with libcdj's own client
    deckFilesMenu_ = network->addMenu("&Browse Deck Files");
    deckFilesMenu_->setIcon(glyphIcon(QStringLiteral("📂")));
    connect(deckFilesMenu_, &QMenu::aboutToShow, this, &MainWindow::populateDeckFilesMenu);
    network->addSeparator();
    autoIpMenuAction_ = network->addAction("&AutoIP...");
    autoIpMenuAction_->setIcon(glyphIcon(QStringLiteral("🔗")));
    autoIpMenuAction_->setCheckable(true);
    autoIpMenuAction_->setToolTip("Claim a link-local address on the interface connected to the decks");
    connect(autoIpMenuAction_, &QAction::triggered, this, &MainWindow::setAutoIp);
    QAction* autoIpPermanent = network->addAction("Auto IP (&permanent)...");
    autoIpPermanent->setIcon(glyphIcon(QStringLiteral("🔗")));
    autoIpPermanent->setToolTip("Keep a link-local address on the interface connected to the decks across reboots, using NetworkManager (no password, vdj-ui need not run)");
    connect(autoIpPermanent, &QAction::triggered, this, &MainWindow::makeAutoIpPermanent);
    QObject::connect(autoIp_, &AutoIpManager::permanentDone, this, [this](bool ok, const QString& message) {
        if (ok) {
            statusBar()->showMessage(QStringLiteral("Auto IP (permanent): NetworkManager profile \"%1\" keeps a link-local address on %2").arg(message, currentInterface_), 10000);
            QMessageBox::information(this, QStringLiteral("Auto IP (permanent)"),
                QStringLiteral("Done. The NetworkManager profile \"%1\" now gives %2 a 169.254.x.x address every time it is up, also after a reboot.\n\n"
                               "To undo it: nmcli con modify \"%1\" ipv4.method auto   (or delete the profile with nmcli con delete).").arg(message, currentInterface_));
        } else {
            statusBar()->showMessage(QStringLiteral("Auto IP (permanent) failed: %1").arg(message), 10000);
            QMessageBox::warning(this, QStringLiteral("Auto IP (permanent)"), QStringLiteral("NetworkManager could not be set up:\n\n%1").arg(message));
        }
    });

    network->addSeparator();
    QAction* forgetNicAction = network->addAction("&Forget Saved Network Interface");
    forgetNicAction->setToolTip("The network interface for the decks is asked once and then reused; forget it after the NIC changes");
    connect(forgetNicAction, &QAction::triggered, this, &MainWindow::forgetInterface);

    QMenu* configMenu = menuBar()->addMenu("&Config");
    QMenu* quantizeMenu = configMenu->addMenu("&Quantize");
    quantizeMenu->setIcon(glyphIcon(QStringLiteral("🟥")));
    quantizeMenu->setToolTip("How close to the beat quantized jumps and starts land: the grid step for every quantize button");
    auto* quantizeGroup = new QActionGroup(this);
    for (auto r : {QuantizeSettings::Beat, QuantizeSettings::Half, QuantizeSettings::Quarter}) {
        QAction* a = quantizeMenu->addAction(QuantizeSettings::label(r));
        a->setCheckable(true);
        a->setChecked(quantize_->resolution() == r);
        quantizeGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, r]() { quantize_->setResolution(r); });
    }
    QAction* instAction = configMenu->addAction("&Instruments...");
    instAction->setIcon(glyphIcon(QStringLiteral("🎹")));
    connect(instAction, &QAction::triggered, this, [this]() {
        if (!instruments_) return;
        InstrumentConfigDialog dlg(instruments_, config_, this);
        dlg.exec();
        applyMidiMap();
        djtoolzPanel_->drumKits()->setImportFolders(InstrumentManager::drumKitFolders(config_));
        djtoolzPanel_->drumKitsChanged();
    });
    QAction* usbAction = configMenu->addAction("&USB / MIDI...");
    usbAction->setIcon(glyphIcon(QStringLiteral("➿")));
    connect(usbAction, &QAction::triggered, this, [this]() {
        UsbConfigDialog dlg(config_, this);
        dlg.exec();
    });
    QAction* joyAction = configMenu->addAction("&Joystick...");
    joyAction->setIcon(glyphIcon(QStringLiteral("🕹")));
    connect(joyAction, &QAction::triggered, this, [this]() {
        JoystickConfigDialog dlg(config_, this);
        dlg.exec();
    });
    QAction* monitorAction = configMenu->addAction("MIDI M&onitor...");
    monitorAction->setIcon(glyphIcon(QStringLiteral("📈")));
    monitorAction->setToolTip("Shows the MIDI events the controller sends and what the map turns them into");
    connect(monitorAction, &QAction::triggered, this, [this]() {
        if (!midiMonitor_) midiMonitor_ = new MidiMonitorDialog(midiIn_, this);
        midiMonitor_->show();
        midiMonitor_->raise();
        midiMonitor_->activateWindow();
    });
    configMenu->addSeparator();
    QAction* fontAction = configMenu->addAction("&Font...");
    fontAction->setIcon(glyphIcon(QStringLiteral("𝐀")));
    fontAction->setToolTip("The font for the whole app");
    connect(fontAction, &QAction::triggered, this, &MainWindow::chooseAppFont);

    QMenu* help = menuBar()->addMenu("&Help");
    QAction* aboutAction = help->addAction("&About");
    aboutAction->setIcon(imageIcon(QStringLiteral(":/img/vdj-22x22.png")));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("About VDJ UI"));
        // both logos at 64x64 so the two sections line up
        box.setTextFormat(Qt::RichText);
        box.setText(QStringLiteral("<table cellspacing='0' cellpadding='4'><tr>"
                                   "<td valign='top'><img src=':/img/vdj-64x64.png' width='64' height='64'></td>"
                                   "<td valign='top'><h3>VDJ UI %1</h3>"
                                   "<p>A Virtual Deck for Pioneer ProLink network: syncing MIDI instruments, beat grids of all decks, "
                                   "rekordbox track browsing and a local track preview player, virtual pendrive exports.</p></td></tr>"
                                   "<tr><td valign='top'><img src=':/img/trakcrate-128x128.png' width='64' height='64'></td>"
                                   "<td valign='top'><h3>trakcrate</h3>"
                                   "<p>A master music library kept as plain files and folders that you can edit, and the export of "
                                   "rekordbox-format pendrives that CDJ and XDJ players accept. Part of VDJ UI; the library format is "
                                   "specified in TRAKCRATE-FORMAT.md.</p></td></tr>"
                                   "<tr><td></td><td valign='top'><h3>libcdj</h3>"
                                   "<p>Pioneer ProLink and rekordbox database/analysis file support comes from "
                                   "<b>libcdj</b>: <a href='https://github.com/teknopaul/libcdj'>https://github.com/teknopaul/libcdj</a></p>"
                                   "</td></tr></table>")
                        .arg(QCoreApplication::applicationVersion()));
        box.exec();
    });

    // App logo at the right of the menu bar opens the About box
    auto* logoBtn = new QToolButton(menuBar());
    logoBtn->setIcon(QIcon(QStringLiteral(":/img/vdj-22x22.png")));
    logoBtn->setToolTip(QStringLiteral("About VDJ UI"));
    logoBtn->setAutoRaise(true);
    logoBtn->setFixedSize(22, 22);
    connect(logoBtn, &QToolButton::clicked, aboutAction, &QAction::trigger);
    menuBar()->setCornerWidget(logoBtn, Qt::TopRightCorner);
}

void MainWindow::setAutoIp(bool on)
{
    if (!on) {
        autoIp_->stop();
        setAutoIpChecked(false);
        return;
    }
    // AutoIP must run on the NIC that is cabled to the decks: asked once, then the saved one is used (Config > Forget Saved Network Interface)
    const QString iface = savedInterfaceUsable() ? currentInterface_
        : NetworkConfigDialog::chooseInterface(this, QStringLiteral("AutoIP"), activeInterface());
    if (iface.isEmpty()) {
        setAutoIpChecked(false);
        return;
    }
    rememberInterface(iface);
    setAutoIpChecked(true);
    statusBar()->showMessage(QStringLiteral("AutoIP: waiting for an address on %1...").arg(iface), 5000);
    autoIp_->start(iface);
}

void MainWindow::makeAutoIpPermanent()
{
    if (!AutoIpManager::networkManagerAvailable()) {
        QMessageBox::information(this, QStringLiteral("Auto IP (permanent)"), QStringLiteral("This needs NetworkManager (nmcli), which was not found."));
        return;
    }
    const QString iface = savedInterfaceUsable() ? currentInterface_
        : NetworkConfigDialog::chooseInterface(this, QStringLiteral("Auto IP (permanent)"), activeInterface());
    if (iface.isEmpty()) return;
    rememberInterface(iface);
    if (QMessageBox::question(this, QStringLiteral("Auto IP (permanent)"),
            QStringLiteral("Make %1 keep a link-local (169.254.x.x) address for the decks, permanently?\n\n"
                           "NetworkManager is told to use only a link-local address on %1 (no DHCP), with autoconnect on, and the profile is tied to this interface. "
                           "It then comes up by itself after a reboot, while the same hardware is installed.\n\nThis runs:\n%2")
                .arg(iface, AutoIpManager::permanentCommands(iface)),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    statusBar()->showMessage(QStringLiteral("Auto IP (permanent): asking NetworkManager about %1...").arg(iface), 8000);
    autoIp_->makePermanent(iface);
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
    // Unlocked, the buttons move the playhead: - sets it back (a longer beat), + pushes it forward (a shorter one).
    // Locked, they keep adj's meaning (+ = the next beat is longer) and the target moves with them.
    clock_->nudgeMillis(difflock_.locked() ? ms : -ms);
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
    const int lockedTo = bpmLockedTo();
    for (int i = 0; i < deckArea_->deckCount(); ++i) {
        DeckWidget* d = deckArea_->deckAt(i);
        if (d && isVdjRow(d))
            d->controls()->setBpmLocked(lockedTo != 0, QStringLiteral("Synced to deck %1: the BPM follows it").arg(lockedTo));
    }
    followLockedBpm();
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

// UDP 111 is privileged: give this binary cap_net_bind_service with setcap (root via the polkit prompt). A capability only applies to a
// freshly started process, so after it is granted vdj-ui offers to restart itself.
void MainWindow::offerPort111Capability()
{
    if (port111Offered_)
        return;
    port111Offered_ = true;
    const QString exe = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();   // setcap fails on a symlink
    QMessageBox box(QMessageBox::Warning, QStringLiteral("LINK export needs UDP port 111"),
        QStringLiteral("The decks look for the NFS portmapper of a player on UDP port 111, a privileged port, and it could not be opened. "
                       "Until it can, no deck will list or load tracks from this export.\n\n"
                       "Allow vdj-ui to bind low ports? This asks for your password and runs:\n\n"
                       "  setcap cap_net_bind_service=+ep %1\n\n"
                       "vdj-ui then restarts. The permission is lost whenever the binary is rebuilt.").arg(exe),
        QMessageBox::NoButton, this);
    QPushButton* grant = box.addButton(QStringLiteral("Grant permission"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Not now"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != grant) {
        port111Offered_ = false;
        return;
    }
    QProcess setcap;
    setcap.setProcessChannelMode(QProcess::MergedChannels);
    setcap.start(QStringLiteral("/usr/bin/pkexec"), { QStringLiteral("setcap"), QStringLiteral("cap_net_bind_service=+ep"), exe });
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool done = setcap.waitForFinished(120000);   // the polkit prompt waits for the password
    QApplication::restoreOverrideCursor();
    port111Offered_ = false;
    if (!done || setcap.exitCode() != 0) {
        const QString out = QString::fromLocal8Bit(setcap.readAll()).trimmed();
        QMessageBox::warning(this, QStringLiteral("setcap"),
            setcap.exitCode() == 126 ? QStringLiteral("Authorisation was cancelled or refused.")
                                     : QStringLiteral("setcap failed (exit %1): %2").arg(setcap.exitCode()).arg(out.isEmpty() ? QStringLiteral("no output") : out));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Restart vdj-ui"),
            QStringLiteral("Permission granted. It only applies to a new process: restart vdj-ui now?")) != QMessageBox::Yes)
        return;
    if (QProcess::startDetached(exe, QCoreApplication::arguments().mid(1)))
        QCoreApplication::quit();
    else
        QMessageBox::warning(this, QStringLiteral("Restart"), QStringLiteral("Could not start %1: restart it by hand.").arg(exe));
}

// One setting for the whole app: the family goes through the stylesheet (it overrides per-widget fonts), the base size through QApplication.
// Empty family / size 0 = the built-in defaults.
void MainWindow::applyAppFont(const QString& family, int pointSize)
{
    if (defaultAppFont_ == QFont())
        defaultAppFont_ = QApplication::font();
    QFont f = defaultAppFont_;
    if (pointSize > 0) f.setPointSize(pointSize);
    QApplication::setFont(f);
    setStyleSheet(darkThemeStylesheet(family));
}

void MainWindow::chooseAppFont()
{
    QFont current = QApplication::font();
    const QString fam = config_->loadValue(QStringLiteral("ui"), QStringLiteral("font_family"));
    if (!fam.isEmpty()) current.setFamily(fam);
    QFontDialog dlg(current, this);
    dlg.setWindowTitle(QStringLiteral("Application Font"));
    dlg.setOption(QFontDialog::DontUseNativeDialog);   // the native dialog has no button box to extend
    bool reset = false;
    if (auto* box = dlg.findChild<QDialogButtonBox*>()) {
        QPushButton* resetBtn = box->addButton(QStringLiteral("Reset Font"), QDialogButtonBox::ResetRole);
        connect(resetBtn, &QPushButton::clicked, &dlg, [&]() { reset = true; dlg.reject(); });
    }
    const bool ok = dlg.exec() == QDialog::Accepted;
    if (reset) {
        resetAppFont();
        return;
    }
    if (!ok)
        return;
    const QFont f = dlg.selectedFont();
    config_->saveValue(QStringLiteral("ui"), QStringLiteral("font_family"), f.family());
    config_->saveValue(QStringLiteral("ui"), QStringLiteral("font_size"), QString::number(f.pointSize()));
    applyAppFont(f.family(), f.pointSize());
}

void MainWindow::resetAppFont()
{
    config_->saveValue(QStringLiteral("ui"), QStringLiteral("font_family"), QString());
    config_->saveValue(QStringLiteral("ui"), QStringLiteral("font_size"), QString());
    applyAppFont(QString(), 0);
}

// grey when nobody is master, the deck widgets' master yellow when somebody is
void MainWindow::setMasterStatus(int pid)
{
    if (pid < 0) {
        statusMaster_->setText(QStringLiteral("Master: none"));
        statusMaster_->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
    } else {
        statusMaster_->setText(QStringLiteral("Master: %1").arg(pid));
        statusMaster_->setStyleSheet(QStringLiteral("color: #ffd54a;"));
    }
}

void MainWindow::setupStatusBar()
{
    statusNetwork_ = new QLabel("Prolink: down", this);
    statusMaster_ = new QLabel(this);
    setMasterStatus(-1);
    statusAutoIp_ = new AutoipStatusWidget(autoIp_, this);
    statusDecks_ = new QLabel("Decks: 0", this);
    statusAutoIp_->setInterface(currentInterface_);
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

void MainWindow::setupDemoDecks()
{
    deckArea_ = new DeckArea(this);
    deckArea_->setMinimumWidth(1000);
    trackBrowser_ = new TrackBrowser(this);
    if (config_->loadValue(QStringLiteral("ui"), QStringLiteral("rekordbox_view")) == QLatin1String("0"))
        trackBrowser_->setVisible(false);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(deckArea_);
    {   // the drive viewer with its one-line pendrive tool bar on top
        auto* viewer = new QWidget(this);
        auto* vl = new QVBoxLayout(viewer);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);
        // rekordbox is written in dark red with a white "db"; "viewer" because this pane has no editing (that is trakcrate)
        auto* title = new QLabel(QStringLiteral("<span style=\"color:#ee0000\">rekor<span style=\"color:#eeeeee\">db</span>ox</span> viewer"), viewer);
        title->setTextFormat(Qt::RichText);
        title->setContentsMargins(4, 2, 4, 2);
        title->setToolTip(QStringLiteral("Look at a pendrive or a deck's rekordbox database, and export one to the decks. To edit your music use trakcrate."));
        vl->addWidget(title);
        pendriveBar_ = new PendriveToolBar(pendrives(), viewer);
        vl->addWidget(pendriveBar_);
        vl->addWidget(trackBrowser_, 1);
        connect(pendriveBar_, &PendriveToolBar::openRequested, this, &MainWindow::openPendrive);
        connect(pendriveBar_, &PendriveToolBar::insertRequested, this, &MainWindow::exportPendrive);
        connect(pendriveBar_, &PendriveToolBar::stopRequested, this, &MainWindow::ejectPendrive);
        connect(pendriveBar_, &PendriveToolBar::deckRequested, this, &MainWindow::browseDeck);
        connect(trackBrowser_, &TrackBrowser::databaseLoaded, pendriveBar_, [this](const QString& folder) {
            if (trackBrowser_->isRemote()) pendriveBar_->setRemoteViewed(trackBrowser_->remotePlayerId());       // a deck's database: no Insert / USB STOP
            else pendriveBar_->setViewed(folder);
        });
        auto* hint = new QTimer(this);
        hint->setInterval(700);
        connect(hint, &QTimer::timeout, this, &MainWindow::updatePendriveBar);
        hint->start();
        splitter->addWidget(viewer);
    }
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>() << 1000 << 400);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    playerPanel_ = new PlayerPanel(this);
    playerPanel_->setQuantizeSettings(quantize_);
    playerPanel_->setVisible(config_->loadValue(QStringLiteral("ui"), QStringLiteral("local_player")) != QLatin1String("0"));
    centralLayout->addWidget(playerPanel_);
    djtoolzPanel_ = new DjtoolzPanel(clock_, ConfigManager::configDir(), this);
    playerPanel_->player()->setFxRack(djtoolzPanel_->fxRack());     // the FX tools colour the local player too
    djtoolzPanel_->drumKits()->setImportFolders(InstrumentManager::drumKitFolders(config_));
    djtoolzPanel_->drumKitsChanged();
    djtoolzPanel_->setVisible(config_->loadValue(QStringLiteral("ui"), QStringLiteral("djtoolz")) == QLatin1String("1"));
    {
        bool ok = false;
        const double latency = config_->loadValue(QStringLiteral("djtoolz"), QStringLiteral("latency_ms")).toDouble(&ok);
        if (ok && latency > 0)
            djtoolzPanel_->setLatencyMs(latency);
        // the DJ Tools plays on the output chosen for the local track player
        auto syncDevice = [this]() { djtoolzPanel_->engine()->setDevice(playerPanel_->deviceCombo()->currentData().toString()); };
        connect(playerPanel_->deviceCombo(), QOverload<int>::of(&QComboBox::currentIndexChanged), this, syncDevice);
        syncDevice();
    }
    for (int i = 0; i < DjtoolzPanel::kFunctionKeys; ++i) {
        // window wide, and the panel may be hidden: the DJ Tools keeps playing and can still be switched
        auto* sc = new QShortcut(QKeySequence(Qt::Key_F1 + i), this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, [this, i]() { djtoolzPanel_->toggleToolAt(i); });
    }
    connect(djtoolzPanel_, &DjtoolzPanel::message, this, [this](const QString& m) { statusBar()->showMessage(m, 5000); });
    bpmRamp_ = new BpmRampWidget(this);
    connect(bpmRamp_, &BpmRampWidget::rampRequested, this, &MainWindow::startBpmRamp);
    connect(bpmRamp_, &BpmRampWidget::bpmNudged, this, [this](double d) { nudgeLocalBpm(d); });
    // the DJ Tools sits above the rest in a vertical splitter: drag the handle between them to give it more room for many tools
    auto* lower = new QWidget(this);
    auto* lowerLayout = new QVBoxLayout(lower);
    lowerLayout->setContentsMargins(0, 0, 0, 0);
    lowerLayout->addWidget(bpmRamp_);
    lowerLayout->addWidget(splitter, 1);
    auto* djtoolzSplit = new QSplitter(Qt::Vertical, this);
    djtoolzSplit->setChildrenCollapsible(false);
    djtoolzSplit->addWidget(djtoolzPanel_);
    djtoolzSplit->addWidget(lower);
    djtoolzSplit->setStretchFactor(0, 0);
    djtoolzSplit->setStretchFactor(1, 1);
    {
        const int h = config_->loadValue(QStringLiteral("ui"), QStringLiteral("djtoolz_height")).toInt();
        djtoolzSplit->setSizes(QList<int>() << (h >= 120 ? h : 260) << 1000);
    }
    connect(djtoolzSplit, &QSplitter::splitterMoved, this, [this, djtoolzSplit]() {
        if (djtoolzPanel_->isVisible())
            config_->saveValue(QStringLiteral("ui"), QStringLiteral("djtoolz_height"), QString::number(djtoolzSplit->sizes().value(0)));
    });
    centralLayout->addWidget(djtoolzSplit, 1);
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
    setMasterStatus(-1);


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
    // The browser starts empty. It shows a pendrive only once the user picks a virtual or physical one (the drop down or the menu): a folder
    // that cannot be exported to the decks is no use, and a pendrive remembered from the last run may not be plugged in now.
    trackBrowser_->setPlayerId(5);
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
    if (rbPanel_ && rbPanel_->isRunning()) {
        QMessageBox::information(this, QStringLiteral("Connect"),
            QStringLiteral("Rekordbox PC mode is running and uses the same UDP ports. Stop it first."));
        return;
    }
    if (tcPanel_) tcPanel_->stopPreview();                         // the playlist player and the VDJ are exclusive
    if (!savedInterfaceUsable()) {
        NetworkConfigDialog dialog(this);
        if (!currentInterface_.isEmpty())
            dialog.setInterface(currentInterface_);
        if (dialog.exec() != QDialog::Accepted)
            return;
        rememberInterface(dialog.interface());
    }
    // The model name goes into every claim, keep-alive and status packet. VDJ_UI_MODEL=XDJ announces an XDJ-1000 instead of
    // the default CDJ-1000 (experiment: the players show an "older firmware" warning after we join).
    uint32_t flags = VDJ_FLAG_AUTO_ID | VDJ_FLAG_DEV_CDJ;
    if (qgetenv("VDJ_UI_MODEL").trimmed().toUpper() == "XDJ") flags = VDJ_FLAG_AUTO_ID | VDJ_FLAG_DEV_XDJ;
    statusBar()->showMessage(QStringLiteral("Announcing as %1").arg(flags & VDJ_FLAG_DEV_XDJ ? "XDJ-1000" : "CDJ-1000"), 3000);
    network_->connectToLink(currentInterface_, flags);
}

// The saved NIC is used without asking, as long as it still exists on this machine
bool MainWindow::savedInterfaceUsable() const
{
    return !currentInterface_.isEmpty() && QNetworkInterface::interfaceFromName(currentInterface_).isValid();
}

void MainWindow::forgetInterface()
{
    currentInterface_.clear();
    if (statusAutoIp_) statusAutoIp_->setInterface(QString());
    config_->saveValue(QStringLiteral("network"), QStringLiteral("interface"), QString());
    statusBar()->showMessage(QStringLiteral("Saved network interface forgotten: you will be asked on the next Connect / AutoIP"), 5000);
}

void MainWindow::rememberInterface(const QString& iface)
{
    currentInterface_ = iface;
    if (statusAutoIp_) statusAutoIp_->setInterface(iface);
    if (!iface.isEmpty())
        config_->saveValue(QStringLiteral("network"), QStringLiteral("interface"), iface);
}

// The LINK export serves the database that the browser shows (the last local one), so there is nothing to choose: tick it or not.
void MainWindow::setLinkExportEnabled(bool on)
{
    if (on) exportNone_ = false;                         // asking for the export again ends "No pendrive"
    linkExportWanted_ = on;
    config_->saveValue(QStringLiteral("link_export"), QStringLiteral("enabled"), on ? QStringLiteral("1") : QStringLiteral("0"));
    if (linkExportAction_) linkExportAction_->setChecked(on);
    applyLinkExport();
}

void MainWindow::applyLinkExport()
{
    if (exportNone_) return;                             // "No pendrive" stays until another one is chosen
    const QString root = !explicitExportFolder_.isEmpty() ? explicitExportFolder_ : (trackBrowser_ && !trackBrowser_->isRemote() ? trackBrowser_->exportRoot() : QString());
    if (linkExportWanted_ && root.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("LINK export: open a rekordbox folder first (File > Open Rekordbox Folder), it exports that database"), 8000);
        network_->setLinkExportFolder(QString());
        return;
    }
    network_->setLinkExportFolder(linkExportWanted_ ? root : QString());
    if (!linkExportWanted_) { statusBar()->showMessage(QStringLiteral("LINK export off"), 4000); return; }
    statusBar()->showMessage(QStringLiteral("LINK export: serving %1%2").arg(root, network_->isConnected() ? QString()
                                                                              : QStringLiteral(" (starts when you connect)")), 6000);
}

PendriveRegistry* MainWindow::pendrives()
{
    if (!pendrives_) {
        pendrives_ = new PendriveRegistry(this);
        pendrives_->load(config_);
        connect(pendrives_, &PendriveRegistry::changed, this, [this]() { pendrives_->save(config_); });
        connect(pendrives_, &PendriveRegistry::physicalChanged, this, [this]() {
            // a stick pulled out from under the export: the slot goes empty (the decks notice the same way as for USB STOP)
            LinkExport* le = network_->linkExport();
            if (le && le->running() && le->usbState() == LinkExport::UsbState::Ready && !PendriveRegistry::isRekordboxFolder(le->folder()))
                le->forceEmpty(QStringLiteral("the pendrive was unplugged"));
        });
        pendrives_->startWatchingPhysical();
        QObject::connect(network_, &NetworkManager::linkExportMounted, this, [this](const QString& folder, const QString& name, bool mounted) {
            statusBar()->showMessage(mounted ? QStringLiteral("LINK export: %1 mounted (%2)").arg(name, folder)
                                             : QStringLiteral("LINK export: %1 unmounted, the slot is empty").arg(name), 6000);
            if (pendrivesDialog_) pendrivesDialog_->setExported(folder, mounted ? QStringLiteral("exported") : QStringLiteral("ejected"));
        });
    }
    return pendrives_;
}

// The decks refuse USB STOP while one uses the stick; dim the button then, but keep it pressable (the decks show who uses it)
void MainWindow::updatePendriveBar()
{
    if (!pendriveBar_) return;
    QList<PendriveToolBar::RemoteDeck> decks;                    // the decks of the network can be looked at from the same list
    if (network_ && network_->isConnected()) {
        for (int i = 0; i < deckArea_->deckCount(); ++i) {
            auto* deck = deckArea_->deckAt(i);
            if (!deck || isLocalDeck(deck->playerId())) continue;
            const QString ip = network_->ipForPlayer(deck->playerId());
            if (ip.isEmpty()) continue;
            decks.append({deck->playerId(), ip, QStringLiteral("Deck %1  %2").arg(deck->playerId()).arg(deck->model())});
        }
    }
    pendriveBar_->setRemoteDecks(decks);
    LinkExport* le = network_->linkExport();
    const bool live = le && le->running();
    pendriveBar_->setExported(live && le->usbState() != LinkExport::UsbState::Empty ? le->folder() : QString());
    if (!live) { pendriveBar_->setStopHint(false, QStringLiteral("the LINK export is not running")); return; }
    if (le->usbState() != LinkExport::UsbState::Ready) { pendriveBar_->setStopHint(false, QStringLiteral("the slot is already unmounting or empty")); return; }
    const QString why = le->busyReason();
    pendriveBar_->setStopHint(why.isEmpty(), why);
}

void MainWindow::showVirtualPendrives()
{
    if (!pendrivesDialog_) {
        pendrivesDialog_ = new VirtualPendrivesDialog(pendrives(), this);
        connect(pendrivesDialog_, &VirtualPendrivesDialog::openRequested, this, &MainWindow::openPendrive);
        connect(pendrivesDialog_, &VirtualPendrivesDialog::exportRequested, this, &MainWindow::exportPendrive);
        connect(pendrivesDialog_, &VirtualPendrivesDialog::ejectRequested, this, &MainWindow::ejectPendrive);
        LinkExport* le = network_->linkExport();
        if (le && le->running()) pendrivesDialog_->setExported(le->folder(), le->usbState() == LinkExport::UsbState::Empty ? QStringLiteral("ejected") : QStringLiteral("exported"));
    }
    pendrivesDialog_->show();
    pendrivesDialog_->raise();
    pendrivesDialog_->activateWindow();
}

void MainWindow::openPendrive(const QString& folder)
{
    QString err;
    if (!trackBrowser_->openRekordboxFolder(folder, &err)) {
        QMessageBox::warning(this, QStringLiteral("Open Pendrive"), err);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Loaded %1 in the browser").arg(folder), 5000);
}

// Swap the exported pendrive the way a deck does (USB STOP, pull, insert); the decks refuse while a deck uses the current one.
void MainWindow::exportPendrive(const QString& folder, const QString& nameIn)
{
    if (!PendriveRegistry::isRekordboxFolder(folder)) {
        QMessageBox::warning(this, QStringLiteral("Export Pendrive"), QStringLiteral("%1 has no PIONEER/rekordbox/export.pdb").arg(folder));
        return;
    }
    const QString name = nameIn.isEmpty() ? PendriveRegistry::defaultNameFor(folder) : nameIn;
    exportNone_ = false;
    explicitExportFolder_ = folder;
    explicitExportName_ = name;
    config_->saveValue(QStringLiteral("link_export"), QStringLiteral("pendrive_folder"), folder);
    linkExportWanted_ = true;
    if (linkExportAction_) linkExportAction_->setChecked(true);
    config_->saveValue(QStringLiteral("link_export"), QStringLiteral("enabled"), QStringLiteral("1"));
    if (!network_->isConnected()) {
        network_->setLinkExportFolder(folder);
        statusBar()->showMessage(QStringLiteral("LINK export: %1 will be served once you connect").arg(name), 6000);
        return;
    }
    const QString why = network_->linkExportSwitch(folder, name);
    if (!why.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("LINK export: not switched to %1: %2").arg(name, why), 8000);
        explicitExportFolder_.clear();                          // the old one is still mounted
        if (LinkExport* le = network_->linkExport()) explicitExportFolder_ = le->folder();
        return;
    }
    statusBar()->showMessage(QStringLiteral("LINK export: switching to %1").arg(name), 6000);
}

void MainWindow::ejectPendrive()
{
    if (!network_->linkExportRunning()) {
        statusBar()->showMessage(QStringLiteral("LINK export is not running, nothing to eject"), 4000);
        return;
    }
    const QString why = network_->linkExportUnmount();
    if (why.isEmpty()) { exportNone_ = true; explicitExportFolder_.clear(); explicitExportName_.clear(); }         // like on a deck: stopped stays stopped
    statusBar()->showMessage(why.isEmpty() ? QStringLiteral("USB STOP: unmounting the exported pendrive")
                                           : QStringLiteral("USB STOP refused: %1").arg(why), 8000);
}

void MainWindow::pickAdHocFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Folder to export (contains PIONEER/rekordbox/export.pdb)"),
                                                          QStringLiteral("/media"));
    if (!dir.isEmpty()) exportPendrive(dir, QString());
}

void MainWindow::rebuildExportMenu()
{
    exportMenu_->clear();
    LinkExport* le = network_->linkExport();
    const QString current = le && le->running() ? le->folder() : explicitExportFolder_;
    auto add = [&](const QString& label, const QString& folder, const QString& name) {
        QAction* a = exportMenu_->addAction(label);
        a->setCheckable(true);
        a->setChecked(!folder.isEmpty() && QDir::cleanPath(folder) == QDir::cleanPath(current));
        a->setEnabled(PendriveRegistry::isRekordboxFolder(folder));
        connect(a, &QAction::triggered, this, [this, folder, name]() { exportPendrive(folder, name); });
    };
    QAction* nothing = exportMenu_->addAction("No pendrive (eject, USB STOP)");
    nothing->setCheckable(true);
    nothing->setChecked(exportNone_ || !(le && le->running() && le->usbState() != LinkExport::UsbState::Empty));
    nothing->setToolTip("Take the exported pendrive away from the decks and do not put one back until you choose another");
    connect(nothing, &QAction::triggered, this, [this]() {
        if (network_->linkExportRunning() && network_->linkExport() && network_->linkExport()->usbState() != LinkExport::UsbState::Empty) {
            const QString why = network_->linkExportUnmount();
            if (!why.isEmpty()) {
                statusBar()->showMessage(QStringLiteral("USB STOP refused: %1").arg(why), 8000);     // a deck is using it: nothing changes
                return;
            }
        }
        exportNone_ = true;
        explicitExportFolder_.clear();
        explicitExportName_.clear();
        statusBar()->showMessage(QStringLiteral("No pendrive is exported to the decks"), 5000);
    });
    QAction* none = exportMenu_->addAction("Whatever the browser shows");
    none->setCheckable(true);
    none->setChecked(!exportNone_ && explicitExportFolder_.isEmpty());
    connect(none, &QAction::triggered, this, [this]() {
        exportNone_ = false;
        explicitExportFolder_.clear();
        explicitExportName_.clear();
        setLinkExportEnabled(true);
    });
    exportMenu_->addSeparator();
    for (const auto& p : pendrives()->virtualPendrives()) add(p.name, p.folder, p.name);
    exportMenu_->addSeparator();
    for (const auto& p : PendriveRegistry::scanPhysical()) add(p.name + QStringLiteral(" (plugged in)"), p.mountPoint, p.name);
    exportMenu_->addSeparator();
    connect(exportMenu_->addAction("Other Folder..."), &QAction::triggered, this, &MainWindow::pickAdHocFolder);
    connect(exportMenu_->addAction("Manage Pendrives..."), &QAction::triggered, this, &MainWindow::showVirtualPendrives);
}

void MainWindow::setLinkExport(const QString& folder)         // --link-export DIR: open that folder in the browser and switch the export on
{
    QString err;
    if (!trackBrowser_->openRekordboxFolder(folder, &err)) {
        statusBar()->showMessage(QStringLiteral("LINK export: %1").arg(err), 8000);
        return;
    }
    setLinkExportEnabled(true);
}

void MainWindow::disconnectFromNetwork()
{
    if (!confirmLeavingLink(tr("Disconnect")))
        return;
    network_->disconnectFromLink();
}

QStringList MainWindow::liveImpact()
{
    QStringList reasons;
    if (!network_ || !network_->isConnected())
        return reasons;
    const QString usb = network_->usbInUseBy();
    if (!usb.isEmpty())
        reasons << tr("A deck is playing from our exported pendrive (%1): its audio would stop.").arg(usb);
    if (masterPid_ == localVdjId())
        reasons << tr("We are the master: the other decks follow our tempo and would lose it.");
    return reasons;
}

bool MainWindow::confirmLeavingLink(const QString& action)
{
    const QStringList reasons = liveImpact();
    if (reasons.isEmpty())
        return true;
    QMessageBox box(QMessageBox::Warning, tr("%1 while live").arg(action),
                    tr("%1 now would affect what the audience hears:\n\n%2").arg(action, reasons.join(QStringLiteral("\n"))),
                    QMessageBox::NoButton, this);
    QPushButton* go = box.addButton(tr("%1 anyway").arg(action), QMessageBox::DestructiveRole);
    QPushButton* stay = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(stay);
    box.setEscapeButton(stay);
    box.exec();
    return box.clickedButton() == go;
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
    if (!setVdjBpm(bpm) && bpmLockedTo()) return;
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
        if (!bpmLockedTo())       // locked: setVdjBpm has said so in the status bar
            statusBar()->showMessage(QStringLiteral("BPM %1 is out of range").arg(bpm, 0, 'f', 2), 4000);
        return;
    }
    statusBar()->showMessage(remote ? QStringLiteral("Copied BPM %1 from deck %2 to the VDJ and the ALSA clock")
                                          .arg(bpm, 0, 'f', 2).arg(playerId)
                                    : QStringLiteral("Copied master BPM %1").arg(bpm, 0, 'f', 2), 4000);
}

// The VDJ's tempo, as the user asks for it. While we are synced to a real deck (difflock) the tempo is that deck's and
// nothing else: the request is refused. Master or free-running, it is applied and the other players follow.
bool MainWindow::setVdjBpm(double bpm)
{
    if (const int locked = bpmLockedTo()) {
        statusBar()->showMessage(QStringLiteral("BPM is locked to deck %1 (turn Difflock off to set it)").arg(locked), 3000);
        return false;
    }
    return applyVdjBpm(bpm);
}

int MainWindow::bpmLockedTo() const
{
    return difflock_.locked() ? difflock_.player() : 0;
}

// The one place the VDJ's tempo changes: the ProLink beats we send, the ALSA sequencer queue that drum machines
// follow, and the VDJ's own deck row.
bool MainWindow::applyVdjBpm(double bpm)
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

// Synced to a real deck: the sequencer and the link tempo are that deck's BPM, whatever it does.
void MainWindow::followLockedBpm()
{
    const int locked = bpmLockedTo();
    if (!locked) return;
    DeckWidget* src = deckAt(locked);
    if (!src || isVdjRow(src)) return;
    const double bpm = src->bpm();
    if (!(bpm > 0.0) || (clock_ && std::abs(clock_->bpm() - bpm) < 0.004)) return;
    applyVdjBpm(bpm);
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

void MainWindow::cancelPendingStart()
{
    if (startTimer_) startTimer_->stop();
    pendingStartPid_ = 0;
}

void MainWindow::cancelPendingRestart()
{
    if (restartTimer_) restartTimer_->stop();
}

// The one entry point for restarting the sequencer (the drum machines start their pattern again, our bar position is 1).
// Quantized: wait for the sync deck's next downbeat so our bar 1 coincides with its bar; as the master, for our own next beat.
void MainWindow::vdjRestart(bool quantize)
{
    if (!clock_ || !clock_->running()) return;
    cancelPendingRestart();                          // a newer request replaces the pending one
    if (!quantize) {
        clock_->restart();
        updateSequencerPlayhead();
        return;
    }
    const double nowMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    bool fallback = false;
    const double wait = restartWaitMs(difflock_.player(), deckPhase_, nowMs, clock_->positionBeats(), clock_->effectiveBpm(), &fallback);
    if (fallback)
        statusBar()->showMessage(QStringLiteral("Quantize: sync deck not seen, restarting on our own beat"), 4000);
    else
        statusBar()->showMessage(QStringLiteral("Restart queued (%1 ms)").arg(qRound(wait)), 4000);
    restartTimer_->start(std::max(0, int(std::lround(wait))));
}

void MainWindow::setVdjPlaying(bool playing)
{
    if (!clock_) return;
    cancelPendingRestart();
    cancelPendingStart();
    if (playing) clock_->start();
    else clock_->stop();
    updateSequencerPlayhead();
}

// While the ALSA sequencer runs, the VDJ rows' four beat grids show its queue position instead of the network's.
void MainWindow::updateSequencerPlayhead()
{
    // barBeats = beats since start plus the whole-beat offset of ⏪/⏩: the label moves, the clock does not
    const double beats = clock_ ? clock_->barBeats() : -1.0;
    const int seqBarPos = clock_ ? clock_->barPos() : 0;
    // Subscribers change rarely; querying ALSA every 16 ms frame is needless.
    const bool refreshFollowers = !followersClock_.isValid() || followersClock_.elapsed() >= 500;
    if (refreshFollowers) {
        followersClock_.start();
        followerNames_ = clock_ ? clock_->subscriberNames() : QStringList();
    }
    for (int i = 0; i < deckArea_->deckCount(); ++i) {
        DeckWidget* d = deckArea_->deckAt(i);
        if (!isVdjRow(d)) continue;
        d->beatGrid()->setSequencerBeats(beats);
        if (d->controls())
            d->controls()->setJumpsEnabled(clock_ && clock_->running());
        // the bar position the VDJ sends on the link (broadcastBeat reads it from the controls) is the sequencer's
        if (seqBarPos > 0 && d->controls() && d->controls()->barPos() != seqBarPos)
            d->setBarPos(seqBarPos);
        if (refreshFollowers && d->diffLockBar())
            d->diffLockBar()->setSequencerFollowers(followerNames_, clock_ && clock_->running());
        if (refreshFollowers)
            d->setSequencerBpm(clock_ ? clock_->effectiveBpm() : 0.0);
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
    if (!confirmLeavingLink(tr("Quit"))) {
        event->ignore();
        return;
    }
    shutdown();
    QMainWindow::closeEvent(event);
}

void MainWindow::shutdown()
{
    if (rbPanel_)
        rbPanel_->stopMode();
    stopBpmRamp();
    if (recorder_ && recorder_->isRecording())
        recorder_->stop();
    if (midiIn_)
        midiIn_->stop();
    if (hud_)
        hud_->close();
    cancelPendingRestart();
    cancelPendingStart();
    if (joystick_)
        joystick_->stop();
    if (clock_) {
        clock_->stop();
    }
    // Graceful shutdown: stop timers, disconnect network (stops managed
    // threads and closes sockets) and release any active NFS mounts.
    if (demoClock_) {
        demoClock_->stop();
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
                QStringLiteral("Log: %1 entries").arg(PacketLogger::instance().entryCount()));
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
    // q_restart is always quantized (like adj): the sync deck's next downbeat, or our own next beat when we are the master
    connect(midiIn_, &MidiMapInput::restartRequested, this, [this]() { vdjRestart(true); });
    connect(midiIn_, &MidiMapInput::nudgeRequested, this, &MainWindow::nudgeSequencer);
    connect(midiIn_, &MidiMapInput::tempoAdjustRequested, this, &MainWindow::nudgeLocalBpm);
    connect(midiIn_, &MidiMapInput::copyMasterRequested, this,
            [this]() { copyBpmFromMaster(localVdjId()); });
    connect(midiIn_, &MidiMapInput::becomeMasterRequested, this,
            [this]() { network_->requestMaster(); });
    connect(instruments_, &InstrumentManager::wired, this,
            [this](const QString& d) { statusBar()->showMessage(QStringLiteral("MIDI: ") + d, 6000); });

    // A joystick does exactly what the MIDI map does (untested on real hardware: only its event map is unit tested)
    joystick_ = new JoystickSeqBridge(config_, this);
    connect(joystick_, &JoystickSeqBridge::toggleRequested, this, [this]() {
        setVdjTransport(!(clock_ && clock_->running()));
    });
    connect(joystick_, &JoystickSeqBridge::stopRequested, this, [this]() { setVdjTransport(false); });
    connect(joystick_, &JoystickSeqBridge::quantizedRestartRequested, this, [this]() { vdjRestart(true); });
    connect(joystick_, &JoystickSeqBridge::nudgeRequested, this, &MainWindow::nudgeSequencer);
    connect(joystick_, &JoystickSeqBridge::tempoAdjustRequested, this, &MainWindow::nudgeLocalBpm);
    joystick_->start();

    instruments_->setSequencer(clock_);
    instruments_->setRouter(midiRouter_);
    instruments_->loadProfiles();

    // The DJ Tools controller (nanoKONTROL ...): an .adjmm file with the stripN_* ops, next to or inside the deck map.
    djtoolzControl_ = new DjtoolzMidiControl(this);
    QString djtoolzMap = config_->loadValue(QStringLiteral("midi"), QStringLiteral("djtoolz_map"));
    if (djtoolzMap.isEmpty())
        djtoolzMap = ConfigManager::configDir() + QStringLiteral("/djtoolz-control.adjmm");
    DjtoolzMidiControl::installExample(djtoolzMap);          // only when the file does not exist
    if (!djtoolzControl_->loadFromFile(djtoolzMap))
        statusBar()->showMessage(tr("DJ Tools controller: cannot read %1").arg(djtoolzMap), 6000);
    wireDjtoolzControl(midiIn_, djtoolzPanel_, djtoolzControl_);
    connect(djtoolzPanel_, &DjtoolzPanel::controllerDialogRequested, this, [this]() {
        ControllerDialog dlg(djtoolzControl_, this);
        connect(&dlg, &ControllerDialog::saved, this, [this](const QString& p) {
            config_->saveValue(QStringLiteral("midi"), QStringLiteral("djtoolz_map"), p);
        });
        dlg.exec();
        checkDjtoolzControllerConnected();
    });
    applyMidiMap();
}

// Tells the human when no connected MIDI client matches the controller's "#! source" name.
void MainWindow::checkDjtoolzControllerConnected()
{
    const QString filter = djtoolzControl_->map().source();
    if (filter.isEmpty() || !midiIn_->isRunning())
        return;
    for (const QString& n : midiIn_->sourceNames())
        if (n.contains(filter, Qt::CaseInsensitive))
            return;
    statusBar()->showMessage(tr("DJ Tools controller: no MIDI client named \"%1\" is connected to the MIDI port - connect it with aconnect or Config > Instruments").arg(filter), 8000);
}

// The map comes from the input device it is paired with (Config > Instruments); without a pairing, from the
// default map search. The reader has to run for the controller to be wired to it, so a device without a map
// still gets the port.
void MainWindow::applyMidiMap()
{
    QString map = instruments_->midiMapFor(instruments_->midiInputKey());
    if (map.isEmpty() || !QFile::exists(map))
        map = defaultMidiMapPath();
    if (!map.isEmpty() && (map != midiIn_->mapPath() || !midiIn_->isRunning()))
        startMidiMap(map);
    else if (map.isEmpty() && !midiIn_->isRunning())
        startMidiMap(QString());                    // no deck map: the port is still there for the DJ Tools controller
    // The DJ Tools controller is its own device: its events drive only the DJ Tools, never the deck map
    const QString djtoolzName = instruments_->alsaNameFor(instruments_->djtoolzInputKey());
    midiIn_->setExcludedSource(djtoolzName);
    djtoolzControl_->setSourceOverride(djtoolzName);
    instruments_->setInputPort(midiIn_->clientId(), midiIn_->portId());
    instruments_->autowire();
}

void MainWindow::startMidiMap(const QString& path)
{
    if (midiIn_->start(path))
        statusBar()->showMessage(path.isEmpty() ? QStringLiteral("MIDI port ready (no deck map)") : QStringLiteral("MIDI map: %1").arg(path), 5000);
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

// ---- trakcrate ----

QDockWidget* MainWindow::trakcrateDock()
{
    if (tcDock_) return tcDock_;
    tcPanel_ = new TrakcratePanel(config_);
    tcDock_ = new QDockWidget(QStringLiteral("trakcrate"), this);
    tcDock_->setObjectName(QStringLiteral("trakcrateDock"));
    tcDock_->setWidget(tcPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, tcDock_);
    tcDock_->hide();
    auto retitle = [this](const QString& path) {
        const QString last = QFileInfo(QDir::cleanPath(path)).fileName();
        tcDock_->setWindowTitle(last.isEmpty() ? QStringLiteral("trakcrate") : QStringLiteral("trakcrate: %1").arg(last));
    };
    if (tcPanel_->hasLibrary()) retitle(QString::fromStdString(tcPanel_->library().root().string()));
    connect(tcPanel_, &TrakcratePanel::libraryChanged, this, retitle);
    connect(tcDock_, &QDockWidget::visibilityChanged, tcAction_, &QAction::setChecked);
    // an export the user asked to serve to the decks: add the folder to the virtual pendrives (the slot is chosen in that dialog)
    connect(tcPanel_, &TrakcratePanel::registerPendriveRequested, this, [this](const QString& name, const QString& folder) {
        const QString err = pendrives()->add(name, folder);
        statusBar()->showMessage(err.isEmpty() ? QStringLiteral("Registered \"%1\" as an HD pendrive copy").arg(name) : err, 8000);
    });
    // the track editor: shows the master track loaded in the local player
    tcEditor_ = new TrakcrateTrackEditor;
    tcPanel_->setPlaybackBlocker([this]() -> QString {
        // listening to playlists at home and running the VDJ both want the audio output and the network: one or the other
        if (network_ && network_->isConnected()) return tr("Disconnect from the Pro DJ Link network to play playlists here");
        return QString();
    });
    connect(tcPanel_, &TrakcratePanel::status, this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });
    tcEditor_->setConfig(config_);
    tcEditor_->setScripts(tcPanel_->scripts());
    connect(tcPanel_->scripts(), &ScriptManager::ran, this, [this] {
        if (playerPanel_) playerPanel_->reloadTrakcrateAnalysis(playerPanel_->trakcrateDir());        // the deck takes up a changed grid or cuemarks
    });
    tcEditorDock_ = new QDockWidget(QStringLiteral("trakcrate track"), this);
    tcEditorDock_->setObjectName(QStringLiteral("trakcrateTrackDock"));
    tcEditorDock_->setWidget(tcEditor_);
    addDockWidget(Qt::BottomDockWidgetArea, tcEditorDock_);
    tcEditorDock_->hide();
    connect(tcPanel_, &TrakcratePanel::loadRequested, this, [this](const QString& dir) {
        if (!playerPanel_) return;
        playerPanel_->setVisible(true);
        playerPanel_->loadTrakcrateTrack(dir);
    });
    connect(playerPanel_, &PlayerPanel::trakcrateTrackLoaded, this, [this](const QString& dir) {
        tcEditor_->setTrack(dir);                         // empty for a track from a pendrive: the editor goes read-only
        if (!dir.isEmpty()) tcEditorDock_->show();
    });
    connect(tcEditor_, &TrakcrateTrackEditor::trackChanged, tcPanel_, &TrakcratePanel::refresh);
    connect(tcPanel_, &TrakcratePanel::ratingEdited, this, [this](const QString& dir) {
        if (tcEditor_->trackDir() == dir) tcEditor_->setTrack(dir);          // the editor shows the new stars
    });
    connect(tcEditor_, &TrakcrateTrackEditor::gridChanged, this, [this](const QString& dir) {
        if (playerPanel_) playerPanel_->reloadTrakcrateAnalysis(dir);       // the deck uses the new grid at once
    });
    connect(tcEditor_, &TrakcrateTrackEditor::status, this, [this](const QString& msg) { statusBar()->showMessage(msg, 8000); });
    return tcDock_;
}

// ---- rekordbox PC mode ----

QDockWidget* MainWindow::rekordboxDock()
{
    if (rbDock_) return rbDock_;
    rbPanel_ = new RekordboxModePanel(config_);
    rbPanel_->setLinkConnectedProbe([this]() { return network_ && network_->isConnected(); });
    rbDock_ = new QDockWidget(QStringLiteral("Rekordbox PC mode"), this);
    rbDock_->setObjectName(QStringLiteral("rekordboxDock"));
    rbDock_->setWidget(rbPanel_);
    addDockWidget(Qt::RightDockWidgetArea, rbDock_);
    rbDock_->hide();
    connect(rbDock_, &QDockWidget::visibilityChanged, rbAction_, &QAction::setChecked);
    // both modes need UDP 50000-50002
    connect(rbPanel_, &RekordboxModePanel::runningChanged, this, [this](bool running) {
        if (connectAction_) connectAction_->setEnabled(!running);
    });
    return rbDock_;
}

void MainWindow::startRekordboxMode(const RbModeOptions& opts)
{
    rekordboxDock()->show();
    rbPanel_->applyOptions(opts);
    QString err;
    if (!rbPanel_->startMode(&err))
        QMessageBox::warning(this, QStringLiteral("Rekordbox PC mode"), err);
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
