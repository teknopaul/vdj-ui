#include <QtTest>
#include <QTemporaryFile>
#include <alsa/asoundlib.h>
#include "midi/midi_map_input.h"

using namespace vdj;

// Sends real events through the ALSA sequencer to MidiMapInput's port, as a controller would.
class TestMidiMapInput : public QObject {
    Q_OBJECT

    snd_seq_t* seq_ = nullptr;
    int port_ = -1;

    void send(snd_seq_event_t& ev)
    {
        snd_seq_ev_set_source(&ev, port_);
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_direct(&ev);
        snd_seq_event_output(seq_, &ev);
        snd_seq_drain_output(seq_);
    }

    void noteOn(int ch, int note, int vel)
    {
        snd_seq_event_t ev;
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_noteon(&ev, ch - 1, note, vel);
        send(ev);
    }

    void cc(int ch, int param, int value)
    {
        snd_seq_event_t ev;
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_controller(&ev, ch - 1, param, value);
        send(ev);
    }

private slots:
    void initTestCase()
    {
        if (snd_seq_open(&seq_, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0)
            QSKIP("no ALSA sequencer available");
        port_ = snd_seq_create_simple_port(seq_, "test controller",
            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_MIDI_GENERIC);
    }

    void cleanupTestCase() { if (seq_) snd_seq_close(seq_); }

    void controllerEventsReachTheMap()
    {
        QTemporaryFile map;
        QVERIFY(map.open());
        map.write("start 1 N 0\nstop 1 N 1\nnudge_fwd 1 48 +\nnudge_frew 1 48 --\ncopy_master 1 N 16\n");
        map.flush();

        MidiMapInput in;
        QVERIFY(in.start(map.fileName()));
        QCOMPARE(in.mappedOps(), 5);
        in.setMonitoring(true);

        snd_seq_connect_to(seq_, port_, in.clientId(), in.portId());
        QCOMPARE(in.sourceNames().size(), 1);

        QSignalSpy start(&in, &MidiMapInput::startRequested);
        QSignalSpy stop(&in, &MidiMapInput::stopRequested);
        QSignalSpy nudge(&in, &MidiMapInput::nudgeRequested);
        QSignalSpy master(&in, &MidiMapInput::copyMasterRequested);
        QSignalSpy seen(&in, &MidiMapInput::midiEvent);

        noteOn(1, 0, 127);
        noteOn(1, 1, 127);
        cc(1, 48, 65);                      // wheel +
        cc(1, 48, 10);                      // wheel --
        noteOn(1, 16, 127);
        noteOn(1, 99, 127);                 // not in the map
        QTRY_VERIFY(seen.count() >= 6);
        // ALSA also announces the new subscription to the port: not a controller event
        for (int i = seen.count() - 1; i >= 0; --i)
            if (seen[i][0].toString().startsWith("ALSA event")) seen.removeAt(i);
        QCOMPARE(seen.count(), 6);

        QCOMPARE(start.count(), 1);
        QCOMPARE(stop.count(), 1);
        QCOMPARE(master.count(), 1);
        QCOMPARE(nudge.count(), 2);
        QCOMPARE(nudge[0][0].toInt(), 10);
        QCOMPARE(nudge[1][0].toInt(), -20);
        QCOMPARE(seen[0][1].toInt(), ADJMM_OP_START);
        QCOMPARE(seen[5][1].toInt(), 0);
        QVERIFY(seen[5][0].toString().contains("note 99"));
        in.stop();
    }

    void rawControlAndNoteEventsWithoutAMap()
    {
        MidiMapInput in;
        QVERIFY(in.start(QString()));                // an empty map still opens the port
        QCOMPARE(in.mappedOps(), 0);
        snd_seq_connect_to(seq_, port_, in.clientId(), in.portId());
        QSignalSpy ccs(&in, &MidiMapInput::controlEvent);
        QSignalSpy notes(&in, &MidiMapInput::noteEvent);
        cc(3, 21, 99);
        noteOn(2, 40, 100);
        QTRY_COMPARE(ccs.count(), 1);
        QTRY_COMPARE(notes.count(), 1);
        QCOMPARE(ccs[0][0].toInt(), 3);
        QCOMPARE(ccs[0][1].toInt(), 21);
        QCOMPARE(ccs[0][2].toInt(), 99);
        QVERIFY(!ccs[0][3].toString().isEmpty());     // the sender's ALSA client name
        QCOMPARE(notes[0][0].toInt(), 2);
        QCOMPARE(notes[0][1].toInt(), 40);
        QCOMPARE(notes[0][2].toInt(), 100);
        QCOMPARE(notes[0][3].toBool(), true);
        in.stop();
    }

    void nothingIsReportedUnlessMonitoring()
    {
        QTemporaryFile map;
        QVERIFY(map.open());
        map.write("start 1 N 0\n");
        map.flush();
        MidiMapInput in;
        QVERIFY(in.start(map.fileName()));
        snd_seq_connect_to(seq_, port_, in.clientId(), in.portId());
        QSignalSpy start(&in, &MidiMapInput::startRequested);
        QSignalSpy seen(&in, &MidiMapInput::midiEvent);
        noteOn(1, 0, 127);
        QTRY_COMPARE(start.count(), 1);
        QCOMPARE(seen.count(), 0);
        in.stop();
    }
};

QTEST_MAIN(TestMidiMapInput)
#include "test_midi_map_input.moc"
