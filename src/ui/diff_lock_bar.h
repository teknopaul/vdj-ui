#ifndef DIFF_LOCK_BAR_H
#define DIFF_LOCK_BAR_H

#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace vdj {

// The difflock controls of the VDJ deck panel (see DiffLock): the offset to keep from the followed deck, follow
// master, off, capture the measured offset, and the sequencer nudges. A deck is chosen to follow by clicking that
// deck's Sync button, so there is no deck picker here.
class DiffLockBar : public QWidget {
    Q_OBJECT

public:
    static constexpr int kDefaultTargetMs = 20;

    explicit DiffLockBar(QWidget* parent = nullptr);

    int targetMs() const;
    void setTargetMs(int ms);                    // does not emit targetChanged
    bool followMaster() const;
    void setFollowMaster(bool on);               // does not emit followMasterToggled
    void setStatus(const QString& text);
    // What follows the VDJ's MIDI clock: names of the subscribed drum machines / synths and whether the clock runs.
    void setSequencerFollowers(const QStringList& names, bool running);
    QString followersText() const;
    QString status() const;

    QSpinBox* targetSpin() const { return target_; }
    QCheckBox* followMasterBox() const { return followMaster_; }
    QPushButton* offButton() const { return off_; }
    QPushButton* captureButton() const { return capture_; }
    const QList<QPushButton*>& nudgeButtons() const { return nudges_; }   // -10 -1 +1 +10

signals:
    void targetChanged(int ms);
    void followMasterToggled(bool on);
    void offClicked();
    void captureClicked();
    void nudgeClicked(int ms);                   // + makes the sequencer's next beat longer (later), - shorter

private:
    QLabel* status_;
    QLabel* followers_;
    QSpinBox* target_;
    QCheckBox* followMaster_;
    QPushButton* off_;
    QPushButton* capture_;
    QList<QPushButton*> nudges_;
};

} // namespace vdj

#endif // DIFF_LOCK_BAR_H
