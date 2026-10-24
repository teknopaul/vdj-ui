#pragma once

#include <QObject>

class QWidget;
class QShortcut;

namespace vdj {

class AlsaSequencer;

// Keyboard shortcuts for a dedicated number-pad "mini-keyboard" controller.
// Mirrors the keybindings from adj_numpad.c:
//
//   Numpad Enter   → toggle (start/stop)
//   Numpad +       → tempo +0.1
//   Numpad -       → tempo -0.1
//   Numpad *       → copy_master   (emits copyMasterRequested)
//   Numpad /       → copy_other    (emits copyOtherRequested)
//   Ctrl+Numpad+   → tempo +1.0
//   Ctrl+Numpad-   → tempo -1.0
//   Numpad Right   → nudge +10
//   Numpad Left    → nudge -10
//   Numpad Up      → nudge +20
//   Numpad Down    → nudge -20
//   Numpad Home    → quantized restart
//
// All shortcuts are ApplicationShortcut so they fire regardless of focus.
class NumpadSeqBridge : public QObject {
    Q_OBJECT
public:
    explicit NumpadSeqBridge(AlsaSequencer* seq, QWidget* host,
                              QObject* parent = nullptr);

signals:
    void copyMasterRequested();
    void copyOtherRequested();

private:
    AlsaSequencer* seq_;
};

} // namespace vdj
