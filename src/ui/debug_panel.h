#ifndef DEBUG_PANEL_H
#define DEBUG_PANEL_H

#include <QWidget>
#include <QVector>

class QListWidget;
class QComboBox;
class QLabel;

namespace vdj {

class PacketLogger;

class DebugPanel : public QWidget {
    Q_OBJECT

public:
    DebugPanel(PacketLogger* logger, QWidget* parent = nullptr);

    void refresh();
    void setFilter(const QString& type);

private:
    PacketLogger* logger_;
    QListWidget* list_;
    QComboBox* filter_;
    QLabel* countLabel_;
    QString currentFilter_ = QStringLiteral("ALL");
};

} // namespace vdj

#endif // DEBUG_PANEL_H