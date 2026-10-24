#ifndef DEBUG_OVERLAY_H
#define DEBUG_OVERLAY_H

#include <QWidget>
#include <QStringList>

namespace vdj {

class DebugOverlay : public QWidget {
    Q_OBJECT

public:
    explicit DebugOverlay(QWidget* parent = nullptr);

public slots:
    void setLines(const QStringList& lines);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QStringList lines_;
};

} // namespace vdj

#endif // DEBUG_OVERLAY_H