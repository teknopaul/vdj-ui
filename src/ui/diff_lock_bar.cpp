#include "diff_lock_bar.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

namespace vdj {

DiffLockBar::DiffLockBar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    layout->addWidget(new QLabel(QStringLiteral("Difflock"), this));
    status_ = new QLabel(QStringLiteral("off"), this);
    status_->setMinimumWidth(210);
    status_->setToolTip(QStringLiteral("Click Sync on a deck to lock the sequencer to its beat"));
    layout->addWidget(status_);

    followers_ = new QLabel(QStringLiteral("Drums: none"), this);
    followers_->setToolTip(QStringLiteral("Devices subscribed to the vdj-ui MIDI clock port, and whether the "
                                          "clock is running (▶) or stopped (■)"));
    layout->addWidget(followers_);

    target_ = new QSpinBox(this);
    target_->setRange(-250, 250);
    target_->setValue(kDefaultTargetMs);
    target_->setSuffix(QStringLiteral(" ms"));
    target_->setToolTip(QStringLiteral("Offset to keep between the followed deck's beat and the sequencer's beat "
                                       "(difflock_ms). Positive: the sequencer's beat comes before the deck's."));
    layout->addWidget(target_);

    capture_ = new QPushButton(QStringLiteral("Capture"), this);
    capture_->setToolTip(QStringLiteral("Use the offset measured right now as the target (what adj does when you lock)"));
    layout->addWidget(capture_);

    followMaster_ = new QCheckBox(QStringLiteral("Follow master"), this);
    followMaster_->setToolTip(QStringLiteral("Lock to whichever deck is the master"));
    layout->addWidget(followMaster_);

    off_ = new QPushButton(QStringLiteral("Off"), this);
    off_->setToolTip(QStringLiteral("Stop locking to a deck"));
    layout->addWidget(off_);

    layout->addSpacing(12);
    layout->addWidget(new QLabel(QStringLiteral("Nudge"), this));
    for (int ms : { -10, -1, 1, 10 }) {
        auto* b = new QPushButton(ms > 0 ? QStringLiteral("+%1").arg(ms) : QString::number(ms), this);
        b->setAutoRepeat(false);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMaximumWidth(48);
        connect(b, &QPushButton::clicked, this, [this, ms]() { emit nudgeClicked(ms); });
        layout->addWidget(b);
        nudges_.append(b);
    }
    layout->addStretch(1);

    connect(target_, QOverload<int>::of(&QSpinBox::valueChanged), this, &DiffLockBar::targetChanged);
    connect(followMaster_, &QCheckBox::toggled, this, &DiffLockBar::followMasterToggled);
    connect(off_, &QPushButton::clicked, this, &DiffLockBar::offClicked);
    connect(capture_, &QPushButton::clicked, this, &DiffLockBar::captureClicked);
}

int DiffLockBar::targetMs() const { return target_->value(); }

void DiffLockBar::setTargetMs(int ms)
{
    QSignalBlocker block(target_);
    target_->setValue(ms);
}

bool DiffLockBar::followMaster() const { return followMaster_->isChecked(); }

void DiffLockBar::setFollowMaster(bool on)
{
    QSignalBlocker block(followMaster_);
    followMaster_->setChecked(on);
}

void DiffLockBar::setStatus(const QString& text) { status_->setText(text); }

void DiffLockBar::setSequencerFollowers(const QStringList& names, bool running)
{
    const QString text = names.isEmpty()
        ? QStringLiteral("Drums: none")
        : QStringLiteral("Drums: %1 %2").arg(names.join(QStringLiteral(", ")),
                                              running ? QStringLiteral("▶ playing") : QStringLiteral("■ stopped"));
    if (followers_->text() != text) followers_->setText(text);
    followers_->setStyleSheet(!names.isEmpty() && running ? QStringLiteral("color: #4caf50; font-weight: bold;") : QString());
}

QString DiffLockBar::followersText() const { return followers_->text(); }
QString DiffLockBar::status() const { return status_->text(); }

} // namespace vdj
