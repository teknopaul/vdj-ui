#ifndef BPM_RAMP_WIDGET_H
#define BPM_RAMP_WIDGET_H

#include <QWidget>

class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QLabel;

namespace vdj {

// Widget for gradually ramping the local VDJ BPM to a target value over a
// specified number of bars. Only enabled when the VDJ is ProLink master.
class BpmRampWidget : public QWidget {
    Q_OBJECT

public:
    explicit BpmRampWidget(QWidget* parent = nullptr);

    void setCurrentBpm(double bpm);

signals:
    void rampRequested(double targetBpm, int bars);
    void bpmNudged(double delta);

private:
    QDoubleSpinBox* targetSpin_;
    QSpinBox*       barsSpin_;
    QPushButton*    applyBtn_;
    QPushButton*    plusOne_;
    QPushButton*    minusOne_;
    QLabel*         statusLabel_;
};

} // namespace vdj

#endif // BPM_RAMP_WIDGET_H
