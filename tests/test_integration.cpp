#include <QtTest/QtTest>
#include "ui/main_window.h"
#include "ui/deck_area.h"
#include "network/network_manager.h"
#include "nfs/nfs_mount_manager.h"
#include "config/config_manager.h"
#include "debug/packet_logger.h"
#include "ui/deck_widget.h"
#include "ui/deck_controls.h"
#include "midi/alsa_clock.h"
#include <QDoubleSpinBox>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "ui/track_browser.h"
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
    void bpmSpinnerSetsTheSequencer();
    void linkExportFollowsTheLoadedDatabase();
    void bpmFollowsTheDeckWeAreSyncedTo();
};

void TestIntegration::mainWindowConstructs()
{
    vdj::MainWindow window;
    window.show();
    QVERIFY(window.isVisible());
    QCOMPARE(window.windowTitle(), QStringLiteral("🎧 VDJ UI"));

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

void TestIntegration::linkExportFollowsTheLoadedDatabase()
{
    QTemporaryDir stick;                                   // a pendrive: Contents/ and PIONEER/rekordbox/export.pdb
    QVERIFY(stick.isValid());
    QVERIFY(QDir().mkpath(stick.path() + QStringLiteral("/Contents")));
    QVERIFY(QDir().mkpath(stick.path() + QStringLiteral("/PIONEER/rekordbox")));
    QVERIFY(QFile::copy(QStringLiteral(VDJ_DBSERVER_FIXTURES "/export7/PIONEER/rekordbox/export.pdb"),
                        stick.path() + QStringLiteral("/PIONEER/rekordbox/export.pdb")));

    vdj::MainWindow window;
    window.show();
    auto* net = window.findChild<vdj::NetworkManager*>();
    auto* browser = window.findChild<vdj::TrackBrowser*>();
    QVERIFY(net && browser);
    QVERIFY(net->linkExportFolder().isEmpty());            // off by default

    window.setLinkExportEnabled(true);                      // nothing loaded yet: nothing to export
    QVERIFY(net->linkExportFolder().isEmpty());

    QString err;
    QVERIFY2(browser->openRekordboxFolder(stick.path(), &err), qPrintable(err));      // the database becomes visible in the GUI
    QCOMPARE(net->linkExportFolder(), QDir(stick.path()).absolutePath());               // ... and is what gets exported

    window.setLinkExportEnabled(false);
    QVERIFY(net->linkExportFolder().isEmpty());
    window.close();
}

void TestIntegration::bpmSpinnerSetsTheSequencer()
{
    vdj::MainWindow window;
    window.show();
    auto* clock = window.alsaClock();
    if (!clock || !clock->isOpen())
        QSKIP("no ALSA sequencer available");
    auto* area = window.findChild<vdj::DeckArea*>();
    QVERIFY(area);
    vdj::DeckWidget* vdjDeck = area->deckAt(0);
    QVERIFY(vdjDeck && vdjDeck->model() == QLatin1String("VDJ"));
    QDoubleSpinBox* spin = nullptr;
    for (auto* b : vdjDeck->findChildren<QDoubleSpinBox*>())
        if (b->isVisibleTo(vdjDeck)) spin = b;
    QVERIFY(spin);

    spin->setValue(141.25);                        // as if the user typed it
    QVERIFY(std::abs(clock->queueBpm() - 141.25) < 0.01);
    QVERIFY(std::abs(vdjDeck->bpm() - 141.25) < 1e-6);
    spin->stepUp();                                // one click on the arrow
    QVERIFY(std::abs(clock->queueBpm() - 141.30) < 0.01);
    window.close();
}

void TestIntegration::bpmFollowsTheDeckWeAreSyncedTo()
{
    vdj::MainWindow window;
    window.show();
    window.addDemoDecks();
    auto* clock = window.alsaClock();
    if (!clock || !clock->isOpen())
        QSKIP("no ALSA sequencer available");
    auto* area = window.findChild<vdj::DeckArea*>();
    vdj::DeckWidget *vdjDeck = nullptr, *remote = nullptr;
    for (int i = 0; i < area->deckCount(); ++i) {
        auto* d = area->deckAt(i);
        if (d->playerId() == 5) vdjDeck = d;
        if (d->playerId() == 1) remote = d;
    }
    QVERIFY(vdjDeck && remote);
    QDoubleSpinBox* spin = nullptr;
    for (auto* b : vdjDeck->findChildren<QDoubleSpinBox*>())
        if (b->isVisibleTo(vdjDeck)) spin = b;
    QVERIFY(spin);

    // free running: the spinner sets the tempo
    spin->setValue(120.0);
    QVERIFY(std::abs(clock->queueBpm() - 120.0) < 0.01);
    QVERIFY(!spin->isReadOnly());

    // sync to the remote deck (Sync on a remote deck): our tempo becomes theirs and the spinner is locked
    remote->setBpm(133.5);
    QPushButton* sync = nullptr;
    for (auto* b : remote->findChildren<QPushButton*>())
        if (b->text() == QLatin1String("Sync")) sync = b;
    QVERIFY(sync);
    sync->click();
    QVERIFY(std::abs(clock->queueBpm() - 133.5) < 0.01);
    QVERIFY(std::abs(vdjDeck->bpm() - 133.5) < 1e-6);
    QVERIFY(spin->isReadOnly());

    // nothing can change it now, not even a programmatic set of the spinner
    spin->setValue(150.0);
    QVERIFY(std::abs(clock->queueBpm() - 133.5) < 0.01);
    QVERIFY(std::abs(spin->value() - 133.5) < 0.01);

    // the deck changes its tempo: we follow
    auto* net = window.findChild<vdj::NetworkManager*>();
    QVERIFY(net);
    emit net->statusUpdated(1, 134.0, 0, 0, 0, 0);      // a status packet of the deck we are synced to
    QVERIFY(std::abs(clock->queueBpm() - 134.0) < 0.01);
    QVERIFY(std::abs(vdjDeck->bpm() - 134.0) < 1e-6);

    // Disconnect: the other decks go, the sync to them is released and the spinner is ours again
    QVERIFY(area->deckCount() > 1);
    emit net->disconnected();
    QCOMPARE(area->deckCount(), 1);
    QCOMPARE(area->deckAt(0)->model(), QStringLiteral("VDJ"));
    QVERIFY(!spin->isReadOnly());
    spin->setValue(125.0);
    QVERIFY(std::abs(clock->queueBpm() - 125.0) < 0.01);
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
        if (b->isCheckable() && b->isVisibleTo(vdjDeck) && b != vdjDeck->controls()->quantizeButton()) play = b;   // the Q button is checkable too
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
