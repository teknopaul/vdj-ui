#include "player_panel.h"

#include <QKeyEvent>
#include "waveform_widget.h"
#include "audio/audio_output.h"
#include "audio/audio_player.h"
#include "audio/quantize_settings.h"
#include "quantize_button.h"
#include "trakcrate_analysis.h"
#include "trakcrate/track.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace vdj {

static const QString kPlayGlyph = QString::fromUtf8("\u25b6\ufe0f");
static const QString kPauseGlyph = QString::fromUtf8("\u23f8\ufe0f");

static QString mmss(double ms)
{
    const qint64 t = qint64(std::max(0.0, ms));
    return QStringLiteral("%1:%2.%3").arg(t / 60000).arg(t / 1000 % 60, 2, 10, QChar('0')).arg(t % 1000 / 100);
}

PlayerPanel::PlayerPanel(QWidget* parent)
    : QWidget(parent)
{
    player_ = new AudioPlayer(this);
    setFocusPolicy(Qt::StrongFocus);                     // Space, Page Up and Page Down drive this player when it has the focus; the trakcrate list forwards the same keys

    auto* top = new QHBoxLayout;
    title_ = new QLabel(QStringLiteral("Player: double click a track to load it"), this);
    time_ = new QLabel(QStringLiteral("0:00.0 / 0:00.0"), this);
    device_ = new QComboBox(this);
    device_->setToolTip(QStringLiteral("Audio output device"));
    device_->setMinimumContentsLength(24);
    top->addWidget(title_, 1);
    top->addWidget(time_);
    top->addWidget(new QLabel(QStringLiteral("Output:"), this));
    top->addWidget(device_);

    auto button = [this](const QString& glyph, const QString& tip) {
        auto* b = new QPushButton(glyph, this);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedSize(30, 22);
        b->setStyleSheet(QStringLiteral("QPushButton { padding: 0px; }"));
        return b;
    };
    back_ = button(QString::fromUtf8("\u23ee\ufe0f"), QStringLiteral("Back one bar (4 beats), Page Up"));
    play_ = button(kPlayGlyph, QStringLiteral("Play / pause, Space"));
    forward_ = button(QString::fromUtf8("\u23e9"), QStringLiteral("Forward one bar (4 beats), Page Down"));
    stop_ = button(QString::fromUtf8("\u23f9\ufe0f"), QStringLiteral("Stop and return to the start"));
    auto* transportRow = new QHBoxLayout;
    transportRow->addWidget(back_);
    transportRow->addWidget(play_);
    transportRow->addWidget(forward_);
    auto* transport = new QVBoxLayout;
    transport->addLayout(transportRow);
    transport->addWidget(stop_);

    waveform_ = new WaveformWidget(this);
    waveform_->setSeekable(true);
    waveform_->setMinimumHeight(90);

    bpmDown_ = new QPushButton(QStringLiteral("−"), this);
    bpmUp_ = new QPushButton(QStringLiteral("+"), this);
    bpmDown_->setToolTip(QStringLiteral("Decrease BPM"));
    bpmUp_->setToolTip(QStringLiteral("Increase BPM"));
    bpmDown_->setAutoRepeat(true);
    bpmUp_->setAutoRepeat(true);
    bpmDown_->setFocusPolicy(Qt::NoFocus);
    bpmUp_->setFocusPolicy(Qt::NoFocus);
    for (QPushButton* b : {bpmDown_, bpmUp_}) {
        b->setFixedSize(30, 22);
        b->setStyleSheet(QStringLiteral("QPushButton { padding: 0px; }"));
    }
    bpmReset_ = new QPushButton(this);
    bpmReset_->setIcon(QIcon(QStringLiteral(":/img/view-refresh-22x22.png")));
    bpmReset_->setToolTip(QStringLiteral("Reset the BPM to the track's own BPM"));
    bpmReset_->setFocusPolicy(Qt::NoFocus);
    bpmReset_->setFixedSize(30, 22);
    bpmReset_->setStyleSheet(QStringLiteral("QPushButton { padding: 0px; }"));
    bpm_ = new QLabel(QStringLiteral("---.-"), this);
    bpm_->setAlignment(Qt::AlignCenter);
    bpm_->setToolTip(QStringLiteral("Double click to type a BPM"));
    bpm_->installEventFilter(this);
    QFont f = bpm_->font();
    f.setPointSize(f.pointSize() + 6);
    f.setBold(true);
    bpm_->setFont(f);
    bpm_->setMinimumWidth(30 * 2 + 20);   // two 30px buttons + 20px, so the whole BPM fits
    bpmLock_ = new QPushButton(bpm_);                      // top right corner of the BPM display, kept there by eventFilter
    bpmLock_->setIcon(QIcon(QStringLiteral(":/img/lock-16x16.png")));
    bpmLock_->setIconSize(QSize(12, 12));
    bpmLock_->setCheckable(true);
    bpmLock_->setFlat(true);
    bpmLock_->setFocusPolicy(Qt::NoFocus);
    bpmLock_->setFixedSize(16, 16);
    bpmLock_->setToolTip(QStringLiteral("Lock BPM: keep the current BPM when a new track is loaded"));
    bpmLock_->setStyleSheet(QStringLiteral("QPushButton { padding: 0px; border: none; background: transparent; }"
                                           "QPushButton:checked { background: #ff3030; border-radius: 3px; }"));
    bpmLock_->move(bpm_->width() - bpmLock_->width(), 0);
    auto* bpmBox = new QVBoxLayout;
    bpmBox->addWidget(new QLabel(QStringLiteral("BPM"), this), 0, Qt::AlignHCenter);
    bpmBox->addWidget(bpm_);
    auto* bpmButtons = new QHBoxLayout;
    bpmButtons->addWidget(bpmDown_);
    bpmButtons->addWidget(bpmUp_);
    bpmButtons->addWidget(bpmReset_);
    bpmBox->addLayout(bpmButtons);

    auto* middle = new QHBoxLayout;
    middle->addLayout(transport);
    middle->addWidget(waveform_, 1);
    middle->addLayout(bpmBox);

    quantize_ = makeQuantizeButton(this);
    quantize_->setToolTip(QStringLiteral("Quantize (q)"));

    cueLayout_ = new QHBoxLayout;
    cueLayout_->addWidget(quantize_);
    cueLayout_->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->addLayout(top);
    layout->addLayout(middle);
    layout->addLayout(cueLayout_);

    refreshDevices();
    connect(device_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        player_->setDevice(device_->currentData().toString());
    });
    playTimer_ = new QTimer(this);
    playTimer_->setSingleShot(true);
    playTimer_->setTimerType(Qt::PreciseTimer);
    connect(playTimer_, &QTimer::timeout, this, [this]() {
        playPending_ = false;
        player_->play();
        updateButtons();
    });
    connect(play_, &QPushButton::clicked, this, &PlayerPanel::playClicked);
    connect(back_, &QPushButton::clicked, this, [this]() { jumpBeats(-4); });
    connect(forward_, &QPushButton::clicked, this, [this]() { jumpBeats(4); });
    connect(quantize_, &QPushButton::toggled, this, [this](bool on) {
        if (on && !clock_.isValid() && player_->isLoaded() && !warnedNoGrid_) {
            warnedNoGrid_ = true;
            emit status(QStringLiteral("No beat grid: cue quantize unavailable"));
        }
    });
    connect(player_, &AudioPlayer::jumped, this, [this](double) { emit status(QString()); });
    connect(stop_, &QPushButton::clicked, this, [this]() {
        cancelPendingPlay();
        player_->stop();
    });
    connect(waveform_, &WaveformWidget::seekRequested, player_, &AudioPlayer::seekMs);
    connect(bpmDown_, &QPushButton::clicked, this, [this]() { setTargetBpm(targetBpm_ - kBpmStep); });
    connect(bpmUp_, &QPushButton::clicked, this, [this]() { setTargetBpm(targetBpm_ + kBpmStep); });
    connect(bpmLock_, &QPushButton::toggled, this, [this](bool on) { if (on && targetBpm_ > 0) heldBpm_ = targetBpm_; });
    connect(bpmReset_, &QPushButton::clicked, this, [this]() { setTargetBpm(originalBpm_); });
    connect(player_, &AudioPlayer::positionChanged, this, [this](double ms) {
        waveform_->setPlayPosition(player_->isLoaded() ? ms : -1.0);
        time_->setText(QStringLiteral("%1 / %2").arg(mmss(ms), mmss(player_->lengthMs())));
        updatePhase(ms);
    });
    connect(player_, &AudioPlayer::stateChanged, this, [this](int) { updateButtons(); });
    connect(player_, &AudioPlayer::deviceError, this, &PlayerPanel::status);
    updateButtons();
    updateBpmLabel();
}

void PlayerPanel::refreshDevices()
{
    const QString current = device_->currentData().toString();
    device_->blockSignals(true);
    device_->clear();
    for (const AudioDevice& d : listAudioDevices())
        device_->addItem(d.description, d.name);
    const int i = device_->findData(current);
    device_->setCurrentIndex(i >= 0 ? i : 0);
    device_->blockSignals(false);
    player_->setDevice(device_->currentData().toString());
}

bool PlayerPanel::loadTrack(const TrackInfo& info)
{
    const bool ok = loadImpl(info, nullptr);
    if (!trakcrateDir_.isEmpty()) { trakcrateDir_.clear(); emit trakcrateTrackLoaded(QString()); }
    return ok;
}

bool PlayerPanel::loadTrakcrateTrack(const QString& trackDir)
{
    trakcrate::Track t;
    if (!trakcrate::loadTrack(trackDir.toStdString(), &t)) { emit status(QStringLiteral("Cannot read the track folder %1").arg(trackDir)); return false; }
    TrackInfo info;
    info.title = QString::fromStdString(t.title());
    if (info.title.isEmpty()) info.title = QString::fromStdString(t.dir.filename().string());
    info.audioPath = QString::fromStdString(trakcrate::audioFileOf(t.dir).string());
    const TrackAnalysis a = trakcrateTrackAnalysis(trackDir);
    const std::string bpmTag = t.meta.get("bpm_tag");
    if (!bpmTag.empty()) info.fields.append({QStringLiteral("BPM"), QString::fromStdString(bpmTag)});
    const bool ok = loadImpl(info, &a);
    trakcrateDir_ = ok ? trackDir : QString();
    emit trakcrateTrackLoaded(trakcrateDir_);
    return ok;
}

void PlayerPanel::reloadTrakcrateAnalysis(const QString& trackDir)
{
    if (trackDir.isEmpty() || trackDir != trakcrateDir_) return;
    analysis_ = trakcrateTrackAnalysis(trackDir);
    waveform_->setAnalysis(analysis_);
    setBeatGrid();                                        // the beat clock follows the new grid at once
    originalBpm_ = analysis_.avgBpm > 0 ? analysis_.avgBpm : originalBpm_;
    rebuildCueButtons();
    updateBpmLabel();
}

bool PlayerPanel::loadImpl(const TrackInfo& info, const TrackAnalysis* given)
{
    const double lockedBpm = bpmLock_->isChecked() ? heldBpm_ : 0.0;       // BPM lock: the new track plays at the BPM of the old one
    cancelPendingPlay();
    player_->unload();
    analysis_ = TrackAnalysis();
    setBeatGrid();
    waveform_->setPlayPosition(-1.0);
    if (info.ensureAnalysis && !info.analyzePath.isEmpty())
        info.ensureAnalysis(info.analyzePath);
    if (info.ensureFile && info.audioPath.startsWith(info.root)) {
        if (!info.ensureFile(info.audioPath.mid(info.root.size()))) {
            title_->setText(info.title);
            waveform_->setAnalysis(analysis_);
            emit status(QStringLiteral("Cannot read %1 from the deck").arg(info.title));
            updateButtons();
            return false;
        }
    }
    analysis_ = given ? *given : (info.analyzePath.isEmpty() ? TrackAnalysis() : loadTrackAnalysis(info.root, info.analyzePath));
    waveform_->setAnalysis(analysis_);
    title_->setText(info.title);

    QString error;
    if (info.audioPath.isEmpty() || !player_->load(info.audioPath, &error)) {
        analysis_ = TrackAnalysis();
        setBeatGrid();
        emit status(info.audioPath.isEmpty() ? QStringLiteral("No audio file for %1").arg(info.title)
                                             : QStringLiteral("Cannot play: %1").arg(error));
        originalBpm_ = targetBpm_ = 0.0;
        rebuildCueButtons();
        updateBpmLabel();
        updateButtons();
        return false;
    }
    setBeatGrid();
    originalBpm_ = analysis_.avgBpm;
    for (const auto& kv : info.fields)
        if (originalBpm_ <= 0 && kv.first == QLatin1String("BPM"))
            originalBpm_ = kv.second.toDouble();
    targetBpm_ = originalBpm_;
    player_->setTempo(1.0);
    if (lockedBpm > 0 && originalBpm_ > 0) {              // no clamping: this is a preview, if it sounds bad the DJ needs to hear that
        targetBpm_ = lockedBpm;
        player_->setTempo(lockedBpm / originalBpm_);
    }
    time_->setText(QStringLiteral("0:00.0 / %1").arg(mmss(player_->lengthMs())));
    rebuildCueButtons();
    updateBpmLabel();
    updateButtons();
    emit status(QStringLiteral("Loaded %1").arg(info.title));
    return true;
}

void PlayerPanel::setBeatGrid()
{
    QVector<double> ms;
    QVector<quint8> numbers;
    ms.reserve(analysis_.beats.size());
    numbers.reserve(analysis_.beats.size());
    for (const AnalysisBeat& b : analysis_.beats) {
        ms.append(b.ms);
        numbers.append(b.number);
    }
    clock_.setBeats(ms, numbers);
    phase_ = BeatPhase();
}

void PlayerPanel::setAnalysisForTest(const TrackAnalysis& analysis)
{
    analysis_ = analysis;
    setBeatGrid();
}

void PlayerPanel::updatePhase(double ms)
{
    const BeatClock::Position p = clock_.locate(ms);
    if (!p.valid) {
        phase_ = BeatPhase();
        return;
    }
    const bool changed = !phase_.valid || phase_.beatIndex != p.index;
    phase_.valid = true;
    phase_.beatIndex = p.index;
    phase_.beatNumber = p.beatNumber;
    phase_.sinceBeatMs = p.sinceBeatMs;
    phase_.fraction = p.beatLenMs > 0 ? p.sinceBeatMs / p.beatLenMs : 0.0;
    if (changed)
        emit beatChanged(p.beatNumber);
}

void PlayerPanel::jumpBeats(int beats)
{
    const double delta = beatJumpMs(originalBpm_, beats);
    if (!player_->isLoaded() || delta == 0.0)
        return;
    jumpToCue(std::clamp(player_->positionMs() + delta, 0.0, player_->lengthMs()));        // lands on the grid when Q is on
}

bool PlayerPanel::bpmLocked() const { return bpmLock_->isChecked(); }

bool PlayerPanel::isPlaying() const { return player_->state() == AudioPlayer::Playing; }

bool PlayerPanel::isPlayerKey(const QKeyEvent* e)
{
    return (e->key() == Qt::Key_Space || e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown) && !(e->modifiers() & ~Qt::KeypadModifier);
}

bool PlayerPanel::wantsPlayerKey(const QKeyEvent* e) const
{
    return isPlayerKey(e) && player_->isLoaded();
}

bool PlayerPanel::handlePlayerKey(QKeyEvent* e)
{
    if (!wantsPlayerKey(e))
        return false;
    if (e->key() == Qt::Key_Space)
        playClicked();
    else
        jumpBeats(e->key() == Qt::Key_PageDown ? 4 : -4);
    e->accept();
    return true;
}

const AnalysisCue* PlayerPanel::hotCueForKey(const QList<AnalysisCue>& cues, int key)
{
    if (key < Qt::Key_A || key > Qt::Key_C)
        return nullptr;
    const int hot = key - Qt::Key_A + 1;
    for (const AnalysisCue& c : cues)
        if (c.hotCue == hot)
            return &c;
    return nullptr;
}

bool PlayerPanel::isCueKey(const QKeyEvent* e) const
{
    return player_->isLoaded() && !(e->modifiers() & ~Qt::KeypadModifier) &&
           ((e->key() >= Qt::Key_A && e->key() <= Qt::Key_C) || e->key() == Qt::Key_Q);
}

// Space and Page Up / Down are shortcuts of the main window (play the VDJ deck), Up / Down nudge its pitch; while this panel has the focus
// Space and Page Up / Down drive this player and Up / Down hand the focus to the track list.
bool PlayerPanel::event(QEvent* e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        auto* k = static_cast<QKeyEvent*>(e);
        const bool upDown = (k->key() == Qt::Key_Up || k->key() == Qt::Key_Down) && !(k->modifiers() & ~Qt::KeypadModifier);
        if (isPlayerKey(k) || upDown || isCueKey(k)) {       // B is also the VDJ's copy BPM shortcut
            e->accept();
            return true;
        }
    }
    return QWidget::event(e);
}

void PlayerPanel::keyPressEvent(QKeyEvent* e)
{
    if (isPlayerKey(e)) {                                 // Space with nothing loaded does nothing here (it never reaches the VDJ while focused)
        if (!handlePlayerKey(e))
            e->accept();
        return;
    }
    if (isCueKey(e)) {
        if (e->key() == Qt::Key_Q) {
            quantize_->toggle();
        } else if (const AnalysisCue* c = hotCueForKey(analysis_.cues, e->key())) {
            jumpToCue(double(c->timeMs));
        } else {
            emit status(QStringLiteral("No cue %1").arg(QChar('A' + e->key() - Qt::Key_A)));
        }
        e->accept();
        return;
    }
    if ((e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) && !(e->modifiers() & ~Qt::KeypadModifier)) {
        emit focusTrackListRequested();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

double PlayerPanel::sequencerGridWaitMs() const
{
    if (!quantize_->isChecked() || !sequencer_ || !sequencer_->running())
        return -1.0;
    const int div = quantizeSettings_ ? quantizeSettings_->subdivisions() : 1;
    const qint64 now = BeatSource::steadyNowNs();
    const double beat = sequencer_->beatsAtSteadyNs(now);
    const double bpm = sequencer_->bpm();
    if (beat < 0.0 || bpm <= 0.0)
        return -1.0;
    const double gridMs = 60000.0 / bpm / div;
    const double nextGrid = std::ceil(beat * div - 1e-9) / div;
    const double wait = double(sequencer_->steadyNsAtBeat(nextGrid) - now) / 1e6;
    if (gridMs - wait < 5.0)                      // a grid point was only just passed: go now, a beat's wait would be silly
        return 0.0;
    return std::max(0.0, wait);
}

void PlayerPanel::cancelPendingPlay()
{
    if (playTimer_) playTimer_->stop();
    if (playPending_) {
        playPending_ = false;
        updateButtons();
    }
}

// Play pauses a playing deck and cancels a queued start; otherwise it starts, on the sequencer's next beat when Q is on and the sequencer runs.
void PlayerPanel::playClicked()
{
    if (playPending_) {
        cancelPendingPlay();
        return;
    }
    if (player_->state() == AudioPlayer::Playing) {
        player_->pause();
        return;
    }
    const double wait = sequencerGridWaitMs();
    if (wait <= 0.0) {
        player_->play();
        return;
    }
    playPending_ = true;
    playTimer_->start(int(std::lround(wait)));
    updateButtons();
    emit status(QStringLiteral("Play queued on the sequencer's beat (%1 ms)").arg(qRound(wait)));
}

void PlayerPanel::jumpToCue(double cueMs)
{
    if (player_->state() == AudioPlayer::Playing) {
        const double wait = sequencerGridWaitMs();
        if (wait > 0.0) {
            // the cue lands on the sequencer's next beat; the trigger is in source time, which runs `tempo` times faster than the wall clock
            const double tempo = player_->tempo() > 0 ? player_->tempo() : 1.0;
            player_->seekAtMs(player_->positionMs() + wait * tempo, cueMs);
            emit status(QStringLiteral("Cue queued on the sequencer's beat (%1 ms)").arg(qRound(wait)));
            return;
        }
    }
    if (!quantize_->isChecked() || player_->state() != AudioPlayer::Playing || !clock_.isValid()) {
        player_->seekMs(cueMs);
        return;
    }
    const int div = quantizeSettings_ ? quantizeSettings_->subdivisions() : 1;
    const double pos = player_->positionMs();     // read afresh: phase_ can be ~33 ms stale
    const QuantizedJump j = planQuantizedJump(clock_, div, pos, cueMs, player_->lengthMs());
    if (!j.wait) {
        player_->seekMs(j.targetMs);
        return;
    }
    player_->seekAtMs(j.triggerMs, j.targetMs);
    const double tempo = player_->tempo();
    emit status(QStringLiteral("Cue queued (%1 ms)").arg(qRound((j.triggerMs - pos) / (tempo > 0 ? tempo : 1.0))));
}

// Double click on the BPM display: type the BPM to play at.
bool PlayerPanel::eventFilter(QObject* o, QEvent* e)
{
    if (o == bpm_ && e->type() == QEvent::Resize) {
        bpmLock_->move(bpm_->width() - bpmLock_->width(), 0);
        return false;
    }
    if (o == bpm_ && e->type() == QEvent::MouseButtonDblClick && originalBpm_ > 0 && player_->isLoaded()) {
        bool ok = false;
        const double v = QInputDialog::getDouble(this, QStringLiteral("BPM"), QStringLiteral("Play at BPM:"), targetBpm_, 1.0, 999.0, 2, &ok);
        if (ok)
            setTargetBpm(v);
        return true;
    }
    return QWidget::eventFilter(o, e);
}

void PlayerPanel::setTargetBpm(double bpm)
{
    if (originalBpm_ <= 0 || !player_->isLoaded())
        return;
    const double lo = originalBpm_ * 0.5, hi = originalBpm_ * 2.0;
    targetBpm_ = std::clamp(std::round(bpm * 100.0) / 100.0, lo, hi);
    player_->setTempo(targetBpm_ / originalBpm_);
    updateBpmLabel();
}

void PlayerPanel::updateBpmLabel()
{
    if (targetBpm_ > 0)
        heldBpm_ = targetBpm_;
    bpm_->setText(targetBpm_ > 0 ? QString::number(targetBpm_, 'f', 1) : QStringLiteral("---.-"));
    const bool on = originalBpm_ > 0 && player_->isLoaded();
    bpmDown_->setEnabled(on);
    bpmUp_->setEnabled(on);
    bpmReset_->setEnabled(on);
}

void PlayerPanel::updateButtons()
{
    const bool loaded = player_->isLoaded();
    const auto s = player_->state();
    play_->setEnabled(loaded);
    play_->setText(s == AudioPlayer::Playing || playPending_ ? kPauseGlyph : kPlayGlyph);
    back_->setEnabled(loaded && originalBpm_ > 0);
    forward_->setEnabled(loaded && originalBpm_ > 0);
    quantize_->setEnabled(loaded);
    stop_->setEnabled(loaded && s != AudioPlayer::Stopped);
}

void PlayerPanel::rebuildCueButtons()
{
    for (QPushButton* b : cueButtons_) {
        cueLayout_->removeWidget(b);
        delete b;
    }
    cueButtons_.clear();
    if (!player_->isLoaded())
        return;
    for (const AnalysisCue& c : analysis_.cues) {
        const QString label = c.hotCue ? c.letter() : QStringLiteral("M");
        auto* b = new QPushButton(label, this);
        b->setStyleSheet(QStringLiteral(
            "QPushButton { background: #f4d963; color: black; font-weight: bold; border: 1px solid #b59a2a; border-radius: 3px; padding: 0px; }"
            "QPushButton:pressed { background: #d9bd45; }"));
        QString tip = QStringLiteral("%1 (%2)").arg(
            c.loop ? QStringLiteral("Loop: jump to the start of the loop") : QStringLiteral("Jump to this cue point"), mmss(c.timeMs));
        if (c.hotCue >= 1 && c.hotCue <= 3)
            tip += QStringLiteral(", key %1").arg(QChar('a' + c.hotCue - 1));
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedSize(26, 22);
        const quint32 t = c.timeMs;
        connect(b, &QPushButton::clicked, this, [this, t]() { jumpToCue(double(t)); });
        cueLayout_->insertWidget(cueLayout_->count() - 1, b);
        cueButtons_.append(b);
    }
}

} // namespace vdj
