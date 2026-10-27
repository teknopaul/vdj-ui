#ifndef TRAKCRATE_JOB_H
#define TRAKCRATE_JOB_H

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <string>

class QThread;

namespace vdj {

// Runs one piece of trakcrate work (import, analysis, export) on a worker thread, one at a time. The work function gets a cancel flag
// and a progress callback and must not touch widgets; results go into state the caller captured. Signals arrive on the thread that owns
// the runner (the GUI thread).
class TrakcrateJobRunner : public QObject {
    Q_OBJECT

public:
    using Progress = std::function<void(int done, int total, const std::string& current)>;
    using Work = std::function<void(const std::atomic<bool>& cancel, const Progress& progress)>;

    explicit TrakcrateJobRunner(QObject* parent = nullptr);
    ~TrakcrateJobRunner() override;               // cancels a running job and waits for it

    bool start(const QString& title, Work work);  // false when a job is already running
    bool busy() const { return thread_ != nullptr; }
    QString title() const { return title_; }
    void cancel();

signals:
    void started(const QString& title);
    void progress(int done, int total, const QString& current);
    void finished(const QString& title, bool cancelled);

private:
    QThread* thread_ = nullptr;
    std::atomic<bool> cancel_{false};
    QString title_;
};

} // namespace vdj

#endif
