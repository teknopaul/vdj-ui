#ifndef TRAKCRATE_JOBS_STRIP_H
#define TRAKCRATE_JOBS_STRIP_H

#include <QMap>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

namespace vdj {

// One row per running export: where it writes, a progress bar, the file being written and its own Cancel button. Hidden while there are no rows.
class TrakcrateJobsStrip : public QWidget {
    Q_OBJECT

public:
    explicit TrakcrateJobsStrip(QWidget* parent = nullptr);

    void addJob(int id, const QString& title);
    void setProgress(int id, int done, int total, const QString& current);
    void setCancellable(int id, bool cancellable);
    void removeJob(int id);
    int count() const { return rows_.size(); }

signals:
    void cancelRequested(int id);

private:
    struct Row {
        QWidget* widget = nullptr;
        QLabel* title = nullptr;
        QProgressBar* bar = nullptr;
        QLabel* current = nullptr;
        QPushButton* cancel = nullptr;
    };
    QVBoxLayout* layout_;
    QMap<int, Row> rows_;
};

} // namespace vdj

#endif
