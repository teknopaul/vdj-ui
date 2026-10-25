#include "beat_chooser.h"
#include "sampler_icons.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QToolButton>

namespace vdj {

BeatChooser::BeatChooser(QWidget* parent)
    : QWidget(parent)
{
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(2);
    for (int i = 0; i < 4; ++i) {
        auto* b = new QToolButton(this);
        b->setCheckable(true);
        b->setAutoRaise(true);
        // the icon shows on/off, so no frame or background; no padding either: the buttons are the icon's size and 2 px apart
        b->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; margin: 0px; border: none; background: transparent; }"));
        b->setFixedSize(24, 24);
        b->setFocusPolicy(Qt::NoFocus);
        b->setIconSize(QSize(24, 24));
        b->setToolTip(tr("Beat %1 of the bar").arg(i + 1));
        l->addWidget(b);
        buttons_[i] = b;
        connect(b, &QToolButton::toggled, this, [this]() {
            updateIcons();
            emit changed(mask());
        });
    }
    updateIcons();
}

quint8 BeatChooser::mask() const
{
    quint8 m = 0;
    for (int i = 0; i < 4; ++i)
        if (buttons_[i]->isChecked())
            m |= quint8(1 << i);
    return m;
}

void BeatChooser::setMask(quint8 mask)
{
    for (int i = 0; i < 4; ++i) {
        QSignalBlocker block(buttons_[i]);
        buttons_[i]->setChecked(mask & (1 << i));
    }
    updateIcons();
}

void BeatChooser::updateIcons()
{
    for (QToolButton* b : buttons_)
        b->setIcon(samplerIcon(!isEnabled() ? SamplerIcon::Grey : b->isChecked() ? SamplerIcon::Red : SamplerIcon::Yellow));
}

void BeatChooser::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::EnabledChange)
        updateIcons();
}

} // namespace vdj
