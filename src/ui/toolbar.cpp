#include "toolbar.h"
#include <QAction>
#include <QIcon>
#include <QStyle>

namespace vdj {

Toolbar::Toolbar(QWidget* parent)
    : QToolBar(parent)
{
    setMovable(false);
    setToolButtonStyle(Qt::ToolButtonTextOnly);

    playPauseAction_ = addAction("Play/Pause", this, &Toolbar::playPauseTriggered);
    syncAction_ = addAction("Sync", this, &Toolbar::syncTriggered);
    bpmUpAction_ = addAction("BPM +", this, &Toolbar::bpmUpTriggered);
    bpmDownAction_ = addAction("BPM -", this, &Toolbar::bpmDownTriggered);
    addSeparator();
    connectAction_ = addAction("Connect", this, &Toolbar::connectTriggered);
    disconnectAction_ = addAction("Disconnect", this, &Toolbar::disconnectTriggered);
    disconnectAction_->setEnabled(false);
    addSeparator();
}

void Toolbar::setConnectedState(bool connected)
{
    connectAction_->setEnabled(!connected);
    disconnectAction_->setEnabled(connected);
}

} // namespace vdj
