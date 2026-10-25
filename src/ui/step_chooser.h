#ifndef STEP_CHOOSER_H
#define STEP_CHOOSER_H

#include <QToolButton>
#include <QVector>
#include <QWidget>
#include "audio/drum_pattern.h"

namespace vdj {

// One step button: reports the keyboard modifiers of the click (a button with three states cannot use toggled()).
class StepButton : public QToolButton {
    Q_OBJECT

public:
    explicit StepButton(QWidget* parent = nullptr) : QToolButton(parent) {}

signals:
    void stepClicked(Qt::KeyboardModifiers modifiers);

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
};

// The 16 steps of a bar on one line: yellow = off, red = on, pink = half volume. A gap after every 4th step shows the beats.
//   click:        off -> on, on -> off, half -> off
//   ctrl + click: any -> half (a half volume step stays half)
class StepChooser : public QWidget {
    Q_OBJECT

public:
    explicit StepChooser(QWidget* parent = nullptr);

    quint8 step(int index) const { return steps_[index]; }
    QVector<quint8> steps() const;
    void setSteps(const quint8 steps[kDrumSteps]);   // does not emit
    QToolButton* button(int index) const { return buttons_[index]; }

    static quint8 nextState(quint8 current, bool ctrl);

signals:
    void changed(int step, quint8 state);

protected:
    void changeEvent(QEvent* event) override;

private:
    void updateIcons();

    StepButton* buttons_[kDrumSteps];
    quint8 steps_[kDrumSteps] = {};
};

} // namespace vdj

#endif // STEP_CHOOSER_H
