#include "deck_controls.h"
#include <QPushButton>
#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QLabel>

namespace vdj {

DeckControls::DeckControls(int playerId, QWidget* parent)
    : QWidget(parent), playerId_(playerId)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    playPause_ = new QPushButton("Play", this);
    playPause_->setCheckable(true);
    playPause_->setChecked(false);   // decks start stopped: checked = playing, the label is what a click does
    sync_ = new QPushButton("Sync", this);
    becomeMaster_ = new QPushButton("Master", this);
    copyBpm_ = new QPushButton("Copy BPM", this);
    broadcastBeat_ = new QPushButton("<< Beat", this);

    auto* bpmLabel = new QLabel("BPM", this);
    bpmSpin_ = new QDoubleSpinBox(this);
    bpmSpin_->setDecimals(2);
    bpmSpin_->setRange(60.0, 200.0);
    bpmSpin_->setValue(128.00);
    bpmSpin_->setSingleStep(0.05);

    // Bar position is not shown as a field anymore: the beat grid timeline
    // above renders the bar/beat position, so the label clique here would just
    // be noise. The spinbox/storage is kept for the beat-broadcast plumbing.
    barPosSpin_ = new QSpinBox(this);
    barPosSpin_->setRange(0, 127);
    barPosSpin_->setValue(0);
    barPosSpin_->setReadOnly(true);
    barPosSpin_->hide();

    // Static BPM readout used in place of the BPM editor on remote decks.
    bpmValueLabel_ = new QLabel(QStringLiteral("BPM %1").arg(bpmSpin_->value(), 0, 'f', 2), this);
    bpmValueLabel_->setVisible(false);

    layout->addWidget(playPause_);
    layout->addWidget(sync_);
    layout->addWidget(becomeMaster_);
    layout->addWidget(copyBpm_);
    layout->addWidget(broadcastBeat_);
    layout->addSpacing(8);
    layout->addWidget(bpmLabel);
    layout->addWidget(bpmSpin_);
    layout->addWidget(bpmValueLabel_);
    layout->addStretch(1);

    connect(playPause_, &QPushButton::toggled, this, [this](bool on) {
        playPause_->setText(on ? "Pause" : "Play");
        emit playPauseClicked(playerId_, on);
    });
    connect(sync_, &QPushButton::clicked, this, [this]() {
        emit syncClicked(playerId_);
    });
    connect(becomeMaster_, &QPushButton::clicked, this, [this]() {
        emit becomeMasterClicked(playerId_);
    });
    connect(copyBpm_, &QPushButton::clicked, this, [this]() {
        emit bpmFromMasterClicked(playerId_);
    });
    connect(broadcastBeat_, &QPushButton::clicked, this, [this]() {
        emit broadcastBeatClicked(playerId_, bpmSpin_->value(), barPosSpin_->value());
    });
    connect(bpmSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
        emit setBpmClicked(playerId_, v);
    });
}

void DeckControls::setBpm(double bpm)
{
    if (bpm > 0) {
        bpmSpin_->setValue(bpm);
        bpmValueLabel_->setText(QStringLiteral("BPM %1").arg(bpm, 0, 'f', 2));
    }
}

double DeckControls::bpm() const
{
    return bpmSpin_->value();
}

void DeckControls::setPlaying(bool playing)
{
    playPause_->setChecked(playing);
}

void DeckControls::setBarPos(int barPos)
{
    barPosSpin_->setValue(barPos);
}

int DeckControls::barPos() const
{
    return barPosSpin_->value();
}

void DeckControls::setRemote(bool remote)
{
    if (remote_ == remote)
        return;
    remote_ = remote;

    // A remote CDJ cannot be controlled over the link: hide transport,
    // sync/master/broadcast actions and every editable field. Copy BPM stays
    // available (it adopts that deck's tempo into the local VDJ).
    playPause_->setVisible(!remote);
    sync_->setVisible(true);   // remote decks: Sync difflocks the VDJ sequencer to that deck
    sync_->setToolTip(remote ? QStringLiteral("Difflock the VDJ sequencer to this deck's beat")
                             : QStringLiteral("Sync the VDJ to the master"));
    becomeMaster_->setVisible(!remote);
    broadcastBeat_->setVisible(!remote);
    bpmSpin_->setVisible(!remote);
    barPosSpin_->setVisible(!remote);
    bpmValueLabel_->setVisible(remote);
    bpmValueLabel_->setText(QStringLiteral("BPM %1").arg(bpm(), 0, 'f', 2));
}

bool DeckControls::bpmEditable() const
{
    return bpmSpin_ && !bpmSpin_->isHidden() && !bpmSpin_->isReadOnly();
}

void DeckControls::setBpmLocked(bool locked, const QString& tip)
{
    bpmSpin_->setReadOnly(locked);
    bpmSpin_->setButtonSymbols(locked ? QAbstractSpinBox::NoButtons : QAbstractSpinBox::UpDownArrows);
    bpmSpin_->setToolTip(locked ? tip : QString());
}

bool DeckControls::barEditable() const
{
    return barPosSpin_ && !barPosSpin_->isHidden() && !barPosSpin_->isReadOnly();
}

bool DeckControls::playVisible() const
{
    return playPause_ && !playPause_->isHidden();
}

bool DeckControls::syncVisible() const
{
    return sync_ && !sync_->isHidden();
}

void DeckControls::setSyncLocked(bool locked)
{
    if (syncLocked_ == locked)
        return;
    syncLocked_ = locked;
    sync_->setText(locked ? QStringLiteral("Locked") : QStringLiteral("Sync"));
    sync_->setStyleSheet(locked ? QStringLiteral("QPushButton { background: #806000; color: white; }") : QString());
}

bool DeckControls::copyBpmVisible() const
{
    return copyBpm_ && !copyBpm_->isHidden();
}

} // namespace vdj