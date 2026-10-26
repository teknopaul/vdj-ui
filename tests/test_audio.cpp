#include <QtTest/QtTest>
#include <QApplication>
#include <QPushButton>
#include <QTemporaryDir>
#include <cmath>
#include "audio/audio_decoder.h"
#include "audio/audio_output.h"
#include "audio/audio_player.h"
#include "audio/time_stretcher.h"
#include "nfs/xdj_nfs.h"
#include "midi/alsa_clock.h"
#include <alsa/asoundlib.h>
#include "ui/player_panel.h"
#include "ui/waveform_widget.h"

using namespace vdj;

class TestAudio : public QObject
{
    Q_OBJECT

private slots:
    void decodeWav();
    void decodeMissingFile();
    void stretcherUnity();
    void stretcherTempo_data();
    void stretcherTempo();
    void stretcherKeepsPitch();
    void playerLoadSeek();
    void splitArithmetic();
    void jumpScheduling();
    void deviceList();
    void playbackRuns();
    void panelCuesAndBpm();
    void waveformClickSeeks();
    void deckUnreachable();
    void alsaClockTempo();
    void alsaClockEmitsMidiClock();
    void alsaClockRestartAndBar();

private:
    static QString writeWav(const QString& dir, int seconds, double freq)
    {
        const int rate = 44100, frames = rate * seconds;
        QByteArray pcm(frames * 4, 0);
        auto* s = reinterpret_cast<qint16*>(pcm.data());
        for (int i = 0; i < frames; ++i) {
            const qint16 v = qint16(12000 * std::sin(2 * M_PI * freq * i / rate));
            s[2 * i] = s[2 * i + 1] = v;
        }
        QByteArray h("RIFF");
        auto le32 = [&](quint32 v) { for (int i = 0; i < 4; ++i) h.append(char((v >> (8 * i)) & 0xff)); };
        auto le16 = [&](quint16 v) { h.append(char(v & 0xff)); h.append(char(v >> 8)); };
        le32(36 + pcm.size()); h.append("WAVEfmt "); le32(16); le16(1); le16(2); le32(rate); le32(rate * 4); le16(4); le16(16);
        h.append("data"); le32(pcm.size());
        const QString path = dir + "/tone.wav";
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(h);
        f.write(pcm);
        return path;
    }

    // zero crossings per second of the left channel
    static double frequency(const QVector<qint16>& left, int rate)
    {
        int crossings = 0;
        for (int i = 1; i < left.size(); ++i)
            if (left[i - 1] < 0 && left[i] >= 0) ++crossings;
        return crossings * double(rate) / left.size();
    }
};

void TestAudio::decodeWav()
{
    QTemporaryDir dir;
    const DecodedAudio a = decodeAudioFile(writeWav(dir.path(), 2, 440.0));
    QVERIFY(!a.isEmpty());
    QVERIFY(std::abs(a.lengthMs() - 2000.0) < 20.0);
    QCOMPARE(a.samples.size() % 2, 0);
}

void TestAudio::decodeMissingFile()
{
    QString err;
    QVERIFY(decodeAudioFile("/nonexistent/none.mp3", &err).isEmpty());
    QVERIFY(!err.isEmpty());
}

void TestAudio::stretcherUnity()
{
    QVector<qint16> src(2 * 5000);
    for (int i = 0; i < src.size(); ++i) src[i] = qint16(i % 1000);
    TimeStretcher t;
    t.setSource(src.constData(), 5000);
    QVector<qint16> out(2 * 3000);
    t.render(out.data(), 3000);
    QVERIFY(std::equal(out.begin(), out.end(), src.begin()));
    QCOMPARE(t.position(), 3000.0);
    t.seek(4000);
    t.render(out.data(), 2000);                  // runs off the end: the rest is silence
    QCOMPARE(out[2 * 1500], qint16(0));
    QVERIFY(t.atEnd());
}

void TestAudio::stretcherTempo_data()
{
    QTest::addColumn<double>("tempo");
    QTest::newRow("slow") << 0.8;
    QTest::newRow("fast") << 1.25;
    QTest::newRow("double") << 2.0;
}

void TestAudio::stretcherTempo()
{
    QFETCH(double, tempo);
    QTemporaryDir dir;
    const DecodedAudio a = decodeAudioFile(writeWav(dir.path(), 6, 440.0));
    TimeStretcher t;
    t.setSource(a.samples.constData(), a.frames());
    t.setTempo(tempo);
    QVector<qint16> out(2 * 44100);
    t.render(out.data(), 44100);                 // one second of output
    // one second out consumes `tempo` seconds of input
    QVERIFY2(std::abs(t.position() - tempo * 44100) < 3000, qPrintable(QString::number(t.position())));
    QVERIFY(std::abs(out[2 * 40000]) > 0 || std::abs(out[2 * 40001]) > 0);
}

void TestAudio::stretcherKeepsPitch()
{
    QTemporaryDir dir;
    const DecodedAudio a = decodeAudioFile(writeWav(dir.path(), 8, 440.0));
    TimeStretcher t;
    t.setSource(a.samples.constData(), a.frames());
    t.setTempo(1.5);
    QVector<qint16> out(2 * 44100 * 2), left;
    t.render(out.data(), 44100 * 2);
    for (int i = 4410; i < 44100 * 2; ++i) left.append(out[2 * i]);   // skip the fade in
    const double f = frequency(left, 44100);
    QVERIFY2(std::abs(f - 440.0) < 15.0, qPrintable(QString::number(f)));
}

void TestAudio::playerLoadSeek()
{
    QTemporaryDir dir;
    AudioPlayer p;
    QVERIFY(!p.isLoaded());
    QString err;
    QVERIFY(!p.load("/nonexistent.wav", &err));
    QVERIFY(p.load(writeWav(dir.path(), 3, 220.0), &err));
    QVERIFY(p.isLoaded());
    QCOMPARE(p.state(), AudioPlayer::Stopped);
    p.seekMs(1500);
    QVERIFY(std::abs(p.positionMs() - 1500) < 1);
    p.setTempo(1.2);
    QVERIFY(std::abs(p.tempo() - 1.2) < 1e-9);
    p.stop();
    QCOMPARE(p.positionMs(), 0.0);
    p.unload();
    QVERIFY(!p.isLoaded());
}

void TestAudio::splitArithmetic()
{
    // trigger inside the chunk
    QCOMPARE(splitAtTrigger(1000, 1.0, 1300, 1024), 300);
    // beyond the chunk: nothing yet
    QCOMPARE(splitAtTrigger(1000, 1.0, 2024, 1024), -1);
    QCOMPARE(splitAtTrigger(1000, 1.0, 5000, 1024), -1);
    // already passed or exactly now: jump first
    QCOMPARE(splitAtTrigger(1000, 1.0, 1000, 1024), 0);
    QCOMPARE(splitAtTrigger(1000, 1.0, 900, 1024), 0);
    // tempo 0.5: 300 source frames take 600 output frames; tempo 1.5: 150
    QCOMPARE(splitAtTrigger(1000, 0.5, 1300, 1024), 600);
    QCOMPARE(splitAtTrigger(1000, 1.5, 1300, 1024), 200);
    QCOMPARE(splitAtTrigger(1000, 0.5, 1600, 1024), -1);
    QCOMPARE(splitAtTrigger(1000, 0.0, 1300, 1024), -1);
}

void TestAudio::jumpScheduling()
{
    QTemporaryDir dir;
    AudioPlayer p;
    QVERIFY(p.load(writeWav(dir.path(), 3, 220.0)));
    // not playing: a request seeks at once and leaves nothing pending
    p.seekAtMs(1000, 2000);
    QVERIFY(!p.hasPendingJump());
    QVERIFY(std::abs(p.positionMs() - 2000) < 1);

    QSignalSpy errors(&p, &AudioPlayer::deviceError);
    QSignalSpy jumped(&p, &AudioPlayer::jumped);
    p.seekMs(0);
    p.play();
    QTest::qWait(300);
    if (errors.count())
        QSKIP("no audio output available");
    // trigger already passed: immediate
    p.seekAtMs(0, 1500);
    QVERIFY(!p.hasPendingJump());
    // pending, then cancelled by seek, pause and tempo change
    p.seekAtMs(p.positionMs() + 60000, 100);
    QVERIFY(p.hasPendingJump());
    p.seekMs(500);
    QVERIFY(!p.hasPendingJump());
    p.seekAtMs(p.positionMs() + 60000, 100);
    p.setTempo(1.1);
    QVERIFY(!p.hasPendingJump());
    p.seekAtMs(p.positionMs() + 60000, 100);
    p.pause();
    QVERIFY(!p.hasPendingJump());
    p.play();
    QTest::qWait(100);
    // a trigger ~150 ms ahead is executed by the worker and reported
    p.seekAtMs(p.positionMs() + 150, 100);
    QTest::qWait(500);
    QCOMPARE(jumped.count(), 1);
    QVERIFY(!p.hasPendingJump());
    QVERIFY2(p.positionMs() < 1000.0, qPrintable(QString::number(p.positionMs())));
    p.seekAtMs(p.positionMs() + 60000, 100);
    p.stop();
    QVERIFY(!p.hasPendingJump());
}

void TestAudio::deviceList()
{
    const auto devices = listAudioDevices();
    QVERIFY(!devices.isEmpty());
    QVERIFY(devices.first().name.isEmpty());     // the default comes first
}

void TestAudio::playbackRuns()
{
    QTemporaryDir dir;
    AudioPlayer p;
    QVERIFY(p.load(writeWav(dir.path(), 2, 440.0)));
    p.setTempo(1.25);
    QSignalSpy errors(&p, &AudioPlayer::deviceError);
    p.play();
    QTest::qWait(600);
    if (errors.count())
        QSKIP("no audio output available");
    QCOMPARE(p.state(), AudioPlayer::Playing);
    QVERIFY2(p.positionMs() > 200.0, qPrintable(QString::number(p.positionMs())));
    p.pause();
    const double at = p.positionMs();
    QTest::qWait(150);
    QCOMPARE(p.positionMs(), at);
    QCOMPARE(p.state(), AudioPlayer::Paused);
    p.stop();
    QCOMPARE(p.state(), AudioPlayer::Stopped);
    QCOMPARE(p.positionMs(), 0.0);
}

void TestAudio::panelCuesAndBpm()
{
    QTemporaryDir dir;
    PlayerPanel panel;
    QVERIFY(!panel.playButton()->isEnabled());
    TrackInfo info;
    info.title = "Tone";
    info.audioPath = writeWav(dir.path(), 3, 330.0);
    info.fields.append(qMakePair(QStringLiteral("BPM"), QStringLiteral("120.00")));
    QVERIFY(panel.loadTrack(info));
    QVERIFY(panel.playButton()->isEnabled());
    QCOMPARE(panel.originalBpm(), 120.0);
    // back / forward are pure BPM jumps in source time: one beat = 500 ms at 120 BPM, a bar = 2000 ms
    QVERIFY(panel.forwardButton()->isEnabled());
    panel.forwardButton()->click();
    QVERIFY(std::abs(panel.player()->positionMs() - 2000) < 1);
    panel.player()->seekMs(2500);
    panel.backButton()->click();
    QVERIFY(std::abs(panel.player()->positionMs() - 500) < 1);
    panel.backButton()->click();
    QVERIFY(std::abs(panel.player()->positionMs()) < 1);     // clamped at the start
    panel.player()->seekMs(0);
    panel.bpmUpButton()->click();
    QVERIFY(std::abs(panel.targetBpm() - 120.1) < 1e-6);
    QVERIFY(std::abs(panel.player()->tempo() - 120.1 / 120.0) < 1e-6);
    panel.bpmDownButton()->click();
    panel.bpmDownButton()->click();
    QVERIFY(std::abs(panel.targetBpm() - 119.9) < 1e-6);

    // cue buttons are built from the analysis; inject one through the same path a loaded track uses
    QVERIFY(panel.cueButtons().isEmpty());       // no analysis for this file
    TrackInfo bad;
    bad.title = "Missing";
    bad.audioPath = "/nonexistent.mp3";
    QVERIFY(!panel.loadTrack(bad));
    QVERIFY(!panel.playButton()->isEnabled());
}

void TestAudio::waveformClickSeeks()
{
    WaveformWidget w;
    TrackAnalysis a;
    a.valid = true;
    a.color = QVector<quint16>(600, 0x1ff);   // 4 s at 150 entries per second
    w.setAnalysis(a);
    w.resize(400, 80);
    w.setSeekable(true);
    QSignalSpy spy(&w, &WaveformWidget::seekRequested);
    QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, QPoint(200, 40));
    QCOMPARE(spy.count(), 1);
    const double ms = spy.first().first().toDouble();
    QVERIFY(std::abs(ms - w.analysis().lengthMs() / 2.0) < w.analysis().lengthMs() / 100.0);
    w.setPlayPosition(1000.0);
    QCOMPARE(w.playPosition(), 1000.0);
}

void TestAudio::deckUnreachable()
{
    XdjNfs nfs(QStringLiteral("127.0.0.1"));      // nothing serves NFS here
    QString err;
    QVERIFY(!nfs.connect(&err));
    QVERIFY(!err.isEmpty());
    QVERIFY(!nfs.mountExport(0, &err));           // not connected: fails cleanly
    QCOMPARE(nfs.fileSize("PIONEER/rekordbox/export.pdb"), qint64(-1));
}

void TestAudio::alsaClockTempo()
{
    AlsaClock c;
    QString err;
    if (!c.open(QStringLiteral("vdj-ui-test"), &err))
        QSKIP("no ALSA sequencer available");
    QVERIFY(c.setBpm(148.82));
    QVERIFY(std::abs(c.queueBpm() - 148.82) < 0.01);   // what the sequencer queue really runs at
    QVERIFY(!c.setBpm(0.0));
    QVERIFY(!c.setBpm(1000.0));
    QVERIFY(std::abs(c.queueBpm() - 148.82) < 0.01);   // rejected values leave the tempo alone
}

void TestAudio::alsaClockEmitsMidiClock()
{
    AlsaClock c;
    if (!c.open(QStringLiteral("vdj-ui-test"), nullptr))
        QSKIP("no ALSA sequencer available");
    QVERIFY(c.setBpm(240.0));                          // 4 beats a second: 96 clock pulses a second

    snd_seq_t* rx = nullptr;
    QVERIFY(snd_seq_open(&rx, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) == 0);
    snd_seq_set_client_name(rx, "vdj-ui-test-rx");
    const int rxPort = snd_seq_create_simple_port(rx, "in", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                                  SND_SEQ_PORT_TYPE_APPLICATION);
    QVERIFY(snd_seq_connect_from(rx, rxPort, c.clientId(), c.portId()) == 0);

    c.start();
    QVERIFY(c.running());
    int clocks = 0, starts = 0, stops = 0;
    auto drain = [&] {
        snd_seq_event_t* ev = nullptr;
        while (snd_seq_event_input(rx, &ev) >= 0 && ev) {
            if (ev->type == SND_SEQ_EVENT_CLOCK) ++clocks;
            else if (ev->type == SND_SEQ_EVENT_START) ++starts;
            else if (ev->type == SND_SEQ_EVENT_STOP) ++stops;
        }
    };
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 1000) {
        QTest::qWait(20);
        drain();
    }
    c.stop();
    QTest::qWait(50);
    drain();
    snd_seq_close(rx);
    QCOMPARE(starts, 1);
    QCOMPARE(stops, 1);
    QVERIFY2(clocks > 80 && clocks < 130, qPrintable(QString::number(clocks)));   // ~96 in a second
    QVERIFY(!c.running());
}

void TestAudio::alsaClockRestartAndBar()
{
    AlsaClock c;
    if (!c.open(QStringLiteral("vdj-ui-test"), nullptr))
        QSKIP("no ALSA sequencer available");
    QVERIFY(c.setBpm(240.0));
    QCOMPARE(c.barPos(), 0);                           // stopped
    c.shiftBeats(1);                                   // ignored while stopped

    snd_seq_t* rx = nullptr;
    QVERIFY(snd_seq_open(&rx, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) == 0);
    snd_seq_set_client_name(rx, "vdj-ui-test-rx2");
    const int rxPort = snd_seq_create_simple_port(rx, "in", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                                  SND_SEQ_PORT_TYPE_APPLICATION);
    QVERIFY(snd_seq_connect_from(rx, rxPort, c.clientId(), c.portId()) == 0);
    QList<int> events;                                 // START / STOP in the order received
    int clocksAfterRestart = 0;
    bool restarted = false;
    auto drain = [&] {
        snd_seq_event_t* ev = nullptr;
        while (snd_seq_event_input(rx, &ev) >= 0 && ev) {
            if (ev->type == SND_SEQ_EVENT_START || ev->type == SND_SEQ_EVENT_STOP)
                events << int(ev->type);
            else if (ev->type == SND_SEQ_EVENT_CLOCK && restarted)
                ++clocksAfterRestart;
        }
    };

    c.restart();                                       // not running: nothing
    QVERIFY(!c.running());
    c.start();
    QVERIFY(c.running());
    QTest::qWait(1300);                                // 240 BPM: more than 4 beats
    QVERIFY2(c.positionBeats() > 4.0, qPrintable(QString::number(c.positionBeats())));
    const int before = c.barPos();
    QVERIFY(before >= 1 && before <= 4);
    c.shiftBeats(1);
    QCOMPARE(c.barPos(), before % 4 + 1);
    c.shiftBeats(-1);
    QCOMPARE(c.barPos(), before);

    drain();
    restarted = true;
    c.shiftBeats(2);
    c.restart();
    QVERIFY(c.running());                              // the clock keeps running
    QVERIFY2(c.positionBeats() >= 0.0 && c.positionBeats() < 0.5, qPrintable(QString::number(c.positionBeats())));
    QCOMPARE(c.barPos(), 1);                           // restart resets the offset: tick 0 is beat 1
    QTest::qWait(600);
    drain();
    QVERIFY2(clocksAfterRestart > 30, qPrintable(QString::number(clocksAfterRestart)));   // pulses restarted from tick 0
    QVERIFY2(c.positionBeats() > 1.0 && c.positionBeats() < 3.5, qPrintable(QString::number(c.positionBeats())));
    c.stop();
    QCOMPARE(c.barPos(), 0);
    QTest::qWait(50);
    drain();
    snd_seq_close(rx);
    // start, then stop + start from the restart, then the final stop
    QCOMPARE(events, (QList<int>{ SND_SEQ_EVENT_START, SND_SEQ_EVENT_STOP, SND_SEQ_EVENT_START, SND_SEQ_EVENT_STOP }));
}

QTEST_MAIN(TestAudio)
#include "test_audio.moc"
