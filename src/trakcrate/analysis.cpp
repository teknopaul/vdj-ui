#include "analysis.h"

#include <cstdio>

namespace trakcrate {

static bool protectedOrigin(const std::string& o) { return o == "rekordbox" || o == "edited"; }

static std::string fixed(double v, int decimals)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

bool loadWaveform(const fs::path& trackDir, Waveform* out, std::string* err)
{
    std::string bytes;
    if (!readFile(trackDir / "waveform.bin", &bytes, err)) return false;
    if (!parseWaveform(bytes, out)) {
        if (err) *err = "waveform.bin is damaged";
        return false;
    }
    return true;
}

AnalyseResult analyseTrack(const fs::path& trackDir, const AnalyseOptions& opts, const std::atomic<bool>* cancel)
{
    AnalyseResult res;
    Track t;
    if (!loadTrack(trackDir, &t, &res.error)) return res;
    fs::path audio = audioFileOf(trackDir);
    if (audio.empty()) { res.error = "no audio file in " + trackDir.string(); return res; }

    bool doGrid = opts.beatgrid, doWave = opts.waveform;
    if (doGrid && !opts.force && protectedOrigin(origin(t.source, "beatgrid"))) {
        doGrid = false;
        res.skipped.push_back("beatgrid (origin " + origin(t.source, "beatgrid") + ")");
    }
    if (doWave && !opts.force && protectedOrigin(origin(t.source, "waveform"))) {
        doWave = false;
        res.skipped.push_back("waveform (origin " + origin(t.source, "waveform") + ")");
    }
    if (!doGrid && !doWave) { res.ok = true; return res; }

    DecodedPcm pcm;
    if (!decodeAudio(audio, &pcm, &res.error, cancel)) return res;

    if (doGrid) {
        BeatAnalysis b = analyseBeats(pcm, t.meta.getDouble("bpm_tag", 0));
        res.grid = b.grid;
        res.confidence = b.confidence;
        if (!t.hasGrid) t.grid.addComment("trakcrate beatgrid");
        t.hasGrid = true;
        setBeatGrid(t.grid, b.grid);
        t.grid.set("confidence", fixed(b.confidence, 3));
        t.grid.set("analyser", "trakcrate-beats 1");
        setOrigin(t.source, "beatgrid", "generated");
    }
    Waveform w;
    if (doWave) {
        w = analyseWaveform(pcm);
        setOrigin(t.source, "waveform", "generated");
    }
    Loudness l = measureLoudness(pcm);
    t.meta.set("peak_dbfs", fixed(l.peakDb, 1));
    t.meta.set("rms_dbfs", fixed(l.rmsDb, 1));

    if (doWave && !atomicWrite(trackDir / "waveform.bin", serialiseWaveform(w), &res.error)) return res;
    res.waveformWritten = doWave;
    if (doGrid && !saveBeatGrid(t, &res.error)) return res;
    res.beatgridWritten = doGrid;
    if (!saveMeta(t, &res.error) || !saveSource(t, &res.error)) return res;
    res.ok = true;
    return res;
}

} // namespace trakcrate
