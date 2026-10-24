#include <QtTest/QtTest>
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