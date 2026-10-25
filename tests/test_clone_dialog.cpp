#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "ui/pendrive_clone_dialog.h"

using namespace vdj;

// The clone dialog runs its copy in a QThread; the thread must outlive neither the end of the copy nor the dialog.
class TestCloneDialog : public QObject {
    Q_OBJECT

private:
    static void makeTree(const QString& root, int files, int kb)
    {
        QDir().mkpath(root + "/Contents/a");
        const QByteArray blob(kb * 1024, 'x');
        for (int i = 0; i < files; ++i) {
            QFile f(root + QString("/Contents/a/f%1.mp3").arg(i));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(blob);
        }
    }

private slots:
    void copiesAndEndsCleanly()
    {
        QTemporaryDir tmp;
        makeTree(tmp.filePath("src"), 30, 64);
        auto* dlg = new PendriveCloneDialog;
        dlg->startCopy(tmp.filePath("src"), tmp.filePath("dst"));
        QVERIFY(dlg->running());
        QTRY_VERIFY_WITH_TIMEOUT(!dlg->running(), 10000);
        QCOMPARE(QDir(tmp.filePath("dst/Contents/a")).entryList(QDir::Files).size(), 30);
        QTest::qWait(200);                        // the queued deleteLater of the thread and the worker run now
        delete dlg;
    }

    void destroyedWhileCopying()
    {
        QTemporaryDir tmp;
        makeTree(tmp.filePath("src"), 200, 512);
        {
            PendriveCloneDialog dlg;
            dlg.startCopy(tmp.filePath("src"), tmp.filePath("dst"));
            QTest::qWait(30);
            QVERIFY(dlg.running());
        }                                         // "QThread: Destroyed while thread is still running" aborted here
        QVERIFY(true);
    }

    void failureEndsCleanly()
    {
        QTemporaryDir tmp;
        makeTree(tmp.filePath("src"), 3, 4);
        QFile blocker(tmp.filePath("blocker"));   // a file where the destination folder should go
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        PendriveCloneDialog dlg;
        dlg.startCopy(tmp.filePath("src"), tmp.filePath("blocker/dst"));
        QTRY_VERIFY_WITH_TIMEOUT(!dlg.running(), 10000);
        dlg.startCopy(tmp.filePath("src"), tmp.filePath("ok"));    // a new copy can start after a failed one
        QTRY_VERIFY_WITH_TIMEOUT(!dlg.running(), 10000);
        QVERIFY(QFile::exists(tmp.filePath("ok/Contents/a/f0.mp3")));
    }
};

QTEST_MAIN(TestCloneDialog)
#include "test_clone_dialog.moc"
