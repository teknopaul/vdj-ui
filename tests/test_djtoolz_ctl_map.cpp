#include <QtTest>
#include <QTemporaryDir>
#include "midi/djtoolz_ctl_map.h"
#include "midi/pickup.h"

using namespace vdj;

class TestDjtoolzCtlMap : public QObject {
    Q_OBJECT

    QTemporaryDir dir_;

    QString write(const QString& name, const QByteArray& text)
    {
        const QString p = dir_.filePath(name);
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write(text);
        return p;
    }
    static QByteArray slurp(const QString& p) { QFile f(p); f.open(QIODevice::ReadOnly); return f.readAll(); }
    static AdjmmMap* load(const QString& p) { return AdjmmMap::readFromFile(p); }

private slots:
    void parsesEveryDjtoolzName()
    {
        QByteArray t;
        for (int s : { 1, 8 })
            for (const char* c : { "mute", "solo", "rec", "select", "knob", "fader" })
                t += QStringLiteral("strip%1_%2 * 10 127\n").arg(s).arg(c).toUtf8();
        t += "track_back 2 N 40\ntrack_fwd 0 59 127\nmode_toggle 0 60 127\n";
        std::unique_ptr<AdjmmMap> m(load(write("a.adjmm", t)));
        QVERIFY(m);
        QCOMPARE(int(m->named.size()), 15);
        QCOMPARE(m->named.at("strip8_fader").channel, uint8_t(ADJMM_ANY_CHANNEL));
        QCOMPARE(m->named.at("track_back").channel, uint8_t(2));
        QCOMPARE(m->named.at("track_back").controller, uint8_t(ADJMM_NOTEON));
        QCOMPARE(m->named.at("track_back").value, uint8_t(40));
    }

    void mixedFileKeepsTransportOps()
    {
        std::unique_ptr<AdjmmMap> m(load(write("m.adjmm",
            "start 0 80 127\nstrip1_mute 0 48 127\nfoo_bar 0 1 1\nstop 1 63 127\nstrip9_mute 0 1 127\n")));
        QCOMPARE(m->ops[ADJMM_OP_START - 1].controller, uint8_t(80));
        QCOMPARE(m->ops[ADJMM_OP_STOP - 1].channel, uint8_t(1));
        QCOMPARE(int(m->named.size()), 1);                       // unknown name and strip 9 skipped
        QVERIFY(m->named.count("strip1_mute"));
    }

    void directivesAndComments()
    {
        std::unique_ptr<AdjmmMap> m(load(write("d.adjmm",
            "# plain comment\n#! source nanoKONTROL\n#! pickup 0\n#!rec_is_forward 0\n# strip1_mute 0 1 127\n")));
        QCOMPARE(m->directives.value("source"), QString("nanoKONTROL"));
        QCOMPARE(m->directives.value("pickup"), QString("0"));
        QCOMPARE(m->directives.value("rec_is_forward"), QString("0"));
        QCOMPARE(int(m->directives.size()), 3);
        QVERIFY(m->named.empty());
        const DjtoolzCtlMap c = DjtoolzCtlMap::fromAdjmm(*m);
        QCOMPARE(c.source(), QString("nanoKONTROL"));
        QVERIFY(!c.pickup());
        QVERIFY(!c.recIsForward());
    }

    void defaults()
    {
        AdjmmMap m;
        const DjtoolzCtlMap c = DjtoolzCtlMap::fromAdjmm(m);
        QVERIFY(c.source().isEmpty());
        QVERIFY(c.pickup());
        QVERIFY(c.recIsForward());
    }

    void decodeControls()
    {
        std::unique_ptr<AdjmmMap> m(load(write("c.adjmm",
            "strip1_mute 1 48 127\nstrip2_solo 0 33 127\nstrip3_rec 0 66 127\nstrip4_select 0 99 127\n"
            "strip5_knob 0 20 *\nstrip6_fader 2 5 *\ntrack_back 0 58 127\ntrack_fwd 0 59 127\nmode_toggle 0 60 127\n")));
        const DjtoolzCtlMap c = DjtoolzCtlMap::fromAdjmm(*m);
        auto a = c.decodeControl(1, 48, 127);
        QCOMPARE(a.kind, ControlAction::Strip); QCOMPARE(a.control, Control::Mute); QCOMPARE(a.strip, 1);
        QCOMPARE(c.decodeControl(1, 48, 0).kind, ControlAction::None);        // release
        QCOMPARE(c.decodeControl(2, 48, 127).kind, ControlAction::None);      // wrong channel
        QCOMPARE(c.decodeControl(1, 49, 127).kind, ControlAction::None);      // wrong controller
        a = c.decodeControl(9, 33, 127);
        QCOMPARE(a.control, Control::Solo); QCOMPARE(a.strip, 2);
        QCOMPARE(c.decodeControl(1, 66, 127).control, Control::Rec);
        QCOMPARE(c.decodeControl(1, 99, 127).control, Control::Select);
        a = c.decodeControl(3, 20, 55);
        QCOMPARE(a.control, Control::Knob); QCOMPARE(a.strip, 5); QCOMPARE(a.value, 55);
        QCOMPARE(c.decodeControl(3, 20, 0).kind, ControlAction::Strip);       // analog: any value
        a = c.decodeControl(2, 5, 100);
        QCOMPARE(a.control, Control::Fader); QCOMPARE(a.strip, 6); QCOMPARE(a.value, 100);
        QCOMPARE(c.decodeControl(1, 5, 100).kind, ControlAction::None);
        QCOMPARE(c.decodeControl(1, 58, 127).kind, ControlAction::TrackBack);
        QCOMPARE(c.decodeControl(1, 59, 127).kind, ControlAction::TrackForward);
        QCOMPARE(c.decodeControl(1, 60, 127).kind, ControlAction::ModeToggle);
        QCOMPARE(c.opCount(), 9);
    }

    void buttonWithAnyValueNeedsHighHalf()
    {
        std::unique_ptr<AdjmmMap> m(load(write("b.adjmm", "strip1_mute 0 48 *\n")));
        const DjtoolzCtlMap c = DjtoolzCtlMap::fromAdjmm(*m);
        QCOMPARE(c.decodeControl(1, 48, 127).kind, ControlAction::Strip);
        QCOMPARE(c.decodeControl(1, 48, 0).kind, ControlAction::None);
    }

    void decodeNotes()
    {
        std::unique_ptr<AdjmmMap> m(load(write("n.adjmm", "strip1_mute 0 N 40\nstrip1_fader 0 N 41\nstrip2_mute 3 N 42\n")));
        const DjtoolzCtlMap c = DjtoolzCtlMap::fromAdjmm(*m);
        QCOMPARE(c.decodeNote(1, 40, 100, true).strip, 1);
        QCOMPARE(c.decodeNote(1, 40, 0, true).kind, ControlAction::None);     // velocity 0 = off
        QCOMPARE(c.decodeNote(1, 40, 100, false).kind, ControlAction::None);
        QCOMPARE(c.decodeNote(1, 41, 100, true).kind, ControlAction::None);   // analog ops need a CC
        QCOMPARE(c.decodeNote(1, 42, 100, true).kind, ControlAction::None);   // channel 3 only
        QCOMPARE(c.decodeNote(3, 42, 100, true).strip, 2);
        QCOMPARE(c.decodeControl(1, 40, 127).kind, ControlAction::None);      // a note op is not a CC op
    }

    void firstMatchWins()
    {
        std::unique_ptr<AdjmmMap> m(load(write("f.adjmm", "strip2_mute 0 48 127\nstrip1_mute 0 48 127\n")));
        const DjtoolzCtlMap c = DjtoolzCtlMap::fromAdjmm(*m);
        QCOMPARE(c.decodeControl(1, 48, 127).strip, 1);                       // deterministic: name order
    }

    void writerReplacesAndKeeps()
    {
        const QByteArray src = "# my map\nstart 0 80 127\n\nstrip1_mute 0 1 127   # old\nstop 0 63 127\n#! pickup 1\n";
        const QString p = write("w.adjmm", src);
        AdjmmOp op; op.channel = 0; op.controller = 48; op.value = 127;
        AdjmmOp fad; fad.channel = 2; fad.controller = 7; fad.value = ADJMM_ANY_VALUE;
        QString err;
        QVERIFY2(writeDjtoolzOps(p, { { "strip1_mute", op }, { "strip3_fader", fad } }, { { "pickup", "0" }, { "source", "nano" } }, &err), qPrintable(err));
        const QList<QByteArray> lines = slurp(p).split('\n');
        QCOMPARE(lines[0], QByteArray("# my map"));
        QCOMPARE(lines[1], QByteArray("start 0 80 127"));
        QCOMPARE(lines[2], QByteArray(""));
        QVERIFY(lines[3].startsWith("strip1_mute") && lines[3].contains("48") && !lines[3].contains("old"));
        QCOMPARE(lines[4], QByteArray("stop 0 63 127"));
        QCOMPARE(lines[5], QByteArray("#! pickup 0"));
        QVERIFY(slurp(p).contains("#! source nano\n"));
        QVERIFY(slurp(p).contains("strip3_fader"));

        std::unique_ptr<AdjmmMap> m(load(p));
        QCOMPARE(m->ops[ADJMM_OP_START - 1].controller, uint8_t(80));         // transport ops intact
        QCOMPARE(m->ops[ADJMM_OP_STOP - 1].controller, uint8_t(63));
        QCOMPARE(m->named.at("strip1_mute").controller, uint8_t(48));
        QCOMPARE(m->named.at("strip3_fader").value, uint8_t(ADJMM_ANY_VALUE));
        QCOMPARE(m->named.at("strip3_fader").channel, uint8_t(2));
        QCOMPARE(m->directives.value("pickup"), QString("0"));
    }

    void writerKeepsUnrelatedLinesByteForByte()
    {
        const QByteArray src = "start   0   80   127\r\n#  odd   spacing\nnudge_fwd\t0 61 127";   // no final newline
        const QString p = write("k.adjmm", src);
        AdjmmOp op; op.channel = 0; op.controller = 1; op.value = 127;
        QVERIFY(writeDjtoolzOps(p, { { "track_fwd", op } }, {}));
        const QByteArray out = slurp(p);
        QVERIFY(out.startsWith(src + "\n"));
        QVERIFY(out.contains("track_fwd"));
    }

    void writerCreatesFile()
    {
        const QString p = dir_.filePath("new.adjmm");
        QVERIFY(!QFile::exists(p));
        AdjmmOp op; op.channel = 5; op.controller = ADJMM_NOTEON; op.value = 36;
        QVERIFY(writeDjtoolzOps(p, { { "strip1_solo", op } }, { { "source", "nanoKONTROL" } }));
        QVERIFY(slurp(p).startsWith("# adj midi map"));
        std::unique_ptr<AdjmmMap> m(load(p));
        const AdjmmOp r = m->named.at("strip1_solo");
        QCOMPARE(r.channel, uint8_t(5)); QCOMPARE(r.controller, uint8_t(ADJMM_NOTEON)); QCOMPARE(r.value, uint8_t(36));
        QCOMPARE(DjtoolzCtlMap::fromAdjmm(*m).source(), QString("nanoKONTROL"));
        // writing twice does not duplicate
        QVERIFY(writeDjtoolzOps(p, { { "strip1_solo", op } }, { { "source", "nanoKONTROL" } }));
        QCOMPARE(slurp(p).count("strip1_solo"), 1);
        QCOMPARE(slurp(p).count("#! source"), 1);
    }

    void examplesFileParses()
    {
        std::unique_ptr<AdjmmMap> m(load(QStringLiteral(VDJ_DOC_DIR "/nanokontrol-studio-djtoolz.adjmm")));
        QVERIFY(m);
        const DjtoolzCtlMap c = DjtoolzCtlMap::fromAdjmm(*m);
        QCOMPARE(c.source(), QString("nanoKONTROL"));
        QCOMPARE(c.decodeControl(1, 0, 10).strip, 1);
        QCOMPARE(c.decodeControl(1, 55, 127).strip, 8);
        QCOMPARE(c.decodeControl(1, 58, 127).kind, ControlAction::TrackBack);
        QCOMPARE(c.opCount(), 8 * 5 + 2);                                      // select + mode_toggle left commented out
    }

    void adjSystemFilesStillParse()
    {
        for (const char* f : { "/etc/adj/xdj700.adjmm", "/etc/adj/korg-nanokontrol.adjmm" }) {
            if (!QFile::exists(f)) continue;
            std::unique_ptr<AdjmmMap> m(load(f));
            QVERIFY(m);
            QVERIFY(m->named.empty());
            QVERIFY(m->ops[ADJMM_OP_START - 1].controller != ADJMM_UNSET);
        }
    }

    void maths()
    {
        QCOMPARE(offsetMsFromValue(0), -40.0);
        QCOMPARE(offsetMsFromValue(63), 0.0);
        QCOMPARE(offsetMsFromValue(64), 0.0);
        QCOMPARE(offsetMsFromValue(127), 40.0);
        QCOMPARE(volumeFromValue(-3), 0);
        QCOMPARE(volumeFromValue(200), 127);
        for (int ms = -40; ms <= 40; ++ms)
            QVERIFY(std::abs(offsetMsFromValue(valueFromOffsetMs(ms)) - ms) <= 1.0);
        QCOMPARE(valueFromOffsetMs(-40), 0);
        QCOMPARE(valueFromOffsetMs(40), 127);
    }

    void pickupSwallowsUntilReached()
    {
        Pickup p;
        int out = -1;
        QVERIFY(!p.filter(0, 100, &out));
        QVERIFY(!p.filter(50, 100, &out));
        QVERIFY(p.filter(99, 100, &out));                 // within 2
        QCOMPARE(out, 99);
        QVERIFY(p.filter(10, 100, &out));                 // now follows
        QCOMPARE(out, 10);
        p.reset();
        QVERIFY(!p.filter(10, 100, &out));                // re-armed
    }

    void pickupCatchesOnCrossing()
    {
        Pickup p;
        int out = -1;
        QVERIFY(!p.filter(10, 60, &out));
        QVERIFY(p.filter(120, 60, &out));                 // jumped over 60
        QCOMPARE(out, 120);
        p.reset();
        QVERIFY(!p.filter(120, 60, &out));
        QVERIFY(!p.filter(90, 60, &out));
        QVERIFY(p.filter(30, 60, &out));                  // downward crossing
    }

    void pickupDisabledPasses()
    {
        Pickup p;
        p.setEnabled(false);
        int out = -1;
        QVERIFY(p.filter(0, 100, &out));
        QCOMPARE(out, 0);
        p.setEnabled(true);
        QVERIFY(!p.filter(0, 100, &out));
    }
};

QTEST_MAIN(TestDjtoolzCtlMap)
#include "test_djtoolz_ctl_map.moc"
