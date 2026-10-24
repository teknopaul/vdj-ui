#include "player_panel.h"
#include "waveform_widget.h"
#include "audio/audio_output.h"
#include "audio/audio_player.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>

namespace vdj {

static QString mmss(double ms)
{
    const qint64 t = qint64(std::max(0.0, ms));
    return QStringLiteral("%1:%2.%3").arg(t / 60000).arg(t / 1000 % 60, 2, 10, QChar('0')).arg(t % 1000 / 100);
}

PlayerPanel::PlayerPanel(QWidget* parent)
    : QWidget(parent)
{
    player_ = new AudioPlayer(this);

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

    auto button = [this](QStyle::StandardPixmap icon, const QString& tip) {
        auto* b = new QPushButton(style()->standardIcon(icon), QString(), this);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    play_ = button(QStyle::SP_MediaPlay, QStringLiteral("Play from the current position"));
    pause_ = button(QStyle::SP_MediaPause, QStringLiteral("Pause"));
    stop_ = button(QStyle::SP_MediaStop, QStringLiteral("Stop and return to the start"));
    auto* transport = new QVBoxLayout;
    transport->addWidget(play_);
    transport->addWidget(pause_);
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
    bpm_ = new QLabel(QStringLiteral("---.--"), this);
    bpm_->setAlignment(Qt::AlignCenter);
    QFont f = bpm_->font();
    f.setPointSize(f.pointSize() + 6);
    f.setBold(true);
    bpm_->setFont(f);
    auto* bpmBox = new QVBoxLayout;
    bpmBox->addWidget(new QLabel(QStringLiteral("BPM"), this), 0, Qt::AlignHCenter);
    bpmBox->addWidget(bpm_);
    auto* bpmButtons = new QHBoxLayout;
    bpmButtons->addWidget(bpmDown_);
    bpmButtons->addWidget(bpmUp_);
    bpmBox->addLayout(bpmButtons);

    auto* middle = new QHBoxLayout;
    middle->addLayout(transport);
    middle->addWidget(waveform_, 1);
    middle->addLayout(bpmBox);

    cueLayout_ = new QHBoxLayout;
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
    connect(play_, &QPushButton::clicked, player_, &AudioPlayer::play);
    connect(pause_, &QPushButton::clicked, player_, &AudioPlayer::pause);
    connect(stop_, &QPushButton::clicked, player_, &AudioPlayer::stop);
    connect(waveform_, &WaveformWidget::seekRequested, player_, &AudioPlayer::seekMs);
    connect(bpmDown_, &QPushButton::clicked, this, [this]() { setTargetBpm(targetBpm_ - kBpmStep); });
    connect(bpmUp_, &QPushButton::clicked, this, [this]() { setTargetBpm(targetBpm_ + kBpmStep); });
    connect(player_, &AudioPlayer::positionChanged, this, [this](double ms) {
        waveform_->setPlayPosition(player_->isLoaded() ? ms : -1.0);
        time_->setText(QStringLiteral("%1 / %2").arg(mmss(ms), mmss(player_->lengthMs())));
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
    player_->unload();
    waveform_->setPlayPosition(-1.0);
    if (info.ensureAnalysis && !info.analyzePath.isEmpty())
        info.ensureAnalysis(info.analyzePath);
    if (info.ensureFile && info.audioPath.startsWith(info.root)) {
        if (!info.ensureFile(info.audioPath.mid(info.root.size()))) {
            title_->setText(info.title);
            emit status(QStringLiteral("Cannot read %1 from the deck").arg(info.title));
            updateButtons();
            return false;
        }
    }
    analysis_ = info.analyzePath.isEmpty() ? TrackAnalysis() : loadTrackAnalysis(info.root, info.analyzePath);
    waveform_->setAnalysis(analysis_);
    title_->setText(info.title);

    QString error;
    if (info.audioPath.isEmpty() || !player_->load(info.audioPath, &error)) {
        emit status(info.audioPath.isEmpty() ? QStringLiteral("No audio file for %1").arg(info.title)
                                             : QStringLiteral("Cannot play: %1").arg(error));
        originalBpm_ = targetBpm_ = 0.0;
        rebuildCueButtons();
        updateBpmLabel();
        updateButtons();
        return false;
    }
    originalBpm_ = analysis_.avgBpm;
    for (const auto& kv : info.fields)
        if (originalBpm_ <= 0 && kv.first == QLatin1String("BPM"))
            originalBpm_ = kv.second.toDouble();
    targetBpm_ = originalBpm_;
    player_->setTempo(1.0);
    time_->setText(QStringLiteral("0:00.0 / %1").arg(mmss(player_->lengthMs())));
    rebuildCueButtons();
    updateBpmLabel();
    updateButtons();
    emit status(QStringLiteral("Loaded %1").arg(info.title));
    return true;
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
    bpm_->setText(targetBpm_ > 0 ? QString::number(targetBpm_, 'f', 2) : QStringLiteral("---.--"));
    const bool on = originalBpm_ > 0 && player_->isLoaded();
    bpmDown_->setEnabled(on);
    bpmUp_->setEnabled(on);
}

void PlayerPanel::updateButtons()
{
    const bool loaded = player_->isLoaded();
    const auto s = player_->state();
    play_->setEnabled(loaded && s != AudioPlayer::Playing);
    pause_->setEnabled(loaded && s == AudioPlayer::Playing);
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
        auto* b = new QPushButton(QStringLiteral("%1  %2").arg(label, mmss(c.timeMs)), this);
        b->setToolTip(c.loop ? QStringLiteral("Loop: jump to the start of the loop") : QStringLiteral("Jump to this cue point"));
        b->setFocusPolicy(Qt::NoFocus);
        const quint32 t = c.timeMs;
        connect(b, &QPushButton::clicked, this, [this, t]() { player_->seekMs(double(t)); });
        cueLayout_->insertWidget(cueLayout_->count() - 1, b);
        cueButtons_.append(b);
    }
}

} // namespace vdj
