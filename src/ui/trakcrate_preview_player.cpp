#include "trakcrate_preview_player.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include <algorithm>

#include "audio/audio_player.h"
#include "audio/time_stretcher.h"
#include "config/config_manager.h"
#include "trakcrate/track.h"

namespace vdj {

namespace {
const QString kSection = QStringLiteral("trakcrate");
// the same glyphs as the deck player (player_panel.cpp)
const QString kPrevGlyph = QString::fromUtf8("\u23ee\ufe0f");
const QString kPlayGlyph = QString::fromUtf8("\u25b6\ufe0f");
const QString kPauseGlyph = QString::fromUtf8("\u23f8\ufe0f");
const QString kStopGlyph = QString::fromUtf8("\u23f9\ufe0f");
const QString kNextGlyph = QString::fromUtf8("\u23e9");
}

TrakcratePreviewPlayer::TrakcratePreviewPlayer(ConfigManager* config, QWidget* parent) : QWidget(parent), config_(config)
{
    player_ = new AudioPlayer(this);
    prev_ = new QPushButton(kPrevGlyph);
    play_ = new QPushButton(kPlayGlyph);
    stop_ = new QPushButton(kStopGlyph);
    next_ = new QPushButton(kNextGlyph);
    prev_->setToolTip(QStringLiteral("Previous track"));
    next_->setToolTip(QStringLiteral("Next track"));
    play_->setToolTip(QStringLiteral("Play / pause"));
    stop_->setToolTip(QStringLiteral("Stop and give the audio output back to the DJ decks"));
    for (QPushButton* b : {prev_, play_, stop_, next_}) {            // glyph buttons: as wide as they are tall
        b->setFixedWidth(b->fontMetrics().height() + 14);
        b->setFocusPolicy(Qt::NoFocus);
    }
    mode_ = new QComboBox;
    mode_->addItem(QStringLiteral("Natural BPM"));
    mode_->addItem(QStringLiteral("All at BPM"));
    mode_->setToolTip(QStringLiteral("Play every track at its own tempo, or all at one tempo (time stretched in memory; the files are never changed)"));
    bpm_ = new QDoubleSpinBox;
    bpm_->setRange(60.0, 200.0);
    bpm_->setDecimals(1);
    bpm_->setValue(128.0);
    bpm_->setSuffix(QStringLiteral(" BPM"));
    continue_ = new QCheckBox(QStringLiteral("Next playlist"));
    continue_->setToolTip(QStringLiteral("At the end of the list, carry on with the next playlist (or the next list of the pendrive)"));
    label_ = new QLabel(QStringLiteral("Preview player: choose a playlist or pendrive"));
    label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    for (QWidget* w : {static_cast<QWidget*>(prev_), static_cast<QWidget*>(play_), static_cast<QWidget*>(stop_), static_cast<QWidget*>(next_), static_cast<QWidget*>(mode_), static_cast<QWidget*>(bpm_), static_cast<QWidget*>(continue_)})
        row->addWidget(w);
    row->addWidget(label_, 1);

    if (config_) {
        mode_->setCurrentIndex(config_->loadValue(kSection, QStringLiteral("preview_mode")) == QLatin1String("fixed") ? 1 : 0);
        const double v = config_->loadValue(kSection, QStringLiteral("preview_bpm")).toDouble();
        if (v >= 60.0 && v <= 200.0) bpm_->setValue(v);
    }
    bpm_->setEnabled(mode_->currentIndex() == 1);

    connect(play_, &QPushButton::clicked, this, [this] {
        if (isActive()) togglePause();
        else emit playRequested();
    });
    connect(stop_, &QPushButton::clicked, this, &TrakcratePreviewPlayer::stop);
    connect(next_, &QPushButton::clicked, this, &TrakcratePreviewPlayer::next);
    connect(prev_, &QPushButton::clicked, this, &TrakcratePreviewPlayer::previous);
    connect(mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        bpm_->setEnabled(i == 1);
        if (config_) config_->saveValue(kSection, QStringLiteral("preview_mode"), i == 1 ? QStringLiteral("fixed") : QStringLiteral("natural"));
        applyTempo();
    });
    connect(bpm_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (config_) config_->saveValue(kSection, QStringLiteral("preview_bpm"), QString::number(v, 'f', 1));
        applyTempo();
    });
    connect(player_, &AudioPlayer::finished, this, &TrakcratePreviewPlayer::trackFinished);
    connect(player_, &AudioPlayer::stateChanged, this, [this](int) { updateButtons(); });
    connect(player_, &AudioPlayer::deviceError, this, [this](const QString& m) { emit status(QStringLiteral("Audio output: %1").arg(m)); });
    updateButtons();
}

bool TrakcratePreviewPlayer::isPlaying() const { return player_->state() == AudioPlayer::Playing; }
bool TrakcratePreviewPlayer::fixedBpm() const { return mode_->currentIndex() == 1; }
double TrakcratePreviewPlayer::targetBpm() const { return bpm_->value(); }

void TrakcratePreviewPlayer::setFixedBpm(bool on, double bpm)
{
    if (bpm >= 60.0 && bpm <= 200.0) bpm_->setValue(bpm);
    mode_->setCurrentIndex(on ? 1 : 0);
}

double TrakcratePreviewPlayer::trackBpm(const QString& dir)
{
    trakcrate::Track t;
    if (!trakcrate::loadTrack(dir.toStdString(), &t)) return 0.0;
    if (t.hasGrid) {
        const trakcrate::BeatGrid g = trakcrate::beatGridFrom(t.grid);
        if (!g.empty()) return g.bpm;
    }
    return QString::fromStdString(t.meta.get("bpm_tag")).toDouble();
}

double TrakcratePreviewPlayer::tempoFor(double trackBpm, double targetBpm)
{
    if (trackBpm <= 0 || targetBpm <= 0) return 1.0;
    return std::clamp(targetBpm / trackBpm, TimeStretcher::kMinTempo, TimeStretcher::kMaxTempo);
}

void TrakcratePreviewPlayer::applyTempo()
{
    tempo_ = 1.0;
    if (fixedBpm() && index_ >= 0) tempo_ = tempoFor(trackBpm(currentDir()), bpm_->value());
    player_->setTempo(tempo_);
}

void TrakcratePreviewPlayer::setAllowedHint(bool playable)
{
    viewPlayable_ = playable;
    updateButtons();
}

void TrakcratePreviewPlayer::updateButtons()
{
    const QString why = blocker_ ? blocker_() : QString();
    const bool allowed = why.isEmpty();
    play_->setEnabled(allowed && (isActive() || viewPlayable_));
    play_->setText(isActive() && isPlaying() ? kPauseGlyph : kPlayGlyph);
    stop_->setEnabled(isActive());
    prev_->setEnabled(isActive() && index_ > 0);
    next_->setEnabled(isActive() && index_ + 1 < dirs_.size());
    play_->setToolTip(allowed ? QStringLiteral("Play the list one track after the other") : why);
    if (!allowed && !isActive()) label_->setText(why);
}

bool TrakcratePreviewPlayer::playList(const QString& ownerKey, const QStringList& trackDirs, int startIndex, QString* err)
{
    auto fail = [&](const QString& m) { if (err) *err = m; emit status(m); return false; };
    const QString why = blocker_ ? blocker_() : QString();
    if (!why.isEmpty()) return fail(why);
    if (trackDirs.isEmpty()) return fail(QStringLiteral("There is nothing to play"));
    stop();
    dirs_ = trackDirs;
    owner_ = ownerKey;
    if (!startTrack(std::clamp(startIndex, 0, int(dirs_.size()) - 1), 1)) {
        const QString m = label_->text();
        stop();
        return fail(m);
    }
    return true;
}

// Starts index, or the next one in `direction` when that one cannot be played. False when none of the rest can.
bool TrakcratePreviewPlayer::startTrack(int index, int direction)
{
    for (; index >= 0 && index < dirs_.size(); index += direction) {
        trakcrate::Track t;
        const QString dir = dirs_[index];
        const trakcrate::fs::path audio = trakcrate::audioFileOf(dir.toStdString());
        QString error;
        if (audio.empty() || !trakcrate::loadTrack(dir.toStdString(), &t) || !player_->load(QString::fromStdString(audio.string()), &error)) {
            label_->setText(QStringLiteral("Cannot play %1").arg(dir.section(QLatin1Char('/'), -1)));
            continue;
        }
        index_ = index;
        applyTempo();
        QString title = QString::fromStdString(t.title());
        if (title.isEmpty()) title = dir.section(QLatin1Char('/'), -1);
        const QString artist = QString::fromStdString(t.meta.get("artist"));
        label_->setText(QStringLiteral("%1/%2  %3%4%5")
                            .arg(index + 1).arg(dirs_.size(), 0, 10).arg(artist.isEmpty() ? QString() : artist + QStringLiteral(" - "), title,
                                 tempo_ != 1.0 ? QStringLiteral("  (%1 BPM)").arg(trackBpm(dir) * tempo_, 0, 'f', 1) : QString()));
        player_->play();
        updateButtons();
        emit trackStarted(owner_, index_, dir);
        return true;
    }
    return false;
}

void TrakcratePreviewPlayer::trackFinished()
{
    if (!isActive()) return;
    if (!startTrack(index_ + 1, 1)) {                      // the end of the list
        const QString owner = owner_;
        if (continue_->isChecked()) {
            emit endOfList(owner);                         // the owner starts the next list; if it does not, the player stops
            if (isActive() && owner_ == owner && index_ + 1 >= dirs_.size() && !isPlaying()) stop();
            return;
        }
        stop();
        label_->setText(QStringLiteral("Finished"));
    }
}

void TrakcratePreviewPlayer::next()
{
    if (isActive() && !startTrack(index_ + 1, 1)) stop();
}

void TrakcratePreviewPlayer::previous()
{
    if (!isActive()) return;
    if (player_->positionMs() > 3000) { player_->seekMs(0); return; }      // like a CD player: first back to the start of the track
    if (index_ > 0) startTrack(index_ - 1, -1);
}

void TrakcratePreviewPlayer::togglePause()
{
    if (!isActive()) return;
    const QString why = blocker_ ? blocker_() : QString();
    if (!why.isEmpty() && !isPlaying()) { stop(); return; }
    if (isPlaying()) player_->pause(); else player_->play();
    updateButtons();
}

void TrakcratePreviewPlayer::stop()
{
    const bool was = isActive();
    player_->stop();
    player_->unload();
    dirs_.clear();
    owner_.clear();
    index_ = -1;
    tempo_ = 1.0;
    if (was) label_->setText(QStringLiteral("Stopped"));
    updateButtons();
    if (was) emit stopped();
}

} // namespace vdj
