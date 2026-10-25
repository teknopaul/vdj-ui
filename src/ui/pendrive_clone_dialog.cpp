#include "pendrive_clone_dialog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#include <fcntl.h>
#include <sys/stat.h>

namespace vdj {

/* ---- CloneWorker ---- */

static void preserveMtime(const QString& srcPath, const QString& dstPath)
{
    QFileInfo fi(srcPath);
    qint64 secs = fi.lastModified().toSecsSinceEpoch();
    struct timespec ts[2];
    ts[0].tv_sec  = secs; ts[0].tv_nsec = 0;
    ts[1].tv_sec  = secs; ts[1].tv_nsec = 0;
    utimensat(AT_FDCWD, dstPath.toLocal8Bit().constData(), ts, 0);
}

CloneWorker::CloneWorker(const QString& src, const QString& dst)
    : src_(src), dst_(dst)
{}

int CloneWorker::scanDir(const QString& dir, qint64& bytes)
{
    int count = 0;
    QDir d(dir);
    for (const QFileInfo& fi : d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (cancelled_.loadAcquire()) break;
        if (fi.isDir())
            count += scanDir(fi.absoluteFilePath(), bytes);
        else {
            bytes += fi.size();
            ++count;
        }
    }
    return count;
}

bool CloneWorker::copyDir(const QString& src, const QString& dst,
                          int totalFiles, qint64 totalBytes,
                          int& filesDone, qint64& bytesDone)
{
    if (!QDir().mkpath(dst)) {
        error_ = QStringLiteral("Cannot create %1").arg(dst);
        return false;
    }
    QDir d(src);
    for (const QFileInfo& fi : d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (cancelled_.loadAcquire()) return false;

        QString srcPath = fi.absoluteFilePath();
        QString dstPath = dst + QDir::separator() + fi.fileName();

        if (fi.isDir()) {
            if (!copyDir(srcPath, dstPath, totalFiles, totalBytes, filesDone, bytesDone))
                return false;
        } else {
            if (QFile::exists(dstPath))
                QFile::remove(dstPath);
            QFile in(srcPath);
            if (!in.copy(dstPath)) {
                error_ = QStringLiteral("Cannot copy %1: %2").arg(srcPath, in.errorString());
                return false;
            }
            preserveMtime(srcPath, dstPath);

            bytesDone += fi.size();
            ++filesDone;
            emit fileDone(filesDone, bytesDone, fi.fileName());
        }
    }
    // Preserve directory mtime after all children written
    preserveMtime(src, dst);
    return true;
}

void CloneWorker::run()
{
    qint64 totalBytes = 0;
    int    totalFiles = scanDir(src_, totalBytes);

    if (cancelled_.loadAcquire()) {
        emit finished(false, QStringLiteral("Cancelled."));
        return;
    }

    emit scanDone(totalFiles, totalBytes);

    int    filesDone = 0;
    qint64 bytesDone = 0;

    bool ok = copyDir(src_, dst_, totalFiles, totalBytes, filesDone, bytesDone);

    if (!ok && !cancelled_.loadAcquire())
        emit finished(false, error_.isEmpty() ? QStringLiteral("Copy failed — check source/destination paths.") : error_);
    else if (cancelled_.loadAcquire())
        emit finished(false, QStringLiteral("Cancelled."));
    else
        emit finished(true, QString());
}

/* ---- PendriveCloneDialog ---- */

PendriveCloneDialog::PendriveCloneDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Clone Pendrive / NFS Mount"));
    setMinimumWidth(560);

    auto* form = new QFormLayout;

    auto* srcRow = new QHBoxLayout;
    srcEdit_ = new QLineEdit(this);
    auto* browSrc = new QPushButton(QStringLiteral("Browse…"), this);
    srcRow->addWidget(srcEdit_);
    srcRow->addWidget(browSrc);
    form->addRow(QStringLiteral("Source:"), srcRow);

    auto* dstRow = new QHBoxLayout;
    dstEdit_ = new QLineEdit(this);
    auto* browDst = new QPushButton(QStringLiteral("Browse…"), this);
    dstRow->addWidget(dstEdit_);
    dstRow->addWidget(browDst);
    form->addRow(QStringLiteral("Destination:"), dstRow);

    auto* swapBtn = new QPushButton(QStringLiteral("⇅ Swap (Restore mode)"), this);

    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setVisible(false);

    countLabel_  = new QLabel(this);
    statusLabel_ = new QLabel(
        QStringLiteral("Tip: swap source/destination to restore a clone back to a pendrive."),
        this);
    statusLabel_->setWordWrap(true);

    startBtn_  = new QPushButton(QStringLiteral("Start Copy"), this);
    cancelBtn_ = new QPushButton(QStringLiteral("Close"), this);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(startBtn_);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(swapBtn);
    layout->addWidget(progressBar_);
    layout->addWidget(countLabel_);
    layout->addWidget(statusLabel_);
    layout->addLayout(btnRow);

    connect(browSrc,   &QPushButton::clicked, this, &PendriveCloneDialog::onBrowseSrc);
    connect(browDst,   &QPushButton::clicked, this, &PendriveCloneDialog::onBrowseDst);
    connect(swapBtn,   &QPushButton::clicked, this, &PendriveCloneDialog::onSwap);
    connect(startBtn_, &QPushButton::clicked, this, &PendriveCloneDialog::onStart);
    connect(cancelBtn_,&QPushButton::clicked, this, &PendriveCloneDialog::onCancel);
}

void PendriveCloneDialog::setSourcePath(const QString& path)
{
    srcEdit_->setText(path);
}

void PendriveCloneDialog::onBrowseSrc()
{
    QString p = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Source"),
                                                   srcEdit_->text());
    if (!p.isEmpty()) srcEdit_->setText(p);
}

void PendriveCloneDialog::onBrowseDst()
{
    QString p = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Destination"),
                                                   dstEdit_->text());
    if (!p.isEmpty()) dstEdit_->setText(p);
}

void PendriveCloneDialog::onSwap()
{
    QString tmp = srcEdit_->text();
    srcEdit_->setText(dstEdit_->text());
    dstEdit_->setText(tmp);
}

void PendriveCloneDialog::onStart()
{
    QString src = srcEdit_->text().trimmed();
    QString dst = dstEdit_->text().trimmed();

    if (src.isEmpty() || dst.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Missing Path"),
                             QStringLiteral("Both source and destination paths are required."));
        return;
    }
    if (!QDir(src).exists()) {
        QMessageBox::warning(this, QStringLiteral("Invalid Source"),
                             QStringLiteral("Source directory does not exist."));
        return;
    }

    int res = QMessageBox::question(
        this, QStringLiteral("Confirm Copy"),
        QStringLiteral("Copy from:\n  %1\nTo:\n  %2\n\n"
                       "Ensure no deck is actively reading from or writing to the source "
                       "during this operation.\n\nProceed?").arg(src, dst),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (res != QMessageBox::Yes) return;

    startCopy(src, dst);
}

void PendriveCloneDialog::startCopy(const QString& src, const QString& dst)
{
    if (thread_) return;                        // the previous copy is still shutting down

    auto* worker = new CloneWorker(src, dst);
    auto* thread = new QThread(this);
    worker_ = worker;
    thread_ = thread;
    worker->moveToThread(thread);

    connect(thread, &QThread::started,        worker, &CloneWorker::run);
    connect(worker, &CloneWorker::scanDone,   this,   &PendriveCloneDialog::onScanDone);
    connect(worker, &CloneWorker::fileDone,   this,   &PendriveCloneDialog::onFileDone);
    connect(worker, &CloneWorker::finished,   this,   &PendriveCloneDialog::onWorkerFinished);
    connect(worker, &CloneWorker::finished,   thread, &QThread::quit);
    // The thread object may only go once it has stopped: delete it from its own finished signal, never earlier
    connect(thread, &QThread::finished,       worker, &QObject::deleteLater);
    connect(thread, &QThread::finished,       thread, &QObject::deleteLater);
    connect(thread, &QThread::finished,       this,   [this]() { setRunning(false); });

    elapsed_.start();
    setRunning(true);
    thread->start();
}

PendriveCloneDialog::~PendriveCloneDialog()
{
    if (thread_) {
        if (worker_) worker_->cancel();
        thread_->quit();
        thread_->wait();                        // returns once the file being copied is done
    }
}

void PendriveCloneDialog::reject()
{
    if (thread_) {                              // copying: cancel, and close once it has stopped (the Cancel button does the same)
        onCancel();
        return;
    }
    QDialog::reject();
}

void PendriveCloneDialog::onCancel()
{
    if (thread_) {
        if (worker_) worker_->cancel();
        statusLabel_->setText(QStringLiteral("Cancelling…"));
    } else {
        accept();
    }
}

void PendriveCloneDialog::onScanDone(int totalFiles, qint64 totalBytes)
{
    totalFiles_ = totalFiles;
    totalBytes_ = totalBytes;
    countLabel_->setText(QStringLiteral("Files: 0 / %1   Bytes: 0 / %2")
                         .arg(totalFiles_)
                         .arg(humanBytes(totalBytes_)));
    progressBar_->setVisible(true);
}

void PendriveCloneDialog::onFileDone(int filesDone, qint64 bytesDone,
                                     const QString& current)
{
    if (totalFiles_ > 0)
        progressBar_->setValue(100 * filesDone / totalFiles_);

    qint64 elapsed = elapsed_.elapsed();  // ms
    QString eta;
    if (bytesDone > 0 && totalBytes_ > bytesDone && elapsed > 1000) {
        qint64 remaining = (qint64)((double)(totalBytes_ - bytesDone) /
                                    bytesDone * elapsed);
        int secs = (int)(remaining / 1000);
        eta = QStringLiteral("  ETA: %1s").arg(secs);
    }

    countLabel_->setText(
        QStringLiteral("Files: %1 / %2   Bytes: %3 / %4%5")
        .arg(filesDone).arg(totalFiles_)
        .arg(humanBytes(bytesDone)).arg(humanBytes(totalBytes_))
        .arg(eta));
    statusLabel_->setText(current);
}

void PendriveCloneDialog::onWorkerFinished(bool success, const QString& error)
{
    if (success) {
        statusLabel_->setText(QStringLiteral("Copy complete."));
        progressBar_->setValue(100);
    } else {
        statusLabel_->setText(error.isEmpty() ? QStringLiteral("Failed.") : error);
    }
}

void PendriveCloneDialog::setRunning(bool running)
{
    if (!running) { worker_ = nullptr; thread_ = nullptr; }
    startBtn_->setEnabled(!running);
    srcEdit_->setEnabled(!running);
    dstEdit_->setEnabled(!running);
    cancelBtn_->setText(running ? QStringLiteral("Cancel") : QStringLiteral("Close"));
}

QString PendriveCloneDialog::humanBytes(qint64 bytes)
{
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024);
    if (bytes < 1024LL * 1024 * 1024) return QStringLiteral("%1 MB").arg(bytes / (1024*1024));
    return QStringLiteral("%1 GB").arg(bytes / (1024LL*1024*1024));
}

} // namespace vdj
