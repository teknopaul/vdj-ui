#ifndef TRACK_ANALYSIS_H
#define TRACK_ANALYSIS_H

#include <QList>
#include <QString>
#include <QVector>

namespace vdj {

// A hot cue or loop from the track's ANLZ analysis files.
struct AnalysisCue {
    int hotCue = 0;             // 1..8 = A..H, 0 = memory point
    bool loop = false;
    quint32 timeMs = 0;
    quint32 loopEndMs = 0;
    int loopNum = 0, loopDen = 0;   // loop length in beats = num/den (0 when unknown)

    QString letter() const;     // "A".."H", "" for a memory point
    double loopBeats() const { return loopDen ? double(loopNum) / loopDen : 0.0; }
};

// One beat of the grid. ms includes the microsecond fraction when the track's .EXT has one.
struct AnalysisBeat {
    double ms = 0.0;
    quint8 number = 1;          // 1..4 within the bar; 1 is the bar's downbeat
};

// What libcdj reads from a track's .DAT/.EXT, converted to Qt types.
struct TrackAnalysis {
    bool valid = false;             // at least one analysis file could be read
    QVector<quint8> mini;           // PWV2, 100 columns, heights 0..15 (the small graphic in the track list)
    QVector<quint16> color;         // PWV5, 150 entries per second: red 3, green 3, blue 3, height 5 bits
    QVector<quint8> preview;        // PWAV, 400 columns, heights 0..31 (when there is no PWV5)
    int beatCount = 0;              // 0 for a track rekordbox has not analysed
    double avgBpm = 0.0;
    QVector<AnalysisBeat> beats;    // the whole grid, in time order (empty when not analysed)
    quint32 firstBeatMs = 0, lastBeatMs = 0;      // clamped at 0; the beats keep their exact (possibly negative) times
    QList<AnalysisCue> cues;        // memory points first, then hot cues A, B, C ...

    quint32 lengthMs() const;       // from the detail waveform, else the last beat
};

// root = folder holding PIONEER, analyzePath = the database's path of the .DAT. Everything is optional.
TrackAnalysis loadTrackAnalysis(const QString& root, const QString& analyzePath);
// Only the 100 column mini waveform from the (small) .DAT; empty when unavailable.
QVector<quint8> loadMiniWaveform(const QString& root, const QString& analyzePath);

} // namespace vdj

#endif // TRACK_ANALYSIS_H
