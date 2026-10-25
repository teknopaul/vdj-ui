#ifndef PENDRIVE_CLONE_DIALOG_H
#define PENDRIVE_CLONE_DIALOG_H

#include <QDialog>
#include <QAtomicInt>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QThread>

class QLineEdit;
class QLabel;
class QProgressBar;
class QPushButton;

namespace vdj {

// Runs in a worker thread; copies a directory tree recursively.
class CloneWorker : public QObject {
    Q_OBJECT
public:
    CloneWorker(const QString& src, const QString& dst);

public slots:
    void run();

signals:
    void scanDone(int totalFiles, qint64 totalBytes);
    void fileDone(int filesDone, qint64 bytesDone, const QString& currentFile);
    void finished(bool success, const QString& error);

public:
    void cancel() { cancelled_.storeRelease(1); }

private:
    int   scanDir(const QString& dir, qint64& bytes);
    bool  copyDir(const QString& src, const QString& dst,
                  int totalFiles, qint64 totalBytes,
                  int& filesDone, qint64& bytesDone);

    QString     src_;
    QString     dst_;
    QAtomicInt  cancelled_;
    QString     error_;      // why a copy stopped: the file and the system's reason
};


class PendriveCloneDialog : public QDialog {
    Q_OBJECT

public:
    explicit PendriveCloneDialog(QWidget* parent = nullptr);
    ~PendriveCloneDialog() override;     // stops a copy that is still running before the thread goes away

    // Pre-populate source field (e.g., from NFS mount or pendrive path)
    void setSourcePath(const QString& path);

    // Start copying without asking for confirmation (onStart asks, then calls this).
    void startCopy(const QString& src, const QString& dst);
    bool running() const { return thread_ != nullptr; }

    void reject() override;              // closing while copying cancels the copy and waits for it, the dialog stays until it has stopped

private slots:
    void onBrowseSrc();
    void onBrowseDst();
    void onSwap();
    void onStart();
    void onCancel();
    void onScanDone(int totalFiles, qint64 totalBytes);
    void onFileDone(int filesDone, qint64 bytesDone, const QString& current);
    void onWorkerFinished(bool success, const QString& error);

private:
    void setRunning(bool running);
    static QString humanBytes(qint64 bytes);

    QLineEdit*   srcEdit_;
    QLineEdit*   dstEdit_;
    QProgressBar* progressBar_;
    QLabel*      statusLabel_;
    QLabel*      countLabel_;
    QPushButton* startBtn_;
    QPushButton* cancelBtn_;

    QPointer<CloneWorker> worker_;
    QPointer<QThread>     thread_;       // set until the thread has really finished

    int    totalFiles_ = 0;
    qint64 totalBytes_ = 0;
    QElapsedTimer elapsed_;
};

} // namespace vdj

#endif // PENDRIVE_CLONE_DIALOG_H
