#include "drum_voice_selector.h"
#include "sampler_icons.h"

#include <QButtonGroup>
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
        b->setToolTip(tr(kTips[v]) + tr(": edit this voice"));
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

void DrumVoiceSelector::updateIcons()
{
    for (int v = 0; v < 6; ++v)
        buttons_[v]->setIcon(drumVoiceIcon(v, v == selected_));
}

} // namespace vdj
