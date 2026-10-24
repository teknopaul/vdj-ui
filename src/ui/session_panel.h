#ifndef SESSION_PANEL_H
#define SESSION_PANEL_H

#include <QWidget>

class QListWidget;
class QLineEdit;

namespace vdj {

class SessionLog;
struct PlayEvent;

class SessionPanel : public QWidget {
    Q_OBJECT
public:
    explicit SessionPanel(SessionLog* log, QWidget* parent = nullptr);

private slots:
    void onNewSet();
    void onExport();
    void onEventAdded(const PlayEvent& ev);

private:
    SessionLog*  log_;
    QListWidget* list_;
    QLineEdit*   djEdit_;
};

} // namespace vdj
#endif
