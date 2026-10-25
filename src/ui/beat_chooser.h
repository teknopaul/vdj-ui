#ifndef BEAT_CHOOSER_H
#define BEAT_CHOOSER_H

#include <QWidget>

class QToolButton;

namespace vdj {

// Four round buttons, beat 1..4 of the bar: red = the sampler plays on this beat, yellow = it does not, grey = the whole
// chooser is disabled. mask() has bit 0..3 for beat 1..4.
class BeatChooser : public QWidget {
    Q_OBJECT

public:
    explicit BeatChooser(QWidget* parent = nullptr);

    quint8 mask() const;
    void setMask(quint8 mask);                       // does not emit changed()
    QToolButton* button(int beat) const { return buttons_[beat & 3]; }   // beat 0..3

signals:
    void changed(quint8 mask);

protected:
    void changeEvent(QEvent* event) override;

private:
    void updateIcons();

    QToolButton* buttons_[4];
};

} // namespace vdj

#endif // BEAT_CHOOSER_H
