#include <QtTest/QtTest>

#include "trakcrate_wav.h"
#include "trakcrate/analysis.h"
#include "trakcrate/importer.h"

using namespace trakcrate;

class TestTrakcrateAnalysis : public QObject
{
    Q_OBJECT

private:
    fs::path tmp_;

    static double expectedFirst(double bpm, double t0)
    {
        double period = 60.0 / bpm;
        return std::fmod(t0, period);
    }

    void checkClicks(double bpm, double t0, double seconds)
    {
        BeatAnalysis a = analyseBeats(tcwav::clicks(bpm, t0, seconds));
        QVERIFY2(!a.grid.empty(), qPrintable(QString("no grid for %1 BPM").arg(bpm)));
        QVERIFY2(std::fabs(a.grid.bpm - bpm) < 0.05, qPrintable(QString("%1 BPM found as %2").arg(bpm).arg(a.grid.bpm)));
        double first = double(a.grid.firstBeatUs) / 1e6;
        double want = expectedFirst(bpm, t0);
        double diff = std::fabs(first - want);
        diff = std::min(diff, 60.0 / bpm - diff);                      // equal modulo one period
        QVERIFY2(diff < 0.010, qPrintable(QString("%1 BPM phase %2 s, wanted %3 s").arg(bpm).arg(first).arg(want)));
        QVERIFY(a.confidence > 0.8);
    }

private slots:
    void init() { tmp_ = makeTempDir("trakcrate-analysis"); QVERIFY(!tmp_.empty()); }
    void cleanup() { std::error_code ec; fs::remove_all(tmp_, ec); }

    void beatsOnClickTracks();
    void beatsOnDrumLoop();
    void beatsSlowNotDoubled();
    void beatsPriorPicksOctave();
    void beatsSilenceNoiseAndShort();
    void beatsAreDeterministic();
    void waveformHeightsAreNotNormalised();
    void waveformShapeAndColours();
    void waveformFileRoundTrip();
    void loudnessValues();
    void decodeResamplesAndDuplicatesMono();
    void decodeFailures();
    void analyseTrackEndToEnd();
    void analyseTrackRespectsOrigin();
    void importerCanAnalyse();
};

void TestTrakcrateAnalysis::beatsOnClickTracks()
{
    checkClicks(90, 0.137, 30);
    checkClicks(120, 0.5, 30);
    checkClicks(128, 0.250, 30);
    checkClicks(140, 0.031, 30);
    checkClicks(127.5, 0.2, 60);                                       // a fractional tempo
}

void TestTrakcrateAnalysis::beatsOnDrumLoop()
{
    BeatAnalysis a = analyseBeats(tcwav::drumLoop(124.0, 0.3, 40));
    QVERIFY(!a.grid.empty());
    QVERIFY2(std::fabs(a.grid.bpm - 124.0) < 0.1, qPrintable(QString::number(a.grid.bpm)));
    double first = double(a.grid.firstBeatUs) / 1e6, want = expectedFirst(124.0, 0.3);
    double diff = std::fabs(first - want);
    diff = std::min(diff, 60.0 / 124.0 - diff);
    QVERIFY2(diff < 0.015, qPrintable(QString::number(first)));
}

void TestTrakcrateAnalysis::beatsSlowNotDoubled()
{
    BeatAnalysis a = analyseBeats(tcwav::clicks(80, 0.1, 40));
    QVERIFY(!a.grid.empty());
    QVERIFY2(std::fabs(a.grid.bpm - 80.0) < 0.05, qPrintable(QString::number(a.grid.bpm)));
}

void TestTrakcrateAnalysis::beatsPriorPicksOctave()
{
    // beats at 70 BPM with a softer click on every off-beat: 70 or 140 both fit the clicks; the tag decides
    DecodedPcm p = tcwav::clicks(70, 0.2, 40, 0.8f);
    DecodedPcm off = tcwav::clicks(70, 0.2 + 60.0 / 70 / 2, 40, 0.5f);
    for (size_t i = 0; i < p.stereo.size(); ++i) p.stereo[i] += off.stereo[i];
    BeatAnalysis fast = analyseBeats(p, 140.0);
    QVERIFY(!fast.grid.empty());
    QVERIFY2(std::fabs(fast.grid.bpm - 140.0) < 0.1, qPrintable(QString::number(fast.grid.bpm)));
    BeatAnalysis slow = analyseBeats(p, 70.0);
    QVERIFY(!slow.grid.empty());
    QVERIFY2(std::fabs(slow.grid.bpm - 70.0) < 0.1, qPrintable(QString::number(slow.grid.bpm)));
}

void TestTrakcrateAnalysis::beatsSilenceNoiseAndShort()
{
    BeatAnalysis s = analyseBeats(tcwav::silence(20));
    QVERIFY(s.grid.empty());
    QCOMPARE(s.confidence, 0.0);
    BeatAnalysis n = analyseBeats(tcwav::noise(30));
    QVERIFY2(n.grid.empty(), qPrintable(QString("noise gave %1 BPM, confidence %2").arg(n.grid.bpm).arg(n.confidence)));
    QVERIFY(analyseBeats(tcwav::clicks(120, 0.1, 2.0)).grid.empty());       // too short to say anything
    QVERIFY(analyseBeats(DecodedPcm()).grid.empty());
    BeatAnalysis tone = analyseBeats(tcwav::sine(440, 0.5f, 20));            // a steady tone has no beats
    QVERIFY(tone.grid.empty());
}

void TestTrakcrateAnalysis::beatsAreDeterministic()
{
    DecodedPcm p = tcwav::drumLoop(128.0, 0.2, 30);
    BeatAnalysis a = analyseBeats(p), b = analyseBeats(p);
    QCOMPARE(a.grid.bpm, b.grid.bpm);
    QCOMPARE(a.grid.firstBeatUs, b.grid.firstBeatUs);
    QCOMPARE(a.confidence, b.confidence);
}

static double meanHeight(const Waveform& w, size_t from, size_t to)
{
    double s = 0;
    for (size_t i = from; i < to; ++i) s += w.pwv3[i] & 0x1F;
    return s / double(to - from);
}

void TestTrakcrateAnalysis::waveformHeightsAreNotNormalised()
{
    Waveform loud = analyseWaveform(tcwav::sine(220, 0.89f, 10));          // about -1 dBFS
    Waveform quiet = analyseWaveform(tcwav::sine(220, 0.1f, 10));          // -20 dBFS
    QCOMPARE(loud.columns(), size_t(1500));                                // 150 columns per second
    QVERIFY(meanHeight(loud, 100, 1400) > 24);
    QVERIFY2(meanHeight(quiet, 100, 1400) < 5, qPrintable(QString::number(meanHeight(quiet, 100, 1400))));
    QVERIFY(meanHeight(loud, 100, 1400) - meanHeight(quiet, 100, 1400) > 15);   // the quiet track stays visibly quiet
    // previews follow the same scale
    int loudMax = 0, quietMax = 0;
    for (uint8_t v : loud.pwav) loudMax = std::max(loudMax, v & 0x1F);
    for (uint8_t v : quiet.pwav) quietMax = std::max(quietMax, v & 0x1F);
    QVERIFY(loudMax > 24);
    QVERIFY(quietMax < 6);
    QCOMPARE(heightFromPeak(1.0), 31);
    QCOMPARE(heightFromPeak(0.0), 0);
    QCOMPARE(heightFromPeak(4.0), 31);                                     // clipped at the top, never negative
}

void TestTrakcrateAnalysis::waveformShapeAndColours()
{
    // 5 s silence, 5 s of a 100 Hz tone, 5 s of a 6 kHz tone
    DecodedPcm p = tcwav::silence(15);
    DecodedPcm lo = tcwav::sine(100, 0.7f, 5), hi = tcwav::sine(6000, 0.7f, 5);
    std::copy(lo.stereo.begin(), lo.stereo.end(), p.stereo.begin() + long(5 * p.sampleRate) * 2);
    std::copy(hi.stereo.begin(), hi.stereo.end(), p.stereo.begin() + long(10 * p.sampleRate) * 2);
    Waveform w = analyseWaveform(p);
    QCOMPARE(w.columns(), size_t(2250));
    QCOMPARE(w.pwv5.size(), size_t(2250));
    QCOMPARE(w.pwav.size(), size_t(400));
    QCOMPARE(w.pwv2.size(), size_t(100));
    QCOMPARE(int(w.pwv3[300] & 0x1F), 0);                                  // silence
    QVERIFY((w.pwv3[1125] & 0x1F) > 15);                                   // tone
    QVERIFY((w.pwav[200] & 0x1F) > 15);
    QVERIFY(w.pwav[50] == 0);
    QVERIFY((w.pwv2[50] & 0x0F) > 7);                                      // 4 bit heights
    QVERIFY((w.pwv2[50] & 0xF0) == 0);
    // the low tone is mostly "red" with little whiteness, the high tone is "blue" and white
    uint16_t a = w.pwv5[1125], b = w.pwv5[1875];
    QVERIFY((a >> 13) > ((a >> 7) & 7));
    QVERIFY(((b >> 7) & 7) > (b >> 13));
    QVERIFY((w.pwv3[1875] >> 5) > (w.pwv3[1125] >> 5));
    QCOMPARE(int((a >> 2) & 0x1F), int(w.pwv3[1125] & 0x1F));              // PWV5 and PWV3 share the height
}

void TestTrakcrateAnalysis::waveformFileRoundTrip()
{
    Waveform w = analyseWaveform(tcwav::drumLoop(120, 0.1, 12));
    std::string bytes = serialiseWaveform(w);
    Waveform back;
    QVERIFY(parseWaveform(bytes, &back));
    QVERIFY(back == w);
    QCOMPARE(serialiseWaveform(analyseWaveform(tcwav::drumLoop(120, 0.1, 12))), bytes);   // deterministic
    QVERIFY(!parseWaveform("", &back));
    QVERIFY(!parseWaveform("TCWV", &back));
    QVERIFY(!parseWaveform(bytes.substr(0, bytes.size() - 5), &back));                      // truncated
    std::string bad = bytes;
    bad[0] = 'X';
    QVERIFY(!parseWaveform(bad, &back));
    // a few rekordbox tracks have one PWV5 column more than PWV3: kept as imported
    Waveform longer = w;
    longer.pwv5.push_back(0x1234);
    Waveform again;
    QVERIFY(parseWaveform(serialiseWaveform(longer), &again));
    QVERIFY(again == longer);
    QCOMPARE(again.pwv5.size(), again.pwv3.size() + 1);
    Waveform shorter = w;
    shorter.pwv5.pop_back();
    QVERIFY(!parseWaveform(serialiseWaveform(shorter), &back));                              // fewer PWV5 than PWV3 stays invalid
}

void TestTrakcrateAnalysis::loudnessValues()
{
    Loudness l = measureLoudness(tcwav::sine(440, 0.5f, 3));
    QVERIFY(std::fabs(l.peakDb - (-6.02)) < 0.1);
    QVERIFY(std::fabs(l.rmsDb - (-9.03)) < 0.2);
    Loudness q = measureLoudness(tcwav::sine(440, 0.05f, 3));
    QVERIFY(q.peakDb < l.peakDb - 15);                                      // quiet tracks are measurably quiet
    QCOMPARE(measureLoudness(tcwav::silence(1)).peakDb, -120.0);
    QCOMPARE(measureLoudness(DecodedPcm()).rmsDb, -120.0);
}

void TestTrakcrateAnalysis::decodeResamplesAndDuplicatesMono()
{
    std::vector<float> m(8000);                                             // 1 s of 8 kHz mono
    for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * float(std::sin(2 * M_PI * 440.0 * double(i) / 8000.0));
    fs::path wav = tmp_ / "a.wav";
    tcwav::writeWavMono(wav, m, 8000);
    DecodedPcm p;
    std::string err;
    QVERIFY2(decodeAudio(wav, &p, &err), err.c_str());
    QCOMPARE(p.sampleRate, 44100);
    QVERIFY(std::fabs(p.seconds() - 1.0) < 0.02);
    QCOMPARE(p.stereo[20000], p.stereo[20001]);                             // mono duplicated to both channels
    double peak = 0;
    for (float v : p.stereo) peak = std::max(peak, double(std::fabs(v)));
    QVERIFY(std::fabs(peak - 0.5) < 0.05);

    // a cancel flag stops the decode
    std::atomic<bool> cancel(true);
    QVERIFY(!decodeAudio(wav, &p, &err, &cancel));
}

void TestTrakcrateAnalysis::decodeFailures()
{
    DecodedPcm p;
    std::string err;
    QVERIFY(!decodeAudio(tmp_ / "missing.wav", &p, &err));
    QVERIFY(!err.empty());
    std::ofstream(tmp_ / "junk.mp3") << "not audio, just text bytes";
    QVERIFY(!decodeAudio(tmp_ / "junk.mp3", &p, &err));
}

static fs::path makeTrack(const fs::path& root, const std::string& name, const DecodedPcm& pcm)
{
    fs::path dir = root / "master" / name;
    fs::create_directories(dir);
    tcwav::writeWavMono(dir / "track.wav", tcwav::mono(pcm), pcm.sampleRate);
    Track t;
    t.dir = dir;
    t.meta.set("title", name);
    saveMeta(t);
    return dir;
}

void TestTrakcrateAnalysis::analyseTrackEndToEnd()
{
    fs::path dir = makeTrack(tmp_, "clicks", tcwav::clicks(128, 0.25, 20));
    AnalyseResult r = analyseTrack(dir);
    QVERIFY2(r.ok, r.error.c_str());
    QVERIFY(r.beatgridWritten && r.waveformWritten);
    Track t;
    QVERIFY(loadTrack(dir, &t));
    QVERIFY(t.hasGrid);
    QVERIFY(std::fabs(beatGridFrom(t.grid).bpm - 128.0) < 0.05);
    QVERIFY(t.grid.getDouble("confidence") > 0.8);
    QCOMPARE(origin(t.source, "beatgrid"), std::string("generated"));
    QCOMPARE(origin(t.source, "waveform"), std::string("generated"));
    QVERIFY(t.meta.has("peak_dbfs") && t.meta.has("rms_dbfs"));
    QVERIFY(std::fabs(t.meta.getDouble("peak_dbfs") - (-5.4)) < 0.3);                          // the click burst peaks at about 0.54
    QCOMPARE(t.meta.get("title"), std::string("clicks"));                                    // other fields untouched
    Waveform w;
    QVERIFY(loadWaveform(dir, &w));
    QVERIFY(w.columns() >= 2990 && w.columns() <= 3010);                                     // 20 s x 150

    std::string grid1, wave1, meta1;
    readFile(dir / "beatgrid.txt", &grid1);
    readFile(dir / "waveform.bin", &wave1);
    readFile(dir / "meta.txt", &meta1);
    QVERIFY(analyseTrack(dir).ok);                                                          // same input, same bytes
    std::string grid2, wave2, meta2;
    readFile(dir / "beatgrid.txt", &grid2);
    readFile(dir / "waveform.bin", &wave2);
    readFile(dir / "meta.txt", &meta2);
    QCOMPARE(grid2, grid1);
    QVERIFY(wave2 == wave1);
    QCOMPARE(meta2, meta1);

    // no beats found: the track still gets an (empty) grid, so it is known to be analysed
    fs::path quiet = makeTrack(tmp_, "tone", tcwav::sine(440, 0.3f, 12));
    QVERIFY(analyseTrack(quiet).ok);
    Track q;
    QVERIFY(loadTrack(quiet, &q));
    QVERIFY(q.hasGrid);
    QVERIFY(beatGridFrom(q.grid).empty());

    AnalyseResult none = analyseTrack(tmp_ / "no-such-track");
    QVERIFY(!none.ok);
    fs::create_directories(tmp_ / "empty");
    QVERIFY(!analyseTrack(tmp_ / "empty").ok);                                              // no audio file
}

void TestTrakcrateAnalysis::analyseTrackRespectsOrigin()
{
    fs::path dir = makeTrack(tmp_, "owned", tcwav::clicks(100, 0.2, 15));
    Track t;
    QVERIFY(loadTrack(dir, &t));
    t.hasGrid = true;
    setBeatGrid(t.grid, BeatGrid{111.11, 5000});
    setOrigin(t.source, "beatgrid", "rekordbox");
    setOrigin(t.source, "waveform", "edited");
    QVERIFY(saveBeatGrid(t) && saveSource(t));

    AnalyseResult r = analyseTrack(dir);
    QVERIFY(r.ok);
    QVERIFY(!r.beatgridWritten && !r.waveformWritten);
    QCOMPARE(r.skipped.size(), size_t(2));
    QVERIFY(!fs::exists(dir / "waveform.bin"));
    QVERIFY(loadTrack(dir, &t));
    QCOMPARE(beatGridFrom(t.grid).bpm, 111.11);                                              // rekordbox's grid survives

    AnalyseOptions force;
    force.force = true;
    r = analyseTrack(dir, force);
    QVERIFY(r.ok && r.beatgridWritten && r.waveformWritten);
    QVERIFY(loadTrack(dir, &t));
    QVERIFY(std::fabs(beatGridFrom(t.grid).bpm - 100.0) < 0.05);
    QCOMPARE(origin(t.source, "beatgrid"), std::string("generated"));                        // forced regeneration changes the origin

    AnalyseOptions only;
    only.beatgrid = false;
    setOrigin(t.source, "waveform", "generated");
    QVERIFY(saveSource(t));
    fs::remove(dir / "waveform.bin");
    r = analyseTrack(dir, only);
    QVERIFY(r.ok && !r.beatgridWritten && r.waveformWritten);
}

void TestTrakcrateAnalysis::importerCanAnalyse()
{
    fs::path src = tmp_ / "Set";
    fs::create_directories(src);
    DecodedPcm p = tcwav::clicks(120, 0.1, 12);
    tcwav::writeWavMono(src / "a.wav", tcwav::mono(p), p.sampleRate);
    Library lib;
    QVERIFY(Library::create(tmp_ / "lib") && Library::open(tmp_ / "lib", &lib));
    ImportOptions o;
    o.analyse = true;
    ImportReport r = importFolder(lib, src, o);
    QVERIFY2(r.ok, r.error.c_str());
    QCOMPARE(r.imported, 1);
    fs::path dir = lib.trackDir("Set", "a");
    QVERIFY(fs::exists(dir / "beatgrid.txt"));
    QVERIFY(fs::exists(dir / "waveform.bin"));
    Track t;
    QVERIFY(loadTrack(dir, &t));
    QVERIFY(std::fabs(beatGridFrom(t.grid).bpm - 120.0) < 0.05);
    QCOMPARE(t.meta.get("title"), std::string("a"));
}

QTEST_APPLESS_MAIN(TestTrakcrateAnalysis)
#include "test_trakcrate_analysis.moc"
