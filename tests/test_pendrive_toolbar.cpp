#include <QtTest>

#include <QComboBox>
#include <QSignalSpy>

#include "export/pendrive_registry.h"
#include "ui/pendrive_toolbar.h"

using namespace vdj;

class TestPendriveToolBar : public QObject {
    Q_OBJECT

private slots:
    void decksAreListedAndHideTheInsertButtons()
    {
        PendriveRegistry registry;
        PendriveToolBar bar(&registry);
        bar.show();
        QVERIFY(bar.buttonsShown());                                    // a local pendrive (or nothing): Insert and USB STOP are there
        const int base = bar.combo()->count();
        QSignalSpy deckAsked(&bar, &PendriveToolBar::deckRequested);
        bar.setRemoteDecks({{2, "169.254.1.2", "Deck 2  CDJ-3000"}, {3, "169.254.1.3", "Deck 3  XDJ-1000MK2"}});
        QCOMPARE(bar.combo()->count(), base + 2);
        QVERIFY(bar.combo()->itemText(base).contains("Deck 2"));
        QVERIFY(bar.combo()->itemText(base).contains("network"));

        // choosing a deck asks for its database; once it is shown the two buttons are hidden
        bar.combo()->setCurrentIndex(base + 1);
        QMetaObject::invokeMethod(bar.combo(), "activated", Q_ARG(int, base + 1));
        QCOMPARE(deckAsked.count(), 1);
        QCOMPARE(deckAsked.at(0).at(0).toInt(), 3);
        QCOMPARE(deckAsked.at(0).at(1).toString(), QString("169.254.1.3"));
        bar.setRemoteViewed(3);
        QVERIFY(!bar.buttonsShown());
        QCOMPARE(bar.remoteViewed(), 3);
        QCOMPARE(bar.combo()->currentIndex(), base + 1);                 // the deck is the chosen item

        // the list changing (a deck joins) keeps the deck chosen and the buttons hidden
        bar.setRemoteDecks({{2, "169.254.1.2", "Deck 2  CDJ-3000"}, {3, "169.254.1.3", "Deck 3  XDJ-1000MK2"}, {4, "169.254.1.4", "Deck 4"}});
        QVERIFY(!bar.buttonsShown());
        QCOMPARE(bar.combo()->currentIndex(), base + 1);

        // a local folder shown again brings the buttons back
        bar.setViewed("/some/pendrive");
        QVERIFY(bar.buttonsShown());
        QCOMPARE(bar.remoteViewed(), 0);

        // no network: no decks in the list
        bar.setRemoteDecks({});
        QCOMPARE(bar.combo()->count(), base);
    }
};

QTEST_MAIN(TestPendriveToolBar)
#include "test_pendrive_toolbar.moc"
