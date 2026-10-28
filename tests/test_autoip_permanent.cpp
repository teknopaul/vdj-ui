#include <QtTest>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "network/auto_ip_manager.h"

using namespace vdj;

// nmcli is replaced by a script that records its arguments, so nothing on the machine is changed.
class TestAutoIpPermanent : public QObject {
    Q_OBJECT

    QString fake(const QTemporaryDir& d, const QString& activeProfile, int failOn = 0)
    {
        QFile f(d.filePath("nmcli"));
        f.open(QIODevice::WriteOnly);
        f.write(QStringLiteral(
            "#!/bin/sh\n"
            "echo \"$@\" >> %1/calls.txt\n"
            "if [ \"$1\" = \"-g\" ]; then echo '%2'; exit 0; fi\n"
            "if [ \"$1 $2\" = \"con mod\" ] && [ %3 = 3 ]; then echo 'denied' >&2; exit 1; fi\n"
            "exit 0\n").arg(d.path(), activeProfile).arg(failOn).toUtf8());
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return d.filePath("calls.txt");
    }
    static QStringList calls(const QString& file)
    {
        QFile f(file);
        f.open(QIODevice::ReadOnly);
        return QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
    }

private slots:
    void existingProfileIsMadeLinkLocalAndPinned()
    {
        QTemporaryDir d;
        const QString log = fake(d, "Wired connection 1");
        qputenv("PATH", d.path().toUtf8() + ":" + qgetenv("PATH"));
        AutoIpManager m;
        QSignalSpy done(&m, &AutoIpManager::permanentDone);
        m.makePermanent("enx00e04c68019a");
        QVERIFY(done.wait(10000));
        QCOMPARE(done.at(0).at(0).toBool(), true);
        QCOMPARE(done.at(0).at(1).toString(), QString("Wired connection 1"));
        const QStringList c = calls(log);
        QVERIFY(c.contains("con mod Wired connection 1 ipv4.method link-local ipv6.method disabled connection.autoconnect yes connection.interface-name enx00e04c68019a"));
        QVERIFY(c.contains("con up Wired connection 1"));                        // one argument, even with spaces in the profile name
        QVERIFY(!m.permanentBusy());
    }
    void anInterfaceWithoutAProfileGetsOne()
    {
        QTemporaryDir d;
        const QString log = fake(d, "--");
        qputenv("PATH", d.path().toUtf8() + ":" + qgetenv("PATH"));
        AutoIpManager m;
        QSignalSpy done(&m, &AutoIpManager::permanentDone);
        m.makePermanent("enx1");
        QVERIFY(done.wait(10000));
        QCOMPARE(done.at(0).at(0).toBool(), true);
        QCOMPARE(done.at(0).at(1).toString(), QString("vdj-prolink-enx1"));
        QVERIFY(calls(log).filter("con add type ethernet ifname enx1 con-name vdj-prolink-enx1 ipv4.method link-local").size() == 1);
    }
    void aFailureIsReported()
    {
        QTemporaryDir d;
        fake(d, "Wired connection 1", 3);
        qputenv("PATH", d.path().toUtf8() + ":" + qgetenv("PATH"));
        AutoIpManager m;
        QSignalSpy done(&m, &AutoIpManager::permanentDone);
        m.makePermanent("enx1");
        QVERIFY(done.wait(10000));
        QCOMPARE(done.at(0).at(0).toBool(), false);
        QVERIFY(done.at(0).at(1).toString().contains("denied"));
    }
    void theCommandsAreShownToTheUser()
    {
        const QString t = AutoIpManager::permanentCommands("enx1");
        QVERIFY(t.contains("ipv4.method link-local"));
        QVERIFY(t.contains("enx1"));
    }
};

QTEST_MAIN(TestAutoIpPermanent)
#include "test_autoip_permanent.moc"
