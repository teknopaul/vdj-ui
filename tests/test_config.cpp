#include <QtTest/QtTest>
#include "config/config_manager.h"
#include "ui/usb_config_dialog.h"
#include "ui/joystick_config_dialog.h"

class TestConfig : public QObject
{
    Q_OBJECT

private slots:
    void configPersistence();
    void midiScanRuns();
    void joystickScanRuns();
};

void TestConfig::configPersistence()
{
    vdj::ConfigManager cfg;
    QVERIFY(cfg.saveValue("test", "key1", "value1"));
    QVERIFY(cfg.saveValue("test", "key2", "value2"));

    QCOMPARE(cfg.loadValue("test", "key1"), QStringLiteral("value1"));
    QCOMPARE(cfg.loadValue("test", "key2"), QStringLiteral("value2"));
    QCOMPARE(cfg.loadValue("test", "missing", "fallback"), QStringLiteral("fallback"));

    QVERIFY(cfg.saveJoystickMapping("js99", "play_pause", "button_3"));
    QCOMPARE(cfg.loadJoystickMapping("js99", "play_pause"), QStringLiteral("button_3"));

    QSignalSpy spy(&cfg, &QObject::destroyed);
    Q_UNUSED(spy);
}

void TestConfig::midiScanRuns()
{
    // Should never crash; may return empty if no ALSA seq clients
    const auto clients = vdj::UsbConfigDialog::scanMidiClients();
    QVERIFY(clients.size() >= 0);
}

void TestConfig::joystickScanRuns()
{
    // Should never crash; may return empty if no joysticks
    const auto sticks = vdj::JoystickConfigDialog::scanJoysticks();
    QVERIFY(sticks.size() >= 0);
}

QTEST_MAIN(TestConfig)
#include "test_config.moc"