#ifndef PITCH_SLIDER_H
#define PITCH_SLIDER_H

#include <QWidget>

class QSlider;
class QLabel;

namespace vdj {

class PitchSlider : public QWidget {
    Q_OBJECT

public:
    explicit PitchSlider(QWidget* parent = nullptr);

    double percentage() const;
    void setPercentage(double pct);

signals:
    void pitchChanged(double percentage);

private:
    QSlider* slider_;
    QLabel* valueLabel_;
};

} // namespace vdj

#endif // PITCH_SLIDER_H
