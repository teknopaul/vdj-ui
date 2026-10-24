#include "keyboard_shortcuts.h"
#include <QShortcut>
#include <QWidget>
#include <QKeySequence>

namespace vdj {

KeyboardShortcuts::KeyboardShortcuts(QWidget* host, QObject* parent)
    : QObject(parent)
{
    auto* pause = new QShortcut(QKeySequence(Qt::Key_Space), host);
    pause->setContext(Qt::ApplicationShortcut);
    shortcuts_.append(pause);
    connect(pause, &QShortcut::activated, this, &KeyboardShortcuts::playPauseRequested);

    auto* sync = new QShortcut(QKeySequence(Qt::Key_S), host);
    sync->setContext(Qt::ApplicationShortcut);
    shortcuts_.append(sync);
    connect(sync, &QShortcut::activated, this, &KeyboardShortcuts::syncRequested);

    auto* master = new QShortcut(QKeySequence(Qt::Key_M), host);
    master->setContext(Qt::ApplicationShortcut);
    shortcuts_.append(master);
    connect(master, &QShortcut::activated, this, &KeyboardShortcuts::becomeMasterRequested);

    auto* copyBpm = new QShortcut(QKeySequence(Qt::Key_B), host);
    copyBpm->setContext(Qt::ApplicationShortcut);
    shortcuts_.append(copyBpm);
    connect(copyBpm, &QShortcut::activated, this, &KeyboardShortcuts::copyBpmRequested);

    auto* cycle = new QShortcut(QKeySequence(Qt::Key_Tab), host);
    cycle->setContext(Qt::ApplicationShortcut);
    shortcuts_.append(cycle);
    connect(cycle, &QShortcut::activated, this, &KeyboardShortcuts::cycleFocusRequested);

    auto* pitchUp = new QShortcut(QKeySequence(Qt::Key_Up), host);
    pitchUp->setContext(Qt::ApplicationShortcut);
    shortcuts_.append(pitchUp);
    connect(pitchUp, &QShortcut::activated, this, [this]() { emit pitchNudgeRequested(0.1); });

    auto* pitchDown = new QShortcut(QKeySequence(Qt::Key_Down), host);
    pitchDown->setContext(Qt::ApplicationShortcut);
    shortcuts_.append(pitchDown);
    connect(pitchDown, &QShortcut::activated, this, [this]() { emit pitchNudgeRequested(-0.1); });

    // difflock (same keys as adj)
    const char* lockKeys[] = { "Shift+U", "Shift+I", "Shift+O", "Shift+P" };
    for (int i = 0; i < 4; ++i) {
        auto* lock = new QShortcut(QKeySequence(QString::fromLatin1(lockKeys[i])), host);
        lock->setContext(Qt::ApplicationShortcut);
        shortcuts_.append(lock);
        connect(lock, &QShortcut::activated, this, [this, i]() { emit diffLockPlayerRequested(i + 1); });
    }
    // window scope so that dialogs keep their own Esc
    auto* off = new QShortcut(QKeySequence(QStringLiteral("Esc, Esc")), host);
    off->setContext(Qt::WindowShortcut);
    shortcuts_.append(off);
    connect(off, &QShortcut::activated, this, &KeyboardShortcuts::diffLockOffRequested);

    auto* followMaster = new QShortcut(QKeySequence(QStringLiteral("Shift+M")), host);
    followMaster->setContext(Qt::ApplicationShortcut);
    shortcuts_.append(followMaster);
    connect(followMaster, &QShortcut::activated, this, &KeyboardShortcuts::diffLockFollowMasterRequested);

    auto* targetUp = new QShortcut(QKeySequence(Qt::Key_Greater), host);
    targetUp->setContext(Qt::ApplicationShortcut);
    shortcuts_.append(targetUp);
    connect(targetUp, &QShortcut::activated, this, [this]() { emit diffLockTargetNudgeRequested(1); });
    auto* targetDown = new QShortcut(QKeySequence(Qt::Key_Less), host);
    targetDown->setContext(Qt::ApplicationShortcut);
    shortcuts_.append(targetDown);
    connect(targetDown, &QShortcut::activated, this, [this]() { emit diffLockTargetNudgeRequested(-1); });
}

} // namespace vdj