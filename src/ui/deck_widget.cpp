#include "deck_widget.h"
#include "beat_grid_widget.h"
#include "deck_controls.h"
#include "diff_lock_bar.h"
#include "pitch_slider.h"
#include "debug_overlay.h"
#include <QPainter>
#include <QPen>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

namespace vdj {

DeckWidget::DeckWidget(int playerId, QWidget* parent)
    : QWidget(parent), playerId_(playerId)
{
    setMinimumHeight(110);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    titleLabel_ = new QLabel(this);
    titleLabel_->setTextFormat(Qt::PlainText);
    layout->addWidget(titleLabel_);

    beatGrid_ = new BeatGridWidget(this);
    layout->addWidget(beatGrid_);

    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(10);

    controls_ = new DeckControls(playerId_, this);
    pitchSlider_ = new PitchSlider(this);

    // The beat grid must open wide enough to show all 4 beats of a bar.
    // Base its minimum width on the player controls (button) width so the
    // timeline is never narrower than the controls beneath it.
    beatGrid_->setMinimumWidth(controls_->sizeHint().width());

    bottom->addWidget(controls_, 3);
    bottom->addWidget(pitchSlider_, 1);
    layout->addLayout(bottom);

    diffBar_ = new DiffLockBar(this);
    layout->addWidget(diffBar_);

    connect(controls_, &DeckControls::playPauseClicked, this,
            [this](int pid, bool play) { emit playPauseClicked(pid, play); });
    connect(controls_, &DeckControls::syncClicked, this,
            [this](int pid) { emit syncClicked(pid); });
    connect(controls_, &DeckControls::setBpmClicked, this,
            [this](int pid, double bpm) { emit setBpmClicked(pid, bpm); });
    connect(controls_, &DeckControls::becomeMasterClicked, this,
            [this](int pid) { emit becomeMasterClicked(pid); });
    connect(controls_, &DeckControls::bpmFromMasterClicked, this,
            [this](int pid) { emit bpmFromMasterClicked(pid); });
    connect(controls_, &DeckControls::broadcastBeatClicked, this,
            [this](int pid, double bpm, int barPos) { emit broadcastBeatClicked(pid, bpm, barPos); });
    connect(pitchSlider_, &PitchSlider::pitchChanged, this,
            [this](double pct) { emit pitchChanged(playerId_, pct); });

    overlay_ = new DebugOverlay(this);
    overlay_->setGeometry(rect());
    overlay_->hide();

    updateTitle();
}

void DeckWidget::setModel(const QString& model)
{
    if (model_ != model) {
        model_ = model;
        updateTitle();
    }
}

void DeckWidget::setIp(const QString& ip)
{
    if (ip_ != ip) {
        ip_ = ip;
        updateTitle();
    }
}

void DeckWidget::setRemote(bool remote)
{
    if (remote_ == remote)
        return;
    remote_ = remote;
    if (controls_)
        controls_->setRemote(remote);
    // No pitch control on remote decks: they are not our transport.
    if (pitchSlider_)
        pitchSlider_->setVisible(!remote);
    if (diffBar_)
        diffBar_->setVisible(!remote);      // difflock is the VDJ's: the panel only on the local deck
}

QString DeckWidget::titleText() const
{
    return titleLabel_ ? titleLabel_->text() : QString();
}

void DeckWidget::updateTitle()
{
    QString state = playing_ ? QStringLiteral("PLAYING") : QStringLiteral("STOPPED");
    QStringList parts;
    parts << QStringLiteral("DECK %1").arg(playerId_);
    if (!model_.isEmpty())
        parts << model_;
    if (!ip_.isEmpty())
        parts << ip_;
    parts << state;
QString text = parts.join(QStringLiteral("  "));
    if (sync_)
        text += QStringLiteral("  [SYNC]");
    if (master_) {
        text += QStringLiteral("  [MASTER]");
        titleLabel_->setStyleSheet(QStringLiteral("color: #ffd54a;"));
    } else {
        titleLabel_->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
    }
    titleLabel_->setText(text);
}

void DeckWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (overlay_)
        overlay_->setGeometry(rect());
}

void DeckWidget::setDebugMode(bool enabled)
{
    if (overlay_)
        overlay_->setVisible(enabled);
}

void DeckWidget::setDebugOverlay(const QStringList& lines)
{
    if (overlay_)
        overlay_->setLines(lines);
}

BeatGridWidget* DeckWidget::beatGrid() const
{
    return beatGrid_;
}

DeckControls* DeckWidget::controls() const
{
    return controls_;
}

double DeckWidget::pitchPercentage() const
{
    return pitch_;
}

void DeckWidget::setPitchPercentage(double percentage)
{
    pitch_ = percentage;
    if (pitchSlider_) pitchSlider_->setPercentage(percentage);
}

void DeckWidget::setPlayerId(int id)
{
    playerId_ = id;
    if (controls_) controls_->setPlayerId(id);
    updateTitle();
    update();
}

void DeckWidget::setBpm(double bpm)
{
    if (bpm_ != bpm) {
        bpm_ = bpm;
        beatGrid_->setBpm(bpm);
        if (controls_) controls_->setBpm(bpm);
        update();
    }
}

void DeckWidget::setMaster(bool master)
{
    if (master_ != master) {
        master_ = master;
        updateTitle();
        update();
    }
}

void DeckWidget::setSync(bool sync)
{
    if (sync_ != sync) {
        sync_ = sync;
        updateTitle();
        update();
    }
}

void DeckWidget::setPlaying(bool playing)
{
    if (playing_ != playing) {
        playing_ = playing;
        beatGrid_->setPlaying(playing);
        if (controls_) controls_->setPlaying(playing);
        updateTitle();
        update();
    }
}

void DeckWidget::setBeatTimestamp(qint64 millisSinceEpoch)
{
    beatGrid_->setBeatTimestamp(millisSinceEpoch);
}

void DeckWidget::setBarPos(int barPos)
{
    beatGrid_->setBarPos(barPos);
    if (controls_) controls_->setBarPos(barPos);
}

void DeckWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);

    QRect bg = rect();
    // The deck that really is master is highlighted yellow; all other decks
    // stay greyed out. (master_ is only set from network masterState info.)
    p.fillRect(bg, master_ ? QColor(0x2c, 0x27, 0x0b) : QColor(0x1c, 0x1c, 0x1c));

    if (master_) {
        p.setPen(QPen(QColor(0xff, 0xd5, 0x4a), 2));
        p.drawRect(bg.adjusted(1, 1, -2, -2));
    } else {
        p.setPen(QColor(0x33, 0x33, 0x33));
        p.drawLine(0, height() - 1, width(), height() - 1);
    }
}

void DeckWidget::mousePressEvent(QMouseEvent* event)
{
    emit deckClicked(playerId_);
    QWidget::mousePressEvent(event);
}

} // namespace vdj
