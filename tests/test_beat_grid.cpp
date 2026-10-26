#include <QtTest/QtTest>
#include <QDateTime>
#include "ui/deck_area.h"
#include "ui/deck_widget.h"
#include "ui/deck_controls.h"
#include "ui/beat_grid_widget.h"

class TestBeatGrid : public QObject
{
    Q_OBJECT

private slots:
    void deckAreaAddRemove();
    void bpmAndBarPosSetters();
    void playheadSpansFullBar();
    void sequencerBarLabelMoves();
    void beatGridMinWidthMatchesControls();
    void deckTitleShowsModelAndState();
    void playheadTakesFullBarToCross();
    void addDeckIsIdempotent();
    void beatReferenceStartsClock();
    void deckForwardsBeatTimestamp();
};

void TestBeatGrid::deckAreaAddRemove()
{
    vdj::DeckArea area;
    QCOMPARE(area.deckCount(), 0);

    auto* d1 = area.addDeck(1);
    auto* d2 = area.addDeck(5);
    QCOMPARE(area.deckCount(), 2);
    QCOMPARE(area.deckAt(0), d1);
    QCOMPARE(area.deckAt(1), d2);
    QCOMPARE(d1->playerId(), 1);
    QCOMPARE(d2->playerId(), 5);

    area.removeDeck(1);
    QCOMPARE(area.deckCount(), 1);
    QCOMPARE(area.deckAt(0), d2);

    area.clearDecks();
    QCOMPARE(area.deckCount(), 0);
}

void TestBeatGrid::bpmAndBarPosSetters()
{
    vdj::BeatGridWidget grid;
    grid.setBpm(124.0);
    QCOMPARE(grid.bpm(), 124.0);

    grid.setPlaying(false);
    QVERIFY(!grid.playing());

    grid.setBpm(0.0);
    QCOMPARE(grid.bpm(), 124.0);
}

void TestBeatGrid::sequencerBarLabelMoves()
{
    // the sequencer's bar position is raw beats + the whole-beat offset: one more beat puts the playhead on another beat of the bar
    vdj::BeatGridWidget w;
    w.resize(400, 60);
    w.setSequencerBeats(5.3);
    const QImage a = w.grab().toImage();
    w.setSequencerBeats(6.3);
    const QImage b = w.grab().toImage();
    QVERIFY(a != b);
    w.setSequencerBeats(9.3);                    // a bar later: the same place
    QCOMPARE(w.grab().toImage(), a);
}

void TestBeatGrid::playheadSpansFullBar()
{
    const int beatsPerBar = 4;

    // At the very start of beat 0 the playhead is at the left edge.
    QCOMPARE(vdj::BeatGridWidget::barPosition(0, 0.0, beatsPerBar), 0.0);

    // Halfway through beat 0 -> 1/8 of the bar.
    QVERIFY(qAbs(vdj::BeatGridWidget::barPosition(0, 0.5, beatsPerBar) - 0.125) < 1e-9);

    // End of beat 0 / start of beat 1 -> 1/4 of the bar.
    QVERIFY(qAbs(vdj::BeatGridWidget::barPosition(0, 0.999999, beatsPerBar) - 0.25) < 1e-4);
    QVERIFY(qAbs(vdj::BeatGridWidget::barPosition(1, 0.0, beatsPerBar) - 0.25) < 1e-9);

    // Halfway through beat 1 -> 3/8 of the bar.
    QVERIFY(qAbs(vdj::BeatGridWidget::barPosition(1, 0.5, beatsPerBar) - 0.375) < 1e-9);

    // At the end of beat 3 the playhead has traversed the whole bar.
    QVERIFY(qAbs(vdj::BeatGridWidget::barPosition(3, 0.999999, beatsPerBar) - 1.0) < 1e-4);

    // Out-of-range inputs are clamped.
    QCOMPARE(vdj::BeatGridWidget::barPosition(-1, 0.0, beatsPerBar), 0.0);
    QCOMPARE(vdj::BeatGridWidget::barPosition(9, 0.5, beatsPerBar), 0.875);
    QCOMPARE(vdj::BeatGridWidget::barPosition(0, 0.0, 0), 0.0);
}

void TestBeatGrid::beatGridMinWidthMatchesControls()
{
    vdj::DeckWidget deck(1);
    deck.show();
    QTest::qWait(20);
    QApplication::processEvents();

    auto* grid = deck.beatGrid();
    auto* ctrl = deck.controls();
    QVERIFY(grid);
    QVERIFY(ctrl);

    // The beat grid must be wide enough to show all 4 beats; it should open
    // at least as wide as the player controls (buttons) beneath it.
    QVERIFY(grid->minimumWidth() >= ctrl->sizeHint().width());
}

void TestBeatGrid::deckTitleShowsModelAndState()
{
    vdj::DeckWidget deck(5);

    // Default (no model, not playing): title identifies the deck + STOPPED.
    QVERIFY(deck.titleText().contains(QStringLiteral("DECK 5")));
    QVERIFY(deck.titleText().contains(QStringLiteral("STOPPED")));

    // A connected deck reports its model, which must appear in the title.
    deck.setModel(QStringLiteral("CDJ-2000NXS2"));
    QVERIFY(deck.titleText().contains(QStringLiteral("CDJ-2000NXS2")));

    // Play state must be reflected in the title and drive the timeline.
    deck.setPlaying(true);
    QVERIFY(deck.titleText().contains(QStringLiteral("PLAYING")));
    QVERIFY(deck.beatGrid()->playing());

    deck.setMaster(true);
    QVERIFY(deck.titleText().contains(QStringLiteral("MASTER")));
}

void TestBeatGrid::playheadTakesFullBarToCross()
{
    const int beatsPerBar = 4;

    // Within a SINGLE beat (bar_pos fixed at 1, phase sweeping 0..1), the
    // playhead must only move within the first quarter of the bar. The old
    // bug derived the beat index from phase so this swept the whole bar in
    // one beat (4x too fast).
    QVERIFY(vdj::BeatGridWidget::playheadBarFraction(1, 0.0, beatsPerBar) <
            vdj::BeatGridWidget::playheadBarFraction(1, 0.999, beatsPerBar));
    QVERIFY(vdj::BeatGridWidget::playheadBarFraction(1, 0.999, beatsPerBar) <= 0.25);

    // The playhead must only reach the right edge after bar_pos 4 (the last
    // beat of the bar).
    QVERIFY(vdj::BeatGridWidget::playheadBarFraction(4, 0.999, beatsPerBar) > 0.99);
    QVERIFY(vdj::BeatGridWidget::playheadBarFraction(2, 0.0, beatsPerBar) == 0.25);
    QVERIFY(vdj::BeatGridWidget::playheadBarFraction(3, 0.0, beatsPerBar) == 0.5);
    QVERIFY(vdj::BeatGridWidget::playheadBarFraction(4, 0.0, beatsPerBar) == 0.75);

    // bar_pos is 1-based; verify out-of-range is clamped.
    QCOMPARE(vdj::BeatGridWidget::playheadBarFraction(0, 0.0, beatsPerBar), 0.0);
    QCOMPARE(vdj::BeatGridWidget::playheadBarFraction(9, 0.5, beatsPerBar), 0.875);
}

void TestBeatGrid::addDeckIsIdempotent()
{
    vdj::DeckArea area;

    // Placeholder for player 1.
    auto* first = area.addDeck(1);
    QCOMPARE(area.deckCount(), 1);

    // A real deck 1 joining must NOT create a duplicate deck 1.
    auto* second = area.addDeck(1);
    QCOMPARE(area.deckCount(), 1);
    QCOMPARE(second, first);

    // Different player ids still create separate decks.
    area.addDeck(5);
    QCOMPARE(area.deckCount(), 2);
    QCOMPARE(area.deckByPlayerId(1), first);
    QCOMPARE(area.deckByPlayerId(3), nullptr);
}

void TestBeatGrid::beatReferenceStartsClock()
{
    // Before any beat reference the animation clock is off.
    vdj::BeatGridWidget grid;
    QVERIFY(!grid.clockActive());

    // A playing deck receiving a real beat time must start animating,
    // otherwise the timeline stays frozen even though bar_pos updates.
    grid.setPlaying(true);
    grid.setBpm(120.0);
    grid.setBeatTimestamp(QDateTime::currentMSecsSinceEpoch());
    QVERIFY(grid.clockActive());

    // A zero beat time (no reference) must not spin up the clock.
    vdj::BeatGridWidget idle;
    idle.setBeatTimestamp(0);
    QVERIFY(!idle.clockActive());
}

void TestBeatGrid::deckForwardsBeatTimestamp()
{
    // Decks that join after initial startup still get their timeline clock
    // started as soon as a real beat reference arrives.
    vdj::DeckWidget deck(7);
    QVERIFY(!deck.beatGrid()->clockActive());

    deck.setPlaying(true);
    deck.setBpm(128.0);
    deck.setBeatTimestamp(QDateTime::currentMSecsSinceEpoch());
    QVERIFY(deck.beatGrid()->clockActive());
}

QTEST_MAIN(TestBeatGrid)
#include "test_beat_grid.moc"
