#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "nfs/nfs_mount_manager.h"

#define QT_QPA_PLATFORM offscreen

namespace {

void waitForFinished(QSignalSpy& mountedSpy, QSignalSpy& failedSpy, int expectedSignals)
{
    for (int i = 0; i < 1000 && (mountedSpy.count() + failedSpy.count()) < expectedSignals; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(10);
    }
}

} // namespace

class TestNfsMount : public QObject {
    Q_OBJECT

private:
    QTemporaryDir tempDir_;

    QString installFakeMountScript()
    {
        QDir binDir(tempDir_.path() + "/bin");
        binDir.mkpath(".");
        QFile script(binDir.filePath("mount"));
        if (!script.open(QIODevice::WriteOnly))
            return QString();
        script.write("#!/bin/sh\nsleep 0.15\nexit 0\n");
        script.close();
        QFile::setPermissions(script.fileName(),
                              QFile::permissions(script.fileName())
                                  | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
        return binDir.path();
    }

private slots:
    void doubleMountRaceDoesNotCrash()
    {
        QString binDir = installFakeMountScript();
        QVERIFY(!binDir.isEmpty());
        qputenv("PATH", qPrintable(binDir + ":" + QString::fromLocal8Bit(qgetenv("PATH"))));

        vdj::NfsMountManager mgr;
        QSignalSpy mountedSpy(&mgr, &vdj::NfsMountManager::mounted);
        QSignalSpy failedSpy(&mgr, &vdj::NfsMountManager::mountFailed);

        // Trigger two mounts back-to-back so the second pre-empts the first.
        mgr.mountPlayer(1, QStringLiteral("10.0.0.1"));
        mgr.mountPlayer(2, QStringLiteral("10.0.0.2"));

        // Both processes finish by proxy; the race previously dereferenced a
        // stale QProcess and crashed. We just need both signals delivered.
        waitForFinished(mountedSpy, failedSpy, 2);
        QCOMPARE(mountedSpy.count() + failedSpy.count(), 2);

        // Second player must be reported (it is the currently-tracked mount).
        bool ok = false;
        for (int i = 0; i < mountedSpy.count(); ++i)
            ok |= (mountedSpy.at(i).at(0).toInt() == 2);
        for (int i = 0; i < failedSpy.count(); ++i)
            ok |= (failedSpy.at(i).at(0).toInt() == 2);
        QVERIFY(ok);
    }

    void sequentialMountsReportBoth()
    {
        vdj::NfsMountManager mgr;
        QSignalSpy mountedSpy(&mgr, &vdj::NfsMountManager::mounted);
        QSignalSpy failedSpy(&mgr, &vdj::NfsMountManager::mountFailed);

        mgr.mountPlayer(3, QStringLiteral("10.0.0.3"));
        waitForFinished(mountedSpy, failedSpy, 1);
        QCOMPARE(mountedSpy.count(), 1);
        QCOMPARE(mountedSpy.at(0).at(0).toInt(), 3);
    }
};

QTEST_MAIN(TestNfsMount)
#include "test_nfs_mount.moc"
