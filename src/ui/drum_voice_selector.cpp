#include "drum_voice_selector.h"
#include "sampler_icons.h"

#include <QButtonGroup>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QToolButton>

namespace vdj {

DrumVoiceSelector::DrumVoiceSelector(QWidget* parent)
    : QWidget(parent)
{
    static const char* const kTips[6] = { "Bass drum (BD)", "Snare (SD)", "Synth (SY)", "Clap (CL)", "Closed hi-hat (HH)", "Open hi-hat (OH)" };
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(2);
    group_ = new QButtonGroup(this);
    group_->setExclusive(true);
    for (int v = 0; v < 6; ++v) {
        auto* b = new QToolButton(this);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; margin: 0px; border: none; background: transparent; }"));
        b->setFixedSize(24, 24);
        b->setIconSize(QSize(24, 24));
        b->setFocusPolicy(Qt::NoFocus);
        tips_[v] = tr(kTips[v]) + tr(": edit this voice. Alt+click mutes it.");
        b->setToolTip(tips_[v]);
        b->installEventFilter(this);
        group_->addButton(b, v);
        l->addWidget(b);
        buttons_[v] = b;
    }
    l->addStretch(1);
    buttons_[0]->setChecked(true);
    updateIcons();
    connect(group_, &QButtonGroup::idClicked, this, [this](int v) {
        if (v == selected_)
            return;
        selected_ = v;
        updateIcons();
        emit selectedChanged(v);
    });
}

void DrumVoiceSelector::setSelected(int voice)
{
    if (voice < 0 || voice > 5)
        return;
    selected_ = voice;
    buttons_[voice]->setChecked(true);
    updateIcons();
}

void DrumVoiceSelector::setMuted(int voice, bool muted)
{
    if (voice < 0 || voice > 5)
        return;
    muted_[voice] = muted;
    QToolButton* b = buttons_[voice];
    if (muted) {
        auto* fx = new QGraphicsOpacityEffect(b);
        fx->setOpacity(0.35);
        b->setGraphicsEffect(fx);                    // replaces (and deletes) any earlier effect
        b->setToolTip(tips_[voice] + tr(" - muted"));
    } else {
        b->setGraphicsEffect(nullptr);
        b->setToolTip(tips_[voice]);
    }
}

bool DrumVoiceSelector::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type t = event->type();
    if (t == QEvent::MouseButtonPress || t == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        const bool muteClick = me->button() == Qt::MiddleButton || (me->button() == Qt::LeftButton && (me->modifiers() & Qt::AltModifier));
        if (muteClick) {
            if (t == QEvent::MouseButtonPress) {
                for (int v = 0; v < 6; ++v)
                    if (watched == buttons_[v]) {
                        setMuted(v, !muted_[v]);
                        emit muteToggled(v, muted_[v]);
                    }
            }
            return true;                             // neither selects nor clicks the button
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DrumVoiceSelector::updateIcons()
{
    for (int v = 0; v < 6; ++v)
        buttons_[v]->setIcon(drumVoiceIcon(v, v == selected_));
}

} // namespace vdj
