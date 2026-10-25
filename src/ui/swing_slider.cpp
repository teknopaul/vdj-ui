#include "swing_slider.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>

namespace vdj {

SwingSlider::SwingSlider(QWidget* parent)
    : QWidget(parent)
{
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, 127);
    slider_->setFixedWidth(100);
    slider_->setFocusPolicy(Qt::NoFocus);
    label_ = new QLabel(this);
    label_->setMinimumWidth(60);
    setToolTip(tr("Swing: delays every second sixteenth note, 127 = a quarter of a beat"));
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    l->addWidget(label_);
    l->addWidget(slider_);
    connect(slider_, &QSlider::valueChanged, this, [this](int v) {
        updateLabel();
        emit valueChanged(v);
    });
    updateLabel();
}

int SwingSlider::value() const
{
    return slider_->value();
}

void SwingSlider::setValue(int swing)
{
    QSignalBlocker block(slider_);
    slider_->setValue(swing);
    updateLabel();
}

void SwingSlider::updateLabel()
{
    label_->setText(tr("swing %1").arg(slider_->value()));
}

bool SwingSlider::isInside(const QObject* object)
{
    for (; object; object = object->parent())
        if (qobject_cast<const SwingSlider*>(object))
            return true;
    return false;
}

} // namespace vdj
