#include "bpm_ramp_widget.h"
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>

namespace vdj {

BpmRampWidget::BpmRampWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(6);

    auto* titleLabel = new QLabel("BPM Ramp:", this);
    titleLabel->setStyleSheet("color: #8a8a8a;");

    targetSpin_ = new QDoubleSpinBox(this);
    targetSpin_->setDecimals(2);
    targetSpin_->setRange(60.0, 200.0);
    targetSpin_->setValue(128.00);
    targetSpin_->setSingleStep(1.0);
    targetSpin_->setPrefix("→ ");
    targetSpin_->setToolTip("Target BPM");

    auto* barsLabel = new QLabel("over", this);
    barsLabel->setStyleSheet("color: #8a8a8a;");

    barsSpin_ = new QSpinBox(this);
    barsSpin_->setRange(1, 256);
    barsSpin_->setValue(32);
    barsSpin_->setSuffix(" bars");
    barsSpin_->setToolTip("Number of bars over which to ramp");

    applyBtn_ = new QPushButton("Apply", this);
    applyBtn_->setToolTip("Apply gradual BPM change (VDJ must be master)");

    plusOne_ = new QPushButton("+1", this);
    plusOne_->setMaximumWidth(36);
    plusOne_->setToolTip("Nudge BPM up by 1");

    minusOne_ = new QPushButton("-1", this);
    minusOne_->setMaximumWidth(36);
    minusOne_->setToolTip("Nudge BPM down by 1");

    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet("color: #5a8a5a; font-style: italic;");

    layout->addWidget(titleLabel);
    layout->addWidget(minusOne_);
    layout->addWidget(plusOne_);
    layout->addSpacing(8);
    layout->addWidget(targetSpin_);
    layout->addWidget(barsLabel);
    layout->addWidget(barsSpin_);
    layout->addWidget(applyBtn_);
    layout->addWidget(statusLabel_);
    layout->addStretch(1);

    connect(applyBtn_, &QPushButton::clicked, this, [this]() {
        emit rampRequested(targetSpin_->value(), barsSpin_->value());
    });
    connect(plusOne_, &QPushButton::clicked, this, [this]() {
        emit bpmNudged(1.0);
    });
    connect(minusOne_, &QPushButton::clicked, this, [this]() {
        emit bpmNudged(-1.0);
    });

    setEnabled(false); // greyed until VDJ becomes master
}

void BpmRampWidget::setCurrentBpm(double bpm)
{
    targetSpin_->setValue(bpm);
}

void BpmRampWidget::setStatusText(const QString& text)
{
    statusLabel_->setText(text);
}

} // namespace vdj
