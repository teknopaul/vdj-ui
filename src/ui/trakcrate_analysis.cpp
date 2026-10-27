#include "trakcrate_analysis.h"

#include <cmath>

#include "trakcrate/analysis.h"
#include "trakcrate/analysis_wave.h"
#include "trakcrate/track.h"

namespace vdj {

TrackAnalysis trakcrateTrackAnalysis(const QString& trackDir)
{
    TrackAnalysis a;
    trakcrate::Track t;
    if (!trakcrate::loadTrack(trackDir.toStdString(), &t)) return a;

    trakcrate::Waveform w;
    if (trakcrate::loadWaveform(trackDir.toStdString(), &w)) {
        a.valid = true;
        a.preview.reserve(int(w.pwav.size()));
        for (uint8_t b : w.pwav) a.preview.append(b);
        a.mini.reserve(int(w.pwv2.size()));
        for (uint8_t b : w.pwv2) a.mini.append(b);
        a.color.reserve(int(w.pwv5.size()));
        for (uint16_t e : w.pwv5) a.color.append(e);
    }

    const trakcrate::BeatGrid g = trakcrate::beatGridFrom(t.grid);
    double lengthMs = double(t.meta.getInt("duration_ms", 0));
    if (lengthMs <= 0 && !w.pwv3.empty()) lengthMs = double(w.pwv3.size()) * 1000.0 / trakcrate::Waveform::kColumnsPerSecond;
    if (t.hasGrid && !g.empty() && lengthMs > 0) {
        a.valid = true;
        const double periodMs = 60000.0 / g.bpm;
        const double firstMs = double(g.firstBeatUs) / 1000.0;
        for (int i = 0; firstMs + i * periodMs <= lengthMs && i < 100000; ++i) {
            AnalysisBeat b;
            b.ms = firstMs + i * periodMs;
            b.number = quint8(i % 4 + 1);
            a.beats.append(b);
        }
        a.beatCount = a.beats.size();
        a.avgBpm = g.bpm;
        if (!a.beats.isEmpty()) {
            a.firstBeatMs = quint32(std::max(0.0, a.beats.first().ms));
            a.lastBeatMs = quint32(std::max(0.0, a.beats.last().ms));
        }
    }

    if (t.hasCuemarks) {
        a.valid = true;
        QList<AnalysisCue> memory, hot;
        for (const trakcrate::Cuemark& m : t.cuemarks.marks) {
            AnalysisCue c;
            c.timeMs = quint32(std::max(0.0, m.ms));
            if (m.hot >= 'A' && m.hot <= 'C') { c.hotCue = m.hot - 'A' + 1; hot.append(c); }
            else memory.append(c);
        }
        a.cues = memory + hot;                         // memory points first, like the ANLZ reader
    }
    return a;
}

} // namespace vdj
