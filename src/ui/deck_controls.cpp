#include "deck_controls.h"
#include <QPushButton>
#include "quantize_button.h"
#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QLabel>

namespace vdj {

static const QString kPlayGlyph = QString::fromUtf8("\u25b6\ufe0f");
static const QString kPauseGlyph = QString::fromUtf8("\u23f8\ufe0f");

DeckControls::DeckControls(int playerId, QWidget* parent)
    : QWidget(parent), playerId_(playerId)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    playPause_ = new QPushButton(kPlayGlyph, this);
    playPause_->setToolTip(QStringLiteral("Play / pause the sequencer"));
    quantize_ = makeQuantizeButton(this);
    restart_ = new QPushButton(QString::fromUtf8("\u23ee\ufe0f"), this);
    restart_->setToolTip(QStringLiteral("Restart at the start of the bar; with Q on, on the synced deck's next bar"));
    back_ = new QPushButton(QString::fromUtf8("\u23ea"), this);
    back_->setToolTip(QStringLiteral("Back one beat"));
    forward_ = new QPushButton(QString::fromUtf8("\u23e9"), this);
    forward_->setToolTip(QStringLiteral("Forward one beat"));
    for (QPushButton* b : { restart_, back_, forward_ }) {
        b->setFocusPolicy(Qt::NoFocus);
        b->setEnabled(false);
    }
    for (QPushButton* b : { playPause_, restart_, back_, forward_ }) {
        b->setFixedSize(26, 22);
        b->setStyleSheet(QStringLiteral("QPushButton { padding: 0px; }"));
    }
    playPause_->setCheckable(true);
    playPause_->setChecked(false);   // decks start stopped: checked = playing, the label is what a click does
    sync_ = new QPushButton("Sync", this);
    becomeMaster_ = new QPushButton("Master", this);
    copyBpm_ = new QPushButton("Copy BPM", this);
    broadcastBeat_ = new QPushButton("Send Beat", this);
    broadcastBeat_->setToolTip(QStringLiteral("Send this deck's BPM and bar position on the link"));

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
    bpmValueLabel_ = new QLabel(QString::number(bpmSpin_->value(), 'f', 2), this);
    bpmValueLabel_->setToolTip(QStringLiteral("The BPM the deck reports"));
    bpmValueLabel_->setVisible(false);
    measuredLabel_ = new QLabel(this);
    measuredLabel_->setToolTip(QStringLiteral("The BPM measured from the timing of the deck's beat packets"));
    measuredLabel_->setVisible(false);
    sequencerLabel_ = new QLabel(this);
    sequencerLabel_->setToolTip(QStringLiteral("The BPM of the ALSA sequencer (MIDI clock) the drum machines follow; "
                                               "the value to the left is what is sent on the link"));
    sequencerLabel_->setVisible(false);

    layout->addWidget(quantize_);
    layout->addWidget(restart_);
    layout->addWidget(back_);
    layout->addWidget(playPause_);
    layout->addWidget(forward_);
    layout->addWidget(sync_);
    layout->addWidget(becomeMaster_);
    layout->addWidget(copyBpm_);
    layout->addWidget(broadcastBeat_);
    layout->addSpacing(8);
    layout->addWidget(bpmLabel);
    layout->addWidget(bpmSpin_);
    layout->addWidget(bpmValueLabel_);
    layout->addWidget(measuredLabel_);
    layout->addWidget(sequencerLabel_);
    layout->addStretch(1);

    connect(playPause_, &QPushButton::toggled, this, [this](bool on) {
        playPause_->setText(on ? kPauseGlyph : kPlayGlyph);
        emit playPauseClicked(playerId_, on);
    });
    connect(restart_, &QPushButton::clicked, this, [this]() {
        emit restartClicked(playerId_, quantize_->isChecked());
    });
    connect(back_, &QPushButton::clicked, this, [this]() {
        emit beatJumpClicked(playerId_, -1);
    });
    connect(forward_, &QPushButton::clicked, this, [this]() {
        emit beatJumpClicked(playerId_, 1);
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
        bpmValueLabel_->setText(QString::number(bpm, 'f', 2));
    }
}

void DeckControls::setMeasuredBpm(double bpm)
{
    measuredLabel_->setText(bpm > 0 ? QStringLiteral("measured %1").arg(bpm, 0, 'f', 3) : QString());
    measuredLabel_->setVisible(remote_ && bpm > 0);
}

QString DeckControls::measuredText() const { return measuredLabel_->text(); }

void DeckControls::setSequencerBpm(double bpm)
{
    sequencerLabel_->setText(bpm > 0 ? QStringLiteral("seq %1").arg(bpm, 0, 'f', 3) : QString());
    sequencerLabel_->setVisible(!remote_ && bpm > 0);
}

QString DeckControls::sequencerText() const { return sequencerLabel_->text(); }

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
    quantize_->setVisible(!remote);
    restart_->setVisible(!remote);
    back_->setVisible(!remote);
    forward_->setVisible(!remote);
    sync_->setVisible(true);   // remote decks: Sync difflocks the VDJ sequencer to that deck
    sync_->setToolTip(remote ? QStringLiteral("Sync the VDJ to this deck")
                             : QStringLiteral("Sync the VDJ to the master"));
    becomeMaster_->setVisible(!remote);
    broadcastBeat_->setVisible(!remote);
    bpmSpin_->setVisible(!remote);
    barPosSpin_->setVisible(!remote);
    bpmValueLabel_->setVisible(remote);
    bpmValueLabel_->setText(QString::number(bpm(), 'f', 2));
    if (remote) sequencerLabel_->setVisible(false);
    else measuredLabel_->setVisible(false);
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

bool DeckControls::quantizeOn() const
{
    return quantize_->isChecked();
}

void DeckControls::setJumpsEnabled(bool enabled)
{
    restart_->setEnabled(enabled);
    back_->setEnabled(enabled);
    forward_->setEnabled(enabled);
}

bool DeckControls::jumpsVisible() const
{
    return !restart_->isHidden() && !back_->isHidden() && !forward_->isHidden() && !quantize_->isHidden();
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