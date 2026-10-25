#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <cmath>
#include <cstring>
#include "audio/beat_source.h"
#include "audio/sample_cache.h"
#include "audio/wav_info.h"

using namespace vdj;

class TestSamplerBasics : public QObject
{
    Q_OBJECT

private slots:
    void bpmFromNames_data();
    void bpmFromNames();
    void acidChunkTempo();
    void noTempoInPlainWav();
    void stepSampleWalks();
    void stepSampleEmptyAndSingle();
    void stepSampleMissingFile();
    void cacheHitAndEviction();
    void cacheMissingFile();
    void fakeBeatSourceRoundTrip();

private:
    // 16 bit stereo 44.1 kHz silence, optionally with an "acid" chunk carrying `acidBpm`
    static QString writeWav(const QString& dir, const QString& name, int frames, double acidBpm = 0.0)
    {
        QByteArray pcm(frames * 4, 0);
        QByteArray h("RIFF");
        auto le32 = [&](quint32 v) { for (int i = 0; i < 4; ++i) h.append(char((v >> (8 * i)) & 0xff)); };
        auto le16 = [&](quint16 v) { h.append(char(v & 0xff)); h.append(char(v >> 8)); };
        QByteArray acid;
        if (acidBpm > 0) {
            QByteArray a;
            auto a32 = [&](quint32 v) { for (int i = 0; i < 4; ++i) a.append(char((v >> (8 * i)) & 0xff)); };
            auto a16 = [&](quint16 v) { a.append(char(v & 0xff)); a.append(char(v >> 8)); };
            a32(0); a16(60); a16(0x8000); a32(0); a32(4); a16(4); a16(4);
            const float t = float(acidBpm);
            quint32 bits;
            std::memcpy(&bits, &t, 4);
            a32(bits);
            acid = QByteArray("acid") + QByteArray(4, 0);
            acid[4] = char(a.size());
            acid += a;
        }
        le32(quint32(4 + 24 + acid.size() + 8 + pcm.size()));
        h.append("WAVEfmt "); le32(16); le16(1); le16(2); le32(44100); le32(44100 * 4); le16(4); le16(16);
        h.append(acid);
        h.append("data"); le32(quint32(pcm.size()));
        const QString path = dir + "/" + name;
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(h);
        f.write(pcm);
        return path;
    }
};

void TestSamplerBasics::bpmFromNames_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<double>("bpm");
    QTest::newRow("underscore") << "loop_128bpm_a.wav" << 128.0;
    QTest::newRow("space") << "Loop 93.5 BPM.wav" << 93.5;
    QTest::newRow("upper") << "x 100BPM.wav" << 100.0;
    QTest::newRow("none") << "kick.wav" << 0.0;
    QTest::newRow("too fast") << "x_999bpm.wav" << 0.0;
    QTest::newRow("one digit") << "x_7bpm.wav" << 0.0;
}

void TestSamplerBasics::bpmFromNames()
{
    QFETCH(QString, name);
    QFETCH(double, bpm);
    QCOMPARE(bpmFromFileName(name), bpm);
}

void TestSamplerBasics::acidChunkTempo()
{
    QTemporaryDir dir;
    // the acid tempo wins over the name
    const QString p = writeWav(dir.path(), "loop_90bpm.wav", 4410, 128.0);
    QVERIFY(qAbs(wavBpmFromFile(p) - 128.0) < 0.001);
    // the audio is still decodable behind the extra chunk
    SampleCache cache;
    auto s = cache.get(p);
    QVERIFY(s);
    QCOMPARE(s->audio.frames(), qint64(4410));
    QVERIFY(qAbs(s->bpm - 128.0) < 0.001);
}

void TestSamplerBasics::noTempoInPlainWav()
{
    QTemporaryDir dir;
    QCOMPARE(wavBpmFromFile(writeWav(dir.path(), "kick.wav", 100)), 0.0);
    QCOMPARE(wavBpmFromFile(writeWav(dir.path(), "kick_120bpm.wav", 100)), 120.0);
    QCOMPARE(wavBpmFromFile(dir.path() + "/missing.wav"), 0.0);
}

void TestSamplerBasics::stepSampleWalks()
{
    QTemporaryDir dir;
    const QString a = writeWav(dir.path(), "a.wav", 10), b = writeWav(dir.path(), "b.wav", 10), c = writeWav(dir.path(), "c.wav", 10);
    writeWav(dir.path(), "B.wav", 10);               // upper case sorts before lower case (ASCIIbetical)
    QFile(dir.path() + "/notes.txt").open(QIODevice::WriteOnly);
    QDir(dir.path()).mkdir("sub.wav");               // a folder is not a sample
    const QStringList all = wavsInFolder(dir.path());
    QCOMPARE(all.size(), 4);
    QCOMPARE(all.first(), dir.path() + "/B.wav");
    QCOMPARE(stepSample(a, +1), b);
    QCOMPARE(stepSample(b, +1), c);
    QCOMPARE(stepSample(c, +1), c);                  // clamps at the end
    QCOMPARE(stepSample(c, -1), b);
    QCOMPARE(stepSample(dir.path() + "/B.wav", -1), dir.path() + "/B.wav");   // and at the start
    QCOMPARE(stepSample(a, 0), a);
}

void TestSamplerBasics::stepSampleEmptyAndSingle()
{
    QTemporaryDir dir;
    QVERIFY(stepSample(dir.path() + "/x.wav", +1).isEmpty());
    const QString only = writeWav(dir.path(), "only.wav", 10);
    QCOMPARE(stepSample(only, +1), only);
    QCOMPARE(stepSample(only, -1), only);
}

void TestSamplerBasics::stepSampleMissingFile()
{
    QTemporaryDir dir;
    const QString a = writeWav(dir.path(), "a.wav", 10), c = writeWav(dir.path(), "c.wav", 10);
    const QString gone = dir.path() + "/b.wav";
    QCOMPARE(stepSample(gone, +1), c);
    QCOMPARE(stepSample(gone, -1), a);
}

void TestSamplerBasics::cacheHitAndEviction()
{
    QTemporaryDir dir;
    const QString a = writeWav(dir.path(), "a.wav", 1000), b = writeWav(dir.path(), "b.wav", 1000), c = writeWav(dir.path(), "c.wav", 1000);
    SampleCache cache(2 * 1000 * 4 + 100);           // room for two samples
    auto sa = cache.get(a);
    QVERIFY(sa);
    QVERIFY(cache.get(a).get() == sa.get());         // hit: the same object
    QVERIFY(cache.peek(a));
    QVERIFY(!cache.peek(b));
    cache.get(b);
    QCOMPARE(cache.count(), 2);
    cache.get(a);                                    // a is now the most recent
    cache.get(c);                                    // evicts b
    QCOMPARE(cache.count(), 2);
    QVERIFY(cache.peek(a));
    QVERIFY(!cache.peek(b));
    QVERIFY(cache.peek(c));
    QVERIFY(sa->audio.frames() == 1000);             // an evicted/held sample stays valid for its users
    cache.clear();
    QCOMPARE(cache.count(), 0);
    QCOMPARE(cache.bytes(), qint64(0));
}

void TestSamplerBasics::cacheMissingFile()
{
    SampleCache cache;
    QString err;
    QVERIFY(!cache.get("/nonexistent/x.wav", &err));
    QVERIFY(!err.isEmpty());
    QCOMPARE(cache.count(), 0);
}

void TestSamplerBasics::fakeBeatSourceRoundTrip()
{
    FakeBeatSource f;
    f.tempo = 120.0;
    f.startNs = 1000000000LL;
    QCOMPARE(f.beatsAtSteadyNs(f.startNs), 0.0);
    QVERIFY(qAbs(f.beatsAtSteadyNs(f.startNs + 500000000LL) - 1.0) < 1e-9);   // 120 BPM = 500 ms per beat
    QCOMPARE(f.steadyNsAtBeat(4.0), f.startNs + 2000000000LL);
    for (double b : { 0.0, 0.25, 3.0, 17.5 })
        QVERIFY(qAbs(f.beatsAtSteadyNs(f.steadyNsAtBeat(b)) - b) < 1e-6);
    f.tempo = 90.0;
    QVERIFY(qAbs(f.beatsAtSteadyNs(f.startNs + 2000000000LL) - 3.0) < 1e-9);
    f.isRunning = false;
    QCOMPARE(f.beatsAtSteadyNs(f.startNs), -1.0);
    QCOMPARE(f.bpm(), 0.0);
}

QTEST_MAIN(TestSamplerBasics)
#include "test_sampler_basics.moc"
