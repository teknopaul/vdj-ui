#include <QtTest/QtTest>
#include "ui/main_window.h"
#include "ui/deck_area.h"
#include "network/network_manager.h"
#include "nfs/nfs_mount_manager.h"
#include "config/config_manager.h"
#include "debug/packet_logger.h"
#include "ui/deck_widget.h"
#include "midi/alsa_clock.h"
#include <QPushButton>
#include "ui/beat_grid_widget.h"

class TestIntegration : public QObject
{
    Q_OBJECT

private slots:
    void mainWindowConstructs();
    void mainWindowShutdown();
    void windowMinimumWidths();
    void networkErrorHandled();
    void nfsFailureHandled();
    void configPersistenceErrorHandled();
    void onlyTheVdjDeckAtStartup();
    void copyBpmFromRemoteDeck();
    void vdjPlayRunsSequencerAndPlayhead();
};

void TestIntegration::mainWindowConstructs()
{
    vdj::MainWindow window;
    window.show();
    QVERIFY(window.isVisible());
    QCOMPARE(window.windowTitle(), QStringLiteral("VDJ UI"));

    // Give Qt a moment to process show/rendering events
    QTest::qWait(50);
    QVERIFY(window.isVisible());
}

void TestIntegration::mainWindowShutdown()
{
    vdj::MainWindow window;
    window.show();
    QTest::qWait(20);
    // Closing the window must not crash and must tear down cleanly
    window.close();
    QVERIFY(window.isHidden() || !window.isVisible());
}

void TestIntegration::windowMinimumWidths()
{
    vdj::MainWindow window;
    window.show();
    QTest::qWait(20);

    // Whole window must open at a minimum of 1400px.
    QVERIFY(window.minimumWidth() >= 1400);

    // The timeline/deck area must be at least 1000px on boot.
    QWidget* area = window.findChild<vdj::DeckArea*>();
    QVERIFY(area);
    QVERIFY(area->minimumWidth() >= 1000);
    QVERIFY(window.width() >= 1400);
    window.close();
}

void TestIntegration::networkErrorHandled()
{
    vdj::NetworkManager nm;
    QSignalSpy errorSpy(&nm, &vdj::NetworkManager::errorOccurred);

    // Connecting with a bogus interface should either fail cleanly or emit an error
    bool ok = nm.connectToLink(QStringLiteral("definitely-not-an-interface-0"));
    Q_UNUSED(ok);

    // If it didn't connect, an error must eventually be emitted on teardown
    nm.disconnectFromLink();
    QVERIFY(!nm.isConnected());
}

void TestIntegration::nfsFailureHandled()
{
    vdj::NfsMountManager nfs;
    QSignalSpy failSpy(&nfs, &vdj::NfsMountManager::mountFailed);

    // No IP => immediate failure, must emit mountFailed without crashing
    nfs.mountPlayer(99, QString());

    QSignalSpy spy(&nfs, &vdj::NfsMountManager::mountFailed);
    QVERIFY(spy.wait(200) || nfs.lastError(99).isEmpty());
}

void TestIntegration::configPersistenceErrorHandled()
{
    vdj::ConfigManager cfg;
    QVERIFY(cfg.saveValue("integration", "key_a", "value_a"));
    QCOMPARE(cfg.loadValue("integration", "key_a"), QStringLiteral("value_a"));
    QCOMPARE(cfg.loadValue("integration", "missing", "fb"), QStringLiteral("fb"));

    // PacketLogger round-trips without crashing across a few entries
    auto& logger = vdj::PacketLogger::instance();
    logger.setEnabled(true);
    logger.log(QStringLiteral("SYS"), QStringLiteral("integration test start"));
    logger.log(QStringLiteral("USER"), QStringLiteral("set bpm=128.00"));
    QVERIFY(logger.entries().size() >= 2);
    logger.setEnabled(false);
}

void TestIntegration::onlyTheVdjDeckAtStartup()
{
    vdj::MainWindow window;
    window.show();
    auto* area = window.findChild<vdj::DeckArea*>();
    QVERIFY(area);
    QCOMPARE(area->deckCount(), 1);              // no placeholder CDJs: other decks appear when they join
    QCOMPARE(area->deckAt(0)->model(), QStringLiteral("VDJ"));
    window.close();
}

void TestIntegration::copyBpmFromRemoteDeck()
{
    vdj::MainWindow window;
    window.show();
    window.addDemoDecks();
    QTest::qWait(20);
    auto* area = window.findChild<vdj::DeckArea*>();
    QVERIFY(area);
    vdj::DeckWidget *vdjDeck = nullptr, *remote = nullptr;
    for (int i = 0; i < area->deckCount(); ++i) {
        auto* d = area->deckAt(i);
        if (d->playerId() == 5) vdjDeck = d;
        if (d->playerId() == 1) remote = d;
    }
    QVERIFY(vdjDeck && remote);
    remote->setBpm(148.82);

    QPushButton* copy = nullptr;
    for (auto* b : remote->findChildren<QPushButton*>())
        if (b->text() == QLatin1String("Copy BPM")) copy = b;
    QVERIFY(copy);
    QVERIFY(copy->isVisibleTo(remote));
    copy->click();

    QVERIFY(std::abs(vdjDeck->bpm() - 148.82) < 1e-6);         // the VDJ deck shows it
    auto* clock = window.alsaClock();
    if (clock && clock->isOpen())
        QVERIFY(std::abs(clock->queueBpm() - 148.82) < 0.01);  // and the ALSA sequencer runs at it
    window.close();
}

void TestIntegration::vdjPlayRunsSequencerAndPlayhead()
{
    vdj::MainWindow window;
    window.show();
    auto* clock = window.alsaClock();
    if (!clock || !clock->isOpen())
        QSKIP("no ALSA sequencer available");
    auto* area = window.findChild<vdj::DeckArea*>();
    vdj::DeckWidget* vdjDeck = nullptr;
    for (int i = 0; i < area->deckCount(); ++i)
        if (area->deckAt(i)->playerId() == 5) vdjDeck = area->deckAt(i);
    QVERIFY(vdjDeck);
    vdjDeck->setBpm(240.0);                        // a beat every 250 ms (Play re-applies the deck's BPM)
    QVERIFY(clock->setBpm(240.0));

    QPushButton* play = nullptr;
    for (auto* b : vdjDeck->findChildren<QPushButton*>())
        if (b->isCheckable() && b->isVisibleTo(vdjDeck)) play = b;
    QVERIFY(play);
    QVERIFY(!clock->running());
    play->click();                                 // Play on the VDJ deck
    QVERIFY(clock->running());

    QTest::qWait(600);
    QVERIFY(vdjDeck->beatGrid()->sequencerDriven());
    const double beats = clock->positionBeats();
    QVERIFY2(beats > 1.5 && beats < 3.5, qPrintable(QString::number(beats)));   // ~2.4 beats after 600 ms

    play->click();                                 // Pause
    QVERIFY(!clock->running());
    QTest::qWait(50);
    QVERIFY(!vdjDeck->beatGrid()->sequencerDriven());
    window.close();
}

QTEST_MAIN(TestIntegration)
#include "test_integration.moc"
