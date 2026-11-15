#ifndef MIX_FILTERS_DIALOG_H
#define MIX_FILTERS_DIALOG_H

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QVBoxLayout;
class QWidget;

namespace vdj {

class StudioMixWindow;

// The global EQ (two crossovers and the slope) and the master filter chain of a studio mix: the built-in compressor and any number of LADSPA plugins,
// in order, each with an on / off switch and its controls. Every change is one undoable edit of the window.
class MixFiltersDialog : public QDialog {
    Q_OBJECT

public:
    explicit MixFiltersDialog(StudioMixWindow* window);

    QDoubleSpinBox* bassSpin() const { return bass_; }
    QDoubleSpinBox* highSpin() const { return high_; }
    QComboBox* slopeCombo() const { return slope_; }
    QListWidget* filterList() const { return list_; }

    void addLadspa(const QString& key);       // also what the "Add LADSPA..." chooser calls
    void removeCurrent();
    void moveCurrent(int delta);
    void refresh();                           // after the mix changed (also undo)

private:
    void showControls();

    StudioMixWindow* w_;
    QDoubleSpinBox *bass_, *high_;
    QComboBox* slope_;
    QListWidget* list_;
    QWidget* controls_;
    QVBoxLayout* controlsLayout_;
    bool filling_ = false;
};

} // namespace vdj

#endif
