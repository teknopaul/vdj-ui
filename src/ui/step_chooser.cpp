#include "step_chooser.h"
#include "sampler_icons.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>

namespace vdj {

void StepButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && isEnabled())
        emit stepClicked(event->modifiers());
    event->accept();
}

void StepButton::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        emit stepClicked(event->modifiers());
    else
        QToolButton::keyPressEvent(event);
}

StepChooser::StepChooser(QWidget* parent)
    : QWidget(parent)
{
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(2);
    for (int i = 0; i < kDrumSteps; ++i) {
        auto* b = new StepButton(this);
        b->setAutoRaise(true);
        b->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; margin: 0px; border: none; background: transparent; }"));
        b->setFixedSize(24, 24);
        b->setIconSize(QSize(24, 24));
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tr("Step %1 (beat %2). Click: on / off. Ctrl+click: half volume.").arg(i + 1).arg(i / 4 + 1));
        l->addWidget(b);
        if (i % 4 == 3 && i < kDrumSteps - 1)
            l->addSpacing(6);                        // a gap between the beats
        buttons_[i] = b;
        connect(b, &StepButton::stepClicked, this, [this, i](Qt::KeyboardModifiers mods) {
            const quint8 next = nextState(steps_[i], mods & Qt::ControlModifier);
            if (next == steps_[i])
                return;
            steps_[i] = next;
            updateIcons();
            emit changed(i, steps_[i]);
        });
    }
    l->addStretch(1);
    updateIcons();
}

quint8 StepChooser::nextState(quint8 current, bool ctrl)
{
    if (ctrl)
        return StepHalf;                             // ctrl+click makes a note pink
    return current == StepOff ? quint8(StepOn) : quint8(StepOff);   // a plain click: off -> on, on or pink -> off
}

QVector<quint8> StepChooser::steps() const
{
    return QVector<quint8>(steps_, steps_ + kDrumSteps);
}

void StepChooser::setSteps(const quint8 steps[kDrumSteps])
{
    for (int i = 0; i < kDrumSteps; ++i)
        steps_[i] = std::min<quint8>(steps[i], StepHalf);
    updateIcons();
}

void StepChooser::updateIcons()
{
    for (int i = 0; i < kDrumSteps; ++i) {
        SamplerIcon icon = SamplerIcon::Yellow;
        if (!isEnabled())
            icon = SamplerIcon::Grey;
        else if (steps_[i] == StepOn)
            icon = SamplerIcon::Red;
        else if (steps_[i] == StepHalf)
            icon = SamplerIcon::Pink;
        buttons_[i]->setIcon(samplerIcon(icon));
    }
}

void StepChooser::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::EnabledChange)
        updateIcons();
}

} // namespace vdj
