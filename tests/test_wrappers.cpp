#include <QtTest/QtTest>
#include "cdj/cdj_wrapper.h"
#include "vdj/vdj_wrapper.h"
#include "vdj/backline_model.h"
#include "network/auto_ip_manager.h"
#include "ui/toolbar.h"

class TestWrappers : public QObject
{
    Q_OBJECT

private slots:
    void testBpmToString();
    void testPitchToPercentage();
    void testBeatMillis();
    void testBacklineModelEmpty();
    void testAutoIpEmptyIface();
    void testAutoIpStopWithoutStart();
    void testToolbarConnectedState();
};

void TestWrappers::testBpmToString()
{
    std::string s = vdj::CdjWrapper::bpmToString(128.5f);
    QVERIFY(!s.empty());
}

void TestWrappers::testPitchToPercentage()
{
    double pct = vdj::CdjWrapper::pitchToPercentage(CDJ_PITCH_NORMAL);
    QCOMPARE(pct, 0.0);
}

void TestWrappers::testBeatMillis()
{
    uint32_t ms = vdj::CdjWrapper::beatMillis(120.0f);
    QVERIFY(ms > 0);
    QVERIFY(ms < 1000);
}

void TestWrappers::testBacklineModelEmpty()
{
    vdj::BacklineModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.deckCount(), 0);
}

void TestWrappers::testAutoIpEmptyIface()
{
    // An empty interface must fail immediately without launching anything,
    // even in a headless test environment that has no polkit agent.
    vdj::AutoIpManager m;
    QSignalSpy errorSpy(&m, &vdj::AutoIpManager::errorOccurred);
    m.start(QString());
    QVERIFY(errorSpy.count() == 1);
    QVERIFY(!m.active());
}

void TestWrappers::testAutoIpStopWithoutStart()
{
    vdj::AutoIpManager m;
    QSignalSpy stoppedSpy(&m, &vdj::AutoIpManager::stopped);
    m.stop(); // no-op, must not crash or emit
    QVERIFY(!m.active());
    QVERIFY(stoppedSpy.count() == 0);
}

void TestWrappers::testToolbarConnectedState()
{
    vdj::Toolbar toolbar;

    QAction* connectAction = nullptr;
    QAction* disconnectAction = nullptr;
    const auto actions = toolbar.actions();
    for (QAction* a : actions) {
        if (a->text() == QStringLiteral("Connect"))
            connectAction = a;
        else if (a->text() == QStringLiteral("Disconnect"))
            disconnectAction = a;
        QVERIFY(a->text() != QStringLiteral("AutoIP"));   // AutoIP lives in the Network menu only
    }
    QVERIFY(connectAction);
    QVERIFY(disconnectAction);

    // Not connected: Connect usable, Disconnect off.
    toolbar.setConnectedState(false);
    QVERIFY(connectAction->isEnabled());
    QVERIFY(!disconnectAction->isEnabled());

    // Connected: flip the buttons.
    toolbar.setConnectedState(true);
    QVERIFY(!connectAction->isEnabled());
    QVERIFY(disconnectAction->isEnabled());
}

QTEST_MAIN(TestWrappers)
#include "test_wrappers.moc"
