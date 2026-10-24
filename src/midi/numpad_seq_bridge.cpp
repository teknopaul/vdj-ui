#include "numpad_seq_bridge.h"
#include "alsa_sequencer.h"

#include <QShortcut>
#include <QKeySequence>
#include <QWidget>

namespace vdj {

NumpadSeqBridge::NumpadSeqBridge(AlsaSequencer* seq, QWidget* host,
                                  QObject* parent)
    : QObject(parent), seq_(seq)
{
    auto add = [&](QKeySequence ks, auto fn) {
        auto* sc = new QShortcut(ks, host);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, fn);
    };

    // Toggle start / stop
    add(Qt::Key_Enter,  [this]() { seq_->toggle(); });

    // Tempo adjust
    add(Qt::Key_Plus,              [this]() { seq_->adjustTempo( 0.1); });
    add(Qt::Key_Minus,             [this]() { seq_->adjustTempo(-0.1); });
    add(Qt::CTRL | Qt::Key_Plus,   [this]() { seq_->adjustTempo( 1.0); });
    add(Qt::CTRL | Qt::Key_Minus,  [this]() { seq_->adjustTempo(-1.0); });

    // Nudge — use Ctrl+numpad arrows to avoid clashing with non-numpad arrows
    add(Qt::CTRL | Qt::Key_Right,  [this]() { seq_->nudge( 10); });
    add(Qt::CTRL | Qt::Key_Left,   [this]() { seq_->nudge(-10); });
    add(Qt::CTRL | Qt::Key_Up,     [this]() { seq_->nudge( 20); });
    add(Qt::CTRL | Qt::Key_Down,   [this]() { seq_->nudge(-20); });
    add(Qt::CTRL | Qt::Key_Home,   [this]() { seq_->quantizedRestart(); });

    // VDJ ops (need main window wiring)
    add(Qt::Key_Asterisk, [this]() { emit copyMasterRequested(); });
    add(Qt::Key_Slash,    [this]() { emit copyOtherRequested(); });
}

} // namespace vdj
