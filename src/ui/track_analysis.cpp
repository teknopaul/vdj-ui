#include "track_analysis.h"
#include <cdj/anlz.h>
#include <QByteArray>
#include <QFile>

namespace vdj {

QString AnalysisCue::letter() const
{
    return (hotCue >= 1 && hotCue <= 8) ? QString(QChar('A' + hotCue - 1)) : QString();
}

quint32 TrackAnalysis::lengthMs() const
{
    if (!color.isEmpty())
        return quint32(color.size() * 1000.0 / 150.0);
    return lastBeatMs;
}

static QVector<quint8> heights(const anlz_waveform_t& w)
{
    QVector<quint8> out;
    out.reserve(int(w.count));
    anlz_wave_column_t c;
    for (uint32_t i = 0; i < w.count; ++i) {
        anlz_wave_column(&w, i, &c);
        out.append(c.height);
    }
    return out;
}

QVector<quint8> loadMiniWaveform(const QString& root, const QString& analyzePath)
{
    QVector<quint8> out;
    char path[4096];
    if (anlz_resolve_path(QFile::encodeName(root).constData(), QFile::encodeName(analyzePath).constData(),
                          ANLZ_FILE_DAT, path, sizeof path) != 0)
        return out;
    anlz_t* a = nullptr;
    if (anlz_open_file(path, &a) != ANLZ_OK)
        return out;
    anlz_waveform_t w;
    if (anlz_waveform(a, ANLZ_WAVE_TINY, &w) == ANLZ_OK)
        out = heights(w);
    anlz_close(a);
    return out;
}

TrackAnalysis loadTrackAnalysis(const QString& root, const QString& analyzePath)
{
    TrackAnalysis r;
    anlz_track_t* t = nullptr;
    if (anlz_track_open(QFile::encodeName(root).constData(), QFile::encodeName(analyzePath).constData(), &t) != ANLZ_OK)
        return r;
    r.valid = true;

    anlz_waveform_t w;
    if (anlz_track_waveform(t, ANLZ_WAVE_TINY, &w) == ANLZ_OK)
        r.mini = heights(w);
    if (anlz_track_waveform(t, ANLZ_WAVE_PREVIEW, &w) == ANLZ_OK)
        r.preview = heights(w);
    if (anlz_track_waveform(t, ANLZ_WAVE_COLOR_DETAIL, &w) == ANLZ_OK) {
        r.color.reserve(int(w.count));
        for (uint32_t i = 0; i < w.count; ++i) {
            const uint8_t* e = w.data + size_t(i) * 2;
            r.color.append(quint16((e[0] << 8) | e[1]));
        }
    }

    anlz_beatgrid_t g;
    if (anlz_track_beatgrid(t, &g) == ANLZ_OK && g.count > 0) {
        anlz_beat_t first, last;
        anlz_beat_at(&g, 0, &first);
        anlz_beat_at(&g, g.count - 1, &last);
        r.beatCount = int(g.count);
        r.avgBpm = anlz_avg_bpm(&g);
        r.firstBeatMs = first.time_ms;
        r.lastBeatMs = last.time_ms;
        r.beats.reserve(int(g.count));
        for (uint32_t i = 0; i < g.count; ++i) {
            anlz_beat_t b;
            anlz_beat_at(&g, i, &b);
            r.beats.append({ b.time_ms + b.sub_ms_us / 1000.0, quint8(b.beat_number) });
        }
    }

    anlz_cue_list_t cues;
    if (anlz_track_cues(t, ANLZ_CUES_ALL, &cues) == ANLZ_OK) {
        for (uint32_t i = 0; i < cues.count; ++i) {
            const anlz_cue_t& c = cues.cues[i];
            AnalysisCue ac;
            ac.hotCue = c.hot_cue;
            ac.loop = c.is_loop;
            ac.timeMs = c.time_ms;
            ac.loopEndMs = c.loop_time_ms;
            ac.loopNum = c.loop_num;
            ac.loopDen = c.loop_den;
            r.cues.append(ac);
        }
    }
    anlz_cue_list_free(&cues);
    anlz_track_close(t);
    return r;
}

} // namespace vdj
