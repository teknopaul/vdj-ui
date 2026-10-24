#include "pitch_slider.h"
#include <QSlider>
#include <QLabel>
#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <cmath>

namespace vdj {

PitchSlider::PitchSlider(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    valueLabel_ = new QLabel("+0.00%", this);
    valueLabel_->setMinimumWidth(48);
    valueLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(-800, 800); // -8.00% .. +8.00%
    slider_->setValue(0);
    slider_->setMinimumWidth(120);

    layout->addWidget(valueLabel_);
    layout->addWidget(slider_, 1);

    connect(slider_, &QSlider::valueChanged, this, [this](int v) {
        double pct = v / 100.0;
        valueLabel_->setText(QString("%1%2%3")
            .arg(pct >= 0 ? "+" : "")
            .arg(pct, 0, 'f', 2)
            .arg("%"));
        emit pitchChanged(pct);
    });
}

double PitchSlider::percentage() const
{
    return static_cast<double>(slider_->value()) / 100.0;
}

void PitchSlider::setPercentage(double pct)
{
    int v = static_cast<int>(std::lround(pct * 100.0));
    slider_->setValue(v);
    valueLabel_->setText(QString("%1%2%3")
        .arg(pct >= 0 ? "+" : "")
        .arg(pct, 0, 'f', 2)
        .arg("%"));
}

} // namespace vdj
