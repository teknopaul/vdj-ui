#include <QtTest/QtTest>
#include "midi/joystick_map.h"

using vdj::JoystickAction;
using vdj::mapJoystickEvent;

class TestJoystickMap : public QObject
{
    Q_OBJECT

private slots:
    void tempoAxes();
    void nudgeAxes();
    void buttons();
    void ignored();
};

void TestJoystickMap::tempoAxes()
{
    auto a = mapJoystickEvent(true, 0, 32767);
    QCOMPARE(a.kind, JoystickAction::TempoAdjust);
    QCOMPARE(a.value, 0.1);
    QCOMPARE(mapJoystickEvent(true, 0, -32767).value, -0.1);
    a = mapJoystickEvent(true, 1, -32767);          // stick up
    QCOMPARE(a.kind, JoystickAction::TempoAdjust);
    QCOMPARE(a.value, 1.0);
    QCOMPARE(mapJoystickEvent(true, 1, 32767).value, -1.0);
}

void TestJoystickMap::nudgeAxes()
{
    auto a = mapJoystickEvent(true, 2, 5);
    QCOMPARE(a.kind, JoystickAction::Nudge);
    QCOMPARE(a.value, 10.0);
    QCOMPARE(mapJoystickEvent(true, 2, -5).value, -10.0);
    QCOMPARE(mapJoystickEvent(true, 3, 5).value, 1.0);
    QCOMPARE(mapJoystickEvent(true, 3, -5).value, -1.0);
    QCOMPARE(mapJoystickEvent(true, 3, 5).kind, JoystickAction::Nudge);
}

void TestJoystickMap::buttons()
{
    QCOMPARE(mapJoystickEvent(false, 8, 1).kind, JoystickAction::Toggle);
    QCOMPARE(mapJoystickEvent(false, 9, 1).kind, JoystickAction::Stop);
    QCOMPARE(mapJoystickEvent(false, 10, 1).kind, JoystickAction::QuantizedRestart);
    QCOMPARE(mapJoystickEvent(false, 11, 1).kind, JoystickAction::QuantizedRestart);
}

void TestJoystickMap::ignored()
{
    QCOMPARE(mapJoystickEvent(true, 0, 0).kind, JoystickAction::None);      // stick back in the middle
    QCOMPARE(mapJoystickEvent(true, 7, 100).kind, JoystickAction::None);    // unknown axis
    QCOMPARE(mapJoystickEvent(false, 10, 0).kind, JoystickAction::None);    // button release
    QCOMPARE(mapJoystickEvent(false, 3, 1).kind, JoystickAction::None);     // unmapped button
    QCOMPARE(mapJoystickEvent(false, 99, 1).kind, JoystickAction::None);
}

QTEST_APPLESS_MAIN(TestJoystickMap)
#include "test_joystick_map.moc"
