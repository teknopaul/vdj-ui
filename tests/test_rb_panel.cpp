#include <QtTest>

#include <QTcpSocket>
#include <QTemporaryDir>

#include <QSignalSpy>
#include "ui/rekordbox_mode_panel.h"

using namespace vdj;

namespace {

template <typename Fn>
bool waitFor(Fn cond, int ms = 3000)
{
    for (int i = 0; i < ms / 10 && !cond(); ++i) QTest::qWait(10);
    return cond();
}

RbModeOptions loopbackOptions(const QTemporaryDir& logs)
{
    RbModeOptions o;
    o.exportRoot = QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7");
    o.logDir = logs.path();
    o.ipOverride = QHostAddress(QStringLiteral("127.0.0.2"));
    o.broadcastOverride = QHostAddress::LocalHost;
    o.bindAddress = QHostAddress::LocalHost;
    o.queryPort = o.dbPort = 0;
    o.bindAnnounce = o.bindBeat = o.bindStatus = 0;
    o.destAnnounce = o.destStatus = 9;
    o.portmapPort = 0; o.mountPort = 0; o.nfsPort = 0;
    o.checkPortFree = false;
    o.claimIntervalMs = 10; o.keepAliveMs = 100;
    return o;
}

} // namespace

class TestRbPanel : public QObject {
    Q_OBJECT

private slots:
    void startsShowsStatusAndStops()
    {
        QTemporaryDir logs;
        RekordboxModePanel panel(nullptr);
        panel.baseOptions() = loopbackOptions(logs);
        RbModeOptions shown;
        shown.exportRoot = QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7");
        shown.device = 18;
        shown.computerName = QStringLiteral("DIRTBOX");
        panel.applyOptions(shown);
        QCOMPARE(panel.options().device, 18);
        QCOMPARE(panel.options().computerName, QStringLiteral("DIRTBOX"));
        QCOMPARE(panel.statusText(), QStringLiteral("stopped"));

        QSignalSpy running(&panel, &RekordboxModePanel::runningChanged);
        QString err;
        QVERIFY2(panel.startMode(&err), qPrintable(err));
        QVERIFY(panel.isRunning());
        QVERIFY(panel.statusText().contains("running"));
        QVERIFY(panel.statusText().contains("7 tracks"));
        QCOMPARE(running.count(), 1);
        QVERIFY(running[0][0].toBool());

        // an unknown request from a client shows up in the unknown list
        QTcpSocket c;
        c.connectToHost(QHostAddress::LocalHost, panel.mode().server().dbPort());
        QVERIFY(c.waitForConnected(2000));
        db::Message m;
        m.txid = 1;
        m.type = 0x7777;
        m.args = {db::Arg::number(1)};
        const auto bytes = db::encode(m);
        c.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size()));
        QVERIFY(waitFor([&] { return panel.unknownCount() == 1; }));

        panel.stopMode();
        QVERIFY(!panel.isRunning());
        QCOMPARE(panel.statusText(), QStringLiteral("stopped"));
        QCOMPARE(running.count(), 2);
        QVERIFY(!running[1][0].toBool());
    }

    void refusesWhileTheLinkIsConnected()
    {
        QTemporaryDir logs;
        RekordboxModePanel panel(nullptr);
        panel.baseOptions() = loopbackOptions(logs);
        RbModeOptions shown;
        shown.exportRoot = QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7");
        panel.applyOptions(shown);
        panel.setLinkConnectedProbe([] { return true; });
        QString err;
        QVERIFY(!panel.startMode(&err));
        QVERIFY(err.contains("Network > Connect"));
        QVERIFY(!panel.isRunning());
    }

    void refusesWithoutAnExport()
    {
        RekordboxModePanel panel(nullptr);
        QString err;
        QVERIFY(!panel.startMode(&err));
        QVERIFY(err.contains("export folder"));
        RbModeOptions bad;
        bad.exportRoot = QStringLiteral("/nonexistent");
        panel.applyOptions(bad);
        QVERIFY(!panel.startMode(&err));
        QVERIFY(!panel.isRunning());
    }
};

QTEST_MAIN(TestRbPanel)
#include "test_rb_panel.moc"
