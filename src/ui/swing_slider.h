#ifndef SWING_SLIDER_H
#define SWING_SLIDER_H

#include <QWidget>

class QLabel;
class QSlider;

namespace vdj {

// The swing amount 0..127 of the DR-6S and the Dembomatik (see audio/swing.h): a slider that is dragged (the mouse wheel is
// kept for the volume) and a "swing 64" label.
class SwingSlider : public QWidget {
    Q_OBJECT

public:
    explicit SwingSlider(QWidget* parent = nullptr);

    int value() const;
    void setValue(int swing);                        // does not emit
    QSlider* slider() const { return slider_; }

    // Is this object, or one of its parents, a SwingSlider? (the module widget leaves the wheel alone over it)
    static bool isInside(const QObject* object);

signals:
    void valueChanged(int swing);

private:
    void updateLabel();

    QSlider* slider_;
    QLabel* label_;
};

} // namespace vdj

#endif // SWING_SLIDER_H
