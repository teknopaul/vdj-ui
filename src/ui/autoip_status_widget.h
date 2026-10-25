#ifndef AUTOIP_STATUS_WIDGET_H
#define AUTOIP_STATUS_WIDGET_H

#include <QWidget>

class QLabel;

namespace vdj {

class AutoIpManager;

// Status bar widget showing the current auto-IP state.
class AutoipStatusWidget : public QWidget {
    Q_OBJECT

public:
    explicit AutoipStatusWidget(AutoIpManager* manager, QWidget* parent = nullptr);

    // The interface cabled to the decks: while our own AutoIP is off, a 169.254.x.x address on it (e.g. from NetworkManager) is shown as working.
    void setInterface(const QString& iface);

private:
    void refreshExternal();
    void updateDisplay(const QString& ip, const QString& error);

    AutoIpManager* manager_;
    QLabel* label_;
    QString iface_;
};

} // namespace vdj

#endif // AUTOIP_STATUS_WIDGET_H
