#ifndef KEYBOARD_SHORTCUTS_H
#define KEYBOARD_SHORTCUTS_H

#include <QObject>
#include <QVector>

class QShortcut;
class QWidget;

namespace vdj {

class KeyboardShortcuts : public QObject {
    Q_OBJECT

public:
    explicit KeyboardShortcuts(QWidget* host, QObject* parent = nullptr);

signals:
    void playPauseRequested();
    void syncRequested();
    void becomeMasterRequested();
    void copyBpmRequested();
    void cycleFocusRequested();
    void pitchNudgeRequested(double delta);
    // difflock, with adj's keys
    void diffLockPlayerRequested(int playerId);   // Shift+U I O P: decks 1..4
    void diffLockOffRequested();                  // Esc Esc
    void diffLockFollowMasterRequested();         // Shift+M
    void diffLockTargetNudgeRequested(int deltaMs); // < and >

private:
    QVector<QShortcut*> shortcuts_;
};

} // namespace vdj

#endif // KEYBOARD_SHORTCUTS_H