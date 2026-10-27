#include "trakcrate_job.h"

#include <QMetaObject>
#include <QThread>

namespace vdj {

TrakcrateJobRunner::TrakcrateJobRunner(QObject* parent) : QObject(parent) {}

TrakcrateJobRunner::~TrakcrateJobRunner()
{
    if (thread_) {
        cancel_ = true;
        thread_->disconnect(this);                // no signals into a half destroyed runner
        thread_->wait();
        delete thread_;
    }
}

bool TrakcrateJobRunner::start(const QString& title, Work work)
{
    if (thread_) return false;
    cancel_ = false;
    title_ = title;
    QThread* t = QThread::create([this, work]() {
        Progress progress = [this](int done, int total, const std::string& current) {
            const QString cur = QString::fromStdString(current);
            QMetaObject::invokeMethod(this, [this, done, total, cur]() { emit this->progress(done, total, cur); }, Qt::QueuedConnection);
        };
        work(cancel_, progress);
    });
    thread_ = t;
    connect(t, &QThread::finished, this, [this, t]() {
        const bool wasCancelled = cancel_.load();
        const QString done = title_;
        thread_ = nullptr;
        t->deleteLater();
        emit finished(done, wasCancelled);
    });
    emit started(title);
    t->start();
    return true;
}

void TrakcrateJobRunner::cancel()
{
    cancel_ = true;
}

} // namespace vdj
