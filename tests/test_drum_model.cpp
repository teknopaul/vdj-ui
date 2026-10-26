#include <QtTest/QtTest>
#include "audio/drum_pattern.h"
#include "audio/djtoolz_tool.h"
#include "audio/swing.h"

using namespace vdj;

class TestDrumModel : public QObject
{
    Q_OBJECT

private slots:
    void patternStringRoundTrip();
    void patternStringRejects();
    void stepLevels();
    void voiceNames();
    void swingZeroDelaysNothing();
    void swing127DelaysEverySecondStep();
    void swingScalesLinearlyAndClamps();
    void swingNeverPassesTheNextStep();
    void swingNegativeIndices();
    void chokeClamp_data();
    void chokeClamp();
    void chokeNextWalksTheList();
    void settingsClamping();
    void drumMachineVariantRoundTrip();
    void drumMachineVariantEdgeCases();
    void swingIsSavedForDembomatikAndDrumsOnly();
    void otherTypesStayByteIdentical();
    void voiceMuteVariant();
};

void TestDrumModel::patternStringRoundTrip()
{
    quint8 steps[kDrumSteps] = { 2, 1, 0, 0, 1, 1, 0, 2, 0, 0, 1, 0, 0, 0, 2, 1 };
    const QString s = drumPatternToString(steps);
    QCOMPARE(s, QString("2100110200100021"));
    quint8 back[kDrumSteps] = {};
    QVERIFY(drumPatternFromString(s, back));
    QVERIFY(std::equal(steps, steps + kDrumSteps, back));
    quint8 none[kDrumSteps] = {};
    QCOMPARE(drumPatternToString(none), QString("0000000000000000"));
}

void TestDrumModel::patternStringRejects()
{
    quint8 keep[kDrumSteps];
    std::fill(keep, keep + kDrumSteps, quint8(1));
    for (const QString& bad : { QString("000000000000000"), QString("00000000000000000"), QString("000000000000000x"), QString("0000000000000003"),
                                QString(), QString("0000 000000000000") }) {
        QVERIFY2(!drumPatternFromString(bad, keep), qPrintable(bad));
        QCOMPARE(int(keep[0]), 1);                   // untouched
        QCOMPARE(int(keep[15]), 1);
    }
}

void TestDrumModel::stepLevels()
{
    QCOMPARE(drumStepLevel(StepOff), 0.0f);
    QCOMPARE(drumStepLevel(StepOn), 1.0f);
    QCOMPARE(drumStepLevel(StepHalf), 0.5f);
    QCOMPARE(drumStepBeat(0), 0.0);
    QCOMPARE(drumStepBeat(5), 1.25);
    QCOMPARE(drumStepBeat(15), 3.75);
}

void TestDrumModel::voiceNames()
{
    QCOMPARE(QString(kDrumVoiceNames[0]), QString("BD"));
    QCOMPARE(QString(kDrumVoiceNames[5]), QString("OH"));
    QCOMPARE(kDrumVoices, 6);
}

void TestDrumModel::swingZeroDelaysNothing()
{
    for (int i = 0; i < 32; ++i)
        QCOMPARE(swingDelayBeats(i, 0), 0.0);
}

void TestDrumModel::swing127DelaysEverySecondStep()
{
    // 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16
    //  n  y  n  y  n  y  n  y  n  y  n  y  n  y  n  y     (1-based, y = delayed)
    for (int i = 0; i < 16; ++i)
        QCOMPARE(swingDelayBeats(i, 127), (i % 2 == 1) ? 0.25 : 0.0);
    QCOMPARE(kSwingMaxDelayBeats, 0.25);
}

void TestDrumModel::swingScalesLinearlyAndClamps()
{
    QVERIFY(qAbs(swingDelayBeats(1, 64) - 0.25 * 64.0 / 127.0) < 1e-12);
    QVERIFY(qAbs(swingDelayBeats(3, 1) - 0.25 / 127.0) < 1e-12);
    QCOMPARE(swingDelayBeats(1, -5), 0.0);
    QCOMPARE(swingDelayBeats(1, 500), 0.25);
    QCOMPARE(swingClamp(-1), 0);
    QCOMPARE(swingClamp(999), 127);
    QCOMPARE(swingClamp(60), 60);
}

void TestDrumModel::swingNeverPassesTheNextStep()
{
    for (int swing = 0; swing <= 127; ++swing)
        for (int i = 0; i < 16; ++i)
            QVERIFY(drumStepBeat(i) + swingDelayBeats(i, swing) <= drumStepBeat(i + 1) + 1e-12);
    QCOMPARE(drumStepBeat(1) + swingDelayBeats(1, 127), drumStepBeat(2));      // lands on the next step's position
    QCOMPARE(drumStepBeat(15) + swingDelayBeats(15, 127), drumStepBeat(16));   // = the next bar's step 0
}

void TestDrumModel::swingNegativeIndices()
{
    QCOMPARE(swingDelayBeats(-1, 127), 0.25);        // odd
    QCOMPARE(swingDelayBeats(-2, 127), 0.0);
    QCOMPARE(swingDelayBeats(-3, 127), 0.25);
}

void TestDrumModel::chokeClamp_data()
{
    QTest::addColumn<int>("in");
    QTest::addColumn<int>("out");
    QTest::newRow("0") << 0 << 0;
    QTest::newRow("1") << 1 << 1;
    QTest::newRow("2") << 2 << 2;
    QTest::newRow("3 ties down") << 3 << 2;
    QTest::newRow("4") << 4 << 4;
    QTest::newRow("5") << 5 << 4;
    QTest::newRow("7") << 7 << 8;
    QTest::newRow("8") << 8 << 8;
    QTest::newRow("11") << 11 << 8;
    QTest::newRow("13") << 13 << 16;
    QTest::newRow("100") << 100 << 16;
    QTest::newRow("-1") << -1 << 0;
    QTest::newRow("-50") << -50 << 0;
}

void TestDrumModel::chokeClamp()
{
    QFETCH(int, in);
    QFETCH(int, out);
    QCOMPARE(drumChokeClamp(in), out);
}

void TestDrumModel::chokeNextWalksTheList()
{
    int v = 0;
    QList<int> up;
    for (int i = 0; i < 8; ++i) {
        v = drumChokeNext(v, +1);
        up << v;
    }
    QCOMPARE(up, QList<int>({ 1, 2, 4, 8, 16, 16, 16, 16 }));       // stops at the end
    QList<int> down;
    for (int i = 0; i < 7; ++i) {
        v = drumChokeNext(v, -1);
        down << v;
    }
    QCOMPARE(down, QList<int>({ 8, 4, 2, 1, 0, 0, 0 }));
    QCOMPARE(drumChokeNext(3, +1), 4);               // an odd stored value is snapped first (3 -> 2), then stepped
    QCOMPARE(drumChokeNext(2, 0), 2);
}

void TestDrumModel::settingsClamping()
{
    auto m = DjtoolzTool::create(DjtoolzTool::DrumMachine);
    QVERIFY(m);
    ToolSettings s = m->settings();
    s.swing = 900;
    s.drums[0].volume = 500;
    s.drums[1].volume = -4;
    s.drums[2].offsetMs = 1000;
    s.drums[3].offsetMs = -1000;
    s.drums[4].chokeSteps = 3;
    s.drums[5].chokeSteps = 77;
    s.drums[0].steps[3] = 9;
    m->setSettings(s);
    const ToolSettings c = m->settings();
    QCOMPARE(c.swing, 127);
    QCOMPARE(c.drums[0].volume, 127);
    QCOMPARE(c.drums[1].volume, 0);
    QCOMPARE(c.drums[2].offsetMs, DjtoolzTool::kMaxOffsetMs);
    QCOMPARE(c.drums[3].offsetMs, -DjtoolzTool::kMaxOffsetMs);
    QCOMPARE(c.drums[4].chokeSteps, 2);
    QCOMPARE(c.drums[5].chokeSteps, 16);
    QCOMPARE(int(c.drums[0].steps[3]), int(StepHalf));
}

void TestDrumModel::voiceMuteVariant()
{
    DrumVoice v;
    v.samplePath = "/a.wav";
    QVERIFY(!drumVoiceToVariant(v).contains("muted"));          // old files stay identical
    v.muted = true;
    const QVariantMap m = drumVoiceToVariant(v);
    QCOMPARE(m.value("muted").toBool(), true);
    QVERIFY(!drumVoiceToVariant(v, false).contains("muted"));   // kits leave it out
    DrumVoice back;
    drumVoiceFromVariant(m, &back);
    QVERIFY(back.muted);
    QVERIFY(back == v);
    DrumVoice other = v;
    other.muted = false;
    QVERIFY(other != v);
    QVariantMap old = m;
    old.remove("muted");
    DrumVoice fromOld;
    drumVoiceFromVariant(old, &fromOld);                        // a file without the key
    QVERIFY(!fromOld.muted);
    // through a whole tool
    auto mod = DjtoolzTool::create(DjtoolzTool::DrumMachine);
    ToolSettings s = mod->settings();
    s.drums[2].muted = true;
    mod->setSettings(s);
    auto copy = DjtoolzTool::fromVariant(mod->toVariant());
    QVERIFY(copy);
    QVERIFY(copy->settings().drums[2].muted);
    QVERIFY(!copy->settings().drums[1].muted);
}

void TestDrumModel::drumMachineVariantRoundTrip()
{
    auto m = DjtoolzTool::create(DjtoolzTool::DrumMachine);
    QCOMPARE(m->typeName(), QString("DR-6S"));
    ToolSettings s = m->settings();
    s.name = "my kit";
    s.swing = 70;
    for (int v = 0; v < kDrumVoices; ++v) {
        s.drums[v].samplePath = QString("/s/%1.wav").arg(kDrumVoiceNames[v]);
        s.drums[v].volume = 20 + v * 20;
        s.drums[v].offsetMs = v * 3 - 7;
        s.drums[v].chokeSteps = kDrumChokeValues[v];
        for (int i = 0; i < kDrumSteps; ++i)
            s.drums[v].steps[i] = quint8((i + v) % 3);
    }
    m->setSettings(s);
    const QVariantMap vm = m->toVariant();
    QCOMPARE(vm.value("type").toString(), QString("drums"));
    QCOMPARE(vm.value("voices").toList().size(), 6);
    QCOMPARE(vm.value("swing").toInt(), 70);
    auto back = DjtoolzTool::fromVariant(vm);
    QVERIFY(back);
    QCOMPARE(back->type(), DjtoolzTool::DrumMachine);
    const ToolSettings b = back->settings();
    QCOMPARE(b.swing, 70);
    QCOMPARE(b.name, QString("my kit"));
    for (int v = 0; v < kDrumVoices; ++v)
        QVERIFY2(b.drums[v] == m->settings().drums[v], kDrumVoiceNames[v]);
    QCOMPARE(back->toVariant(), vm);                 // and again
}

void TestDrumModel::drumMachineVariantEdgeCases()
{
    // fewer voices than six, a bad pattern, missing keys, extra voices
    QVariantList voices;
    voices << QVariantMap{ { "sample", "/a.wav" }, { "volume", 50 }, { "pattern", "1000100010001000" } }
           << QVariantMap{ { "pattern", "nonsense" }, { "choke", 4 } };
    auto m = DjtoolzTool::fromVariant(QVariantMap{ { "type", "drums" }, { "voices", voices } });
    QVERIFY(m);
    const ToolSettings s = m->settings();
    QCOMPARE(s.drums[0].samplePath, QString("/a.wav"));
    QCOMPARE(s.drums[0].volume, 50);
    QCOMPARE(int(s.drums[0].steps[4]), 1);
    QCOMPARE(s.drums[1].volume, 127);                // default
    QCOMPARE(s.drums[1].chokeSteps, 4);
    QCOMPARE(drumPatternToString(s.drums[1].steps), QString("0000000000000000"));   // a bad pattern is empty
    QCOMPARE(s.drums[5].samplePath, QString());      // the voices that were not in the file
    QCOMPARE(s.swing, 0);
    QVariantList many;
    for (int i = 0; i < 9; ++i)
        many << QVariantMap{ { "sample", QString("/%1.wav").arg(i) } };
    auto big = DjtoolzTool::fromVariant(QVariantMap{ { "type", "drums" }, { "voices", many } });
    QCOMPARE(big->settings().drums[5].samplePath, QString("/5.wav"));        // the first six are used
    QCOMPARE(big->toVariant().value("voices").toList().size(), 6);
}

void TestDrumModel::swingIsSavedForDembomatikAndDrumsOnly()
{
    auto d = DjtoolzTool::create(DjtoolzTool::Dembomatik);
    QVERIFY(d->supportsSwing());
    QVERIFY(!d->hasDrumVoices());
    QVERIFY(d->toVariant().contains("swing"));
    QVERIFY(!d->toVariant().contains("voices"));
    QCOMPARE(d->toVariant().value("swing").toInt(), 0);
    ToolSettings s = d->settings();
    s.swing = 99;
    d->setSettings(s);
    QCOMPARE(DjtoolzTool::fromVariant(d->toVariant())->settings().swing, 99);
    // an old Dembomatik preset without the key
    auto old = DjtoolzTool::fromVariant(QVariantMap{ { "type", "dembow" }, { "name", "old" } });
    QCOMPARE(old->settings().swing, 0);
    QVERIFY(DjtoolzTool::create(DjtoolzTool::DrumMachine)->supportsSwing());
}

void TestDrumModel::otherTypesStayByteIdentical()
{
    for (auto t : { DjtoolzTool::Bombomatic, DjtoolzTool::StabPlayer, DjtoolzTool::AcidLoop, DjtoolzTool::OneShot, DjtoolzTool::MidiMatic }) {
        auto m = DjtoolzTool::create(t);
        QVERIFY(m);
        const QVariantMap vm = m->toVariant();
        QVERIFY2(!vm.contains("swing") && !vm.contains("voices"), qPrintable(m->typeName()));
        // a swing / voices key in a file for these types is ignored
        QVariantMap in = vm;
        in["swing"] = 50;
        in["voices"] = QVariantList{ QVariantMap{ { "sample", "/x.wav" } } };
        auto back = DjtoolzTool::fromVariant(in);
        QCOMPARE(back->settings().swing, 0);
        QCOMPARE(back->settings().drums[0].samplePath, QString());
    }
}

QTEST_MAIN(TestDrumModel)
#include "test_drum_model.moc"
