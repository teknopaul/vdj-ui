#ifndef SYNC_TELEMETRY_H
#define SYNC_TELEMETRY_H

#include <QFile>
#include <QString>
#include <mutex>

namespace vdj {

// One CSV line per remote beat (and one per sync event) so a DJ can test sync against real decks and plot what happened:
// reported vs measured BPM, the offset to the sequencer, the nudges we applied. Off by default; nothing is opened or
// written until start(). Columns are only ever appended to, so scripts that read the log keep working.
class SyncTelemetry {
public:
    struct Sample {
        double steadyMs = 0.0;         // arrival time on the steady clock
        int pid = 0;
        int barPos = 0;
        double reportedBpm = 0.0;      // from the beat packet
        double measuredBpm = 0.0;      // BeatTempo, 0 until valid
        double alsaBpm = 0.0;          // the sequencer queue's tempo
        double diffMs = 0.0;           // this beat's offset from the sequencer
        double avgDiffMs = 0.0;        // the rolling average, 0 until measured
        int targetMs = 0;
        int nudgeMs = 0;               // nudge applied because of this beat
        int masterPid = 0;
        QString syncState;             // "off", "locked", "follow-master", ...
        // raw fields of the deck's beat packet (appended columns): reported = trackBpmRaw / 100 * pitch multiplier
        int trackBpmRaw = 0;           // the deck's track BPM in 0.01 BPM
        quint32 pitchRaw = 0;          // 0x100000 = 100%
        quint32 nextBeatMs = 0;        // grid time to the next beat at 100% pitch, whole ms
        quint32 beat8Ms = 0;           // grid time to the 8th beat at 100% pitch, whole ms
        double estPeriodMs = 0.0;      // BeatPhase: the deck's beat period (0 = no estimate)
        double estPhaseUncMs = 0.0;    // BeatPhase: half width of the last beat's phase
        int estResonant = 0;           // BeatPhase: 1 when the period is near a multiple of the deck's tick and averaging cannot help
        double diffTickMs = 0.0;
        double diffUsedMs = 0.0;       // the offset the difflock was given (the exact one, or the quantised-phase one when that is on)       // diffMs as the sequencer's tick counter gives it (5 ms steps), to compare with the exact one
    };

    ~SyncTelemetry() { stop(); }

    static QString header();
    static QString formatBeat(const Sample& s);
    static QString formatEvent(double steadyMs, const QString& event, const Sample& state);

    // Opens <dir>/sync-telemetry-<timestamp>.csv. False (and stays off) when the file cannot be created.
    bool start(const QString& dir);
    void stop();
    bool active() const { return active_; }
    QString filePath() const { return path_; }

    void beat(const Sample& s);
    void event(double steadyMs, const QString& event, const Sample& state);

private:
    void writeLine(const QString& line);

    mutable std::mutex mutex_;
    QFile file_;
    QString path_;
    bool active_ = false;
};

} // namespace vdj

#endif // SYNC_TELEMETRY_H
