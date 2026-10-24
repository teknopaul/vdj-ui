#ifndef TOOLBAR_H
#define TOOLBAR_H

#include <QToolBar>

class QAction;

namespace vdj {

class Toolbar : public QToolBar {
    Q_OBJECT

public:
    explicit Toolbar(QWidget* parent = nullptr);

signals:
    void playPauseTriggered();
    void syncTriggered();
    void bpmUpTriggered();
    void bpmDownTriggered();
    void connectTriggered();
    void disconnectTriggered();

public:
    // Enabled/disabled state for the connect/disconnect pair.
    void setConnectedState(bool connected);

private:
    QAction* playPauseAction_;
    QAction* syncAction_;
    QAction* bpmUpAction_;
    QAction* bpmDownAction_;
    QAction* connectAction_;
    QAction* disconnectAction_;
};

} // namespace vdj

#endif // TOOLBAR_H
