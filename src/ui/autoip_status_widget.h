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

private:
    void updateDisplay(const QString& ip, const QString& error);

    AutoIpManager* manager_;
    QLabel* label_;
};

} // namespace vdj

#endif // AUTOIP_STATUS_WIDGET_H
