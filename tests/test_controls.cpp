#include <QtTest/QtTest>
#include <QPushButton>
#include "ui/deck_controls.h"
#include "ui/deck_widget.h"
#include "ui/pitch_slider.h"

class TestControls : public QObject
{
    Q_OBJECT

private slots:
    void pitchSliderSignals();
    void deckControlsDefaultState();
    void deckControlsSignals();
    void remoteDecksAreDisplayOnly();
    void deckWidgetRemoteMode();
    void quantizeAndJumpButtons();
    void playGlyphToggles();
};

void TestControls::pitchSliderSignals()
{
    vdj::PitchSlider slider;
    QCOMPARE(slider.percentage(), 0.0);

    double captured = 0.0;
    QObject::connect(&slider, &vdj::PitchSlider::pitchChanged,
                     [&](double pct) { captured = pct; });

    slider.setPercentage(3.5);
    QCOMPARE(slider.percentage(), 3.5);

    slider.setPercentage(-1.25);
    QCOMPARE(slider.percentage(), -1.25);
}

void TestControls::deckControlsDefaultState()
{
    vdj::DeckControls controls(7);
    QCOMPARE(controls.playerId(), 7);
    QCOMPARE(controls.bpm(), 128.00);
    QCOMPARE(controls.barPos(), 0);

    controls.setBpm(140.0);
    QCOMPARE(controls.bpm(), 140.0);

    controls.setPlaying(false);
    QVERIFY(true);
}

void TestControls::deckControlsSignals()
{
    vdj::DeckControls controls(2);

    int playPid = 0;
    bool playState = true;
    QObject::connect(&controls, &vdj::DeckControls::playPauseClicked,
                     [&](int pid, bool play) { playPid = pid; playState = play; });

    // decks start stopped (button unchecked), so starting must emit
    controls.setPlaying(true);
    QCOMPARE(playPid, 2);
    QVERIFY(playState);

    // toggling back to stopped emits again
    controls.setPlaying(false);
    QCOMPARE(playPid, 2);
    QVERIFY(!playState);
    controls.setPlaying(true);
    QVERIFY(playState);

    // no new emission if state unchanged
    controls.setPlaying(true);
    QCOMPARE(playPid, 2);

    controls.setBpm(124.5);
    QCOMPARE(controls.bpm(), 124.5);
    controls.setBarPos(7);
    QCOMPARE(controls.barPos(), 7);
}

void TestControls::remoteDecksAreDisplayOnly()
{
    // Local (VDJ) deck: BPM editable, Bar display-only, transport available.
    vdj::DeckControls local(5);
    QVERIFY(local.bpmEditable());
    QVERIFY(!local.barEditable());
    QVERIFY(local.playVisible());
    QVERIFY(local.copyBpmVisible());

    // Remote deck: no editable fields, and Copy BPM is the only action.
    local.setRemote(true);
    QVERIFY(local.remote());
    QVERIFY(!local.bpmEditable());
    QVERIFY(!local.playVisible());
    QVERIFY(local.copyBpmVisible());
}

void TestControls::quantizeAndJumpButtons()
{
    vdj::DeckControls c(5);
    QCOMPARE(c.quantizeButton()->text(), QStringLiteral("Q"));
    QCOMPARE(c.quantizeButton()->toolTip(), QStringLiteral("Quantize"));
    QVERIFY(c.quantizeButton()->isCheckable());
    QVERIFY(!c.quantizeOn());
    QVERIFY(c.jumpsVisible());
    QCOMPARE(c.restartButton()->text(), QString::fromUtf8("\u23ee\ufe0f"));
    QCOMPARE(c.backButton()->text(), QString::fromUtf8("\u23ea"));
    QCOMPARE(c.forwardButton()->text(), QString::fromUtf8("\u23e9"));

    // disabled until the sequencer runs
    QVERIFY(!c.restartButton()->isEnabled());
    QVERIFY(!c.backButton()->isEnabled());
    QVERIFY(!c.forwardButton()->isEnabled());
    c.setJumpsEnabled(true);

    int restartPid = 0, jumps = 0, lastBeats = 0;
    bool restartQ = false;
    QObject::connect(&c, &vdj::DeckControls::restartClicked, [&](int pid, bool q) { restartPid = pid; restartQ = q; });
    QObject::connect(&c, &vdj::DeckControls::beatJumpClicked, [&](int, int beats) { ++jumps; lastBeats = beats; });
    c.restartButton()->click();
    QCOMPARE(restartPid, 5);
    QVERIFY(!restartQ);
    c.quantizeButton()->click();
    QVERIFY(c.quantizeOn());
    c.restartButton()->click();
    QVERIFY(restartQ);
    c.backButton()->click();
    QCOMPARE(lastBeats, -1);
    c.forwardButton()->click();
    QCOMPARE(lastBeats, 1);
    QCOMPARE(jumps, 2);

    // remote decks are display-only
    c.setRemote(true);
    QVERIFY(!c.jumpsVisible());
    QVERIFY(c.quantizeButton()->isHidden());
    QVERIFY(c.restartButton()->isHidden());
    c.setRemote(false);
    QVERIFY(c.jumpsVisible());
}

void TestControls::playGlyphToggles()
{
    vdj::DeckControls c(5);
    QPushButton* toggle = nullptr;
    for (auto* b : c.findChildren<QPushButton*>())
        if (b->isCheckable() && b != c.quantizeButton())   // the play | pause toggle
            toggle = b;
    QVERIFY(toggle);
    QCOMPARE(toggle->text(), QString::fromUtf8("\u25b6\ufe0f"));
    c.setPlaying(true);
    QCOMPARE(toggle->text(), QString::fromUtf8("\u23f8\ufe0f"));
    c.setPlaying(false);
    QCOMPARE(toggle->text(), QString::fromUtf8("\u25b6\ufe0f"));
}

void TestControls::deckWidgetRemoteMode()
{
    vdj::DeckWidget deck(1);
    deck.show();
    QTest::qWait(20);
    QApplication::processEvents();

    QVERIFY(!deck.isRemote());
    auto* pitch = deck.findChild<vdj::PitchSlider*>();
    QVERIFY(pitch);
    QVERIFY(!pitch->isHidden());
    QVERIFY(deck.controls()->playVisible());

    deck.setRemote(true);
    QVERIFY(deck.isRemote());
    QVERIFY(pitch->isHidden());
    QVERIFY(!deck.controls()->playVisible());
    QVERIFY(deck.controls()->copyBpmVisible());
}

QTEST_MAIN(TestControls)
#include "test_controls.moc"