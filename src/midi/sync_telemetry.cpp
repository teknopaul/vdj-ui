#include "sync_telemetry.h"
#include <QDateTime>
#include <QDir>

namespace vdj {

QString SyncTelemetry::header()
{
    return QStringLiteral("steady_ms,kind,pid,bar_pos,reported_bpm,measured_bpm,alsa_bpm,diff_ms,avg_diff_ms,target_ms,nudge_ms,master_pid,sync_state,note,track_bpm_raw,pitch_raw,pitch_pct,next_beat_ms,beat8_ms,diff_tick_ms,est_period_ms,est_phase_unc_ms,est_resonant,diff_used_ms");
}

static QString num(double v, int decimals)
{
    return QString::number(v, 'f', decimals);
}

static QString row(double steadyMs, const QString& kind, const SyncTelemetry::Sample& s, const QString& note)
{
    QString n = note;
    n.replace(QLatin1Char('"'), QLatin1String("'"));
    const double pitchPct = s.pitchRaw ? (double(s.pitchRaw) / 1048576.0 - 1.0) * 100.0 : 0.0;
    return QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,\"%14\",%15,%16,%17,%18,%19,%20,%21,%22,%23,%24")
        .arg(num(steadyMs, 3), kind)
        .arg(s.pid).arg(s.barPos)
        .arg(num(s.reportedBpm, 3), num(s.measuredBpm, 3), num(s.alsaBpm, 3))
        .arg(num(s.diffMs, 2), num(s.avgDiffMs, 2))
        .arg(s.targetMs).arg(s.nudgeMs).arg(s.masterPid)
        .arg(s.syncState, n)
        .arg(s.trackBpmRaw).arg(s.pitchRaw).arg(num(pitchPct, 4)).arg(s.nextBeatMs).arg(s.beat8Ms).arg(num(s.diffTickMs, 2)).arg(num(s.estPeriodMs, 4), num(s.estPhaseUncMs, 3)).arg(s.estResonant).arg(num(s.diffUsedMs, 3));
}

QString SyncTelemetry::formatBeat(const Sample& s)
{
    return row(s.steadyMs, QStringLiteral("beat"), s, QString());
}

QString SyncTelemetry::formatEvent(double steadyMs, const QString& event, const Sample& state)
{
    return row(steadyMs, QStringLiteral("event"), state, event);
}

bool SyncTelemetry::start(const QString& dir)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_)
        return true;
    QDir().mkpath(dir);
    path_ = dir + QStringLiteral("/sync-telemetry-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".csv");
    file_.setFileName(path_);
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    active_ = true;
    file_.write((header() + QLatin1Char('\n')).toUtf8());
    file_.flush();
    return true;
}

void SyncTelemetry::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_)
        return;
    file_.close();
    active_ = false;
}

void SyncTelemetry::writeLine(const QString& line)
{
    file_.write((line + QLatin1Char('\n')).toUtf8());
    file_.flush();                                   // a crash or a pulled cable must not lose the interesting part
}

void SyncTelemetry::beat(const Sample& s)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_)
        writeLine(formatBeat(s));
}

void SyncTelemetry::event(double steadyMs, const QString& event, const Sample& state)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_)
        writeLine(formatEvent(steadyMs, event, state));
}

} // namespace vdj
