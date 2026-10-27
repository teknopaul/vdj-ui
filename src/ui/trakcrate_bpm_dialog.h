#ifndef TRAKCRATE_BPM_DIALOG_H
#define TRAKCRATE_BPM_DIALOG_H

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;

namespace vdj {

// Shows a detected BPM and lets the user keep it, halve it, double it, type their own, or reject it (Cancel).
class TrakcrateBpmDialog : public QDialog {
    Q_OBJECT

public:
    // tagBpm 0 = the file has no BPM tag. hasGrid: a beat grid already exists, so the user must opt in to replacing it.
    TrakcrateBpmDialog(const QString& title, double detected, double confidence, double tagBpm, bool hasGrid, QWidget* parent = nullptr);

    double bpm() const;
    bool replaceGrid() const;
    void halve();
    void doubleIt();
    void setBpm(double bpm);

private:
    QDoubleSpinBox* spin_;
    QCheckBox* replace_ = nullptr;
};

} // namespace vdj

#endif
