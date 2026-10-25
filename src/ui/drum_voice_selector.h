#ifndef DRUM_VOICE_SELECTOR_H
#define DRUM_VOICE_SELECTOR_H

#include <QWidget>

class QButtonGroup;
class QToolButton;

namespace vdj {

// The six voice buttons of the DR-6S, BD SD SY CL HH OH, side by side. The one that is lit is the voice being edited; exactly one
// is always lit (it is a selector, not a mute).
class DrumVoiceSelector : public QWidget {
    Q_OBJECT

public:
    explicit DrumVoiceSelector(QWidget* parent = nullptr);

    int selected() const { return selected_; }
    void setSelected(int voice);                     // 0..5, does not emit
    QToolButton* button(int voice) const { return buttons_[voice]; }

signals:
    void selectedChanged(int voice);

private:
    void updateIcons();

    QToolButton* buttons_[6];
    QButtonGroup* group_;
    int selected_ = 0;
};

} // namespace vdj

#endif // DRUM_VOICE_SELECTOR_H
