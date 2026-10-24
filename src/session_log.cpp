#include "session_log.h"

#include <QFile>
#include <QHash>
#include <QTextStream>

namespace vdj {

SessionLog::SessionLog(QObject* parent) : QObject(parent)
{
    sets_.append({ QStringLiteral("DJ 1"), QDateTime::currentDateTime(), {} });
}

DjSet* SessionLog::currentSet()
{
    return sets_.isEmpty() ? nullptr : &sets_.last();
}

void SessionLog::newSet(const QString& djName)
{
    sets_.append({ djName, QDateTime::currentDateTime(), {} });
    emit eventAdded({});  // notify panel to refresh header
    emit setStarted(djName);
}

void SessionLog::addEvent(int deckId, const QString& title,
                          const QString& artist, const QString& bpm)
{
    if (title.isEmpty()) return;

    // Only log when title changes on this deck
    if (lastTitle_.value(deckId) == title) return;
    lastTitle_.insert(deckId, title);

    PlayEvent ev;
    ev.time   = QDateTime::currentDateTime();
    ev.deckId = deckId;
    ev.title  = title;
    ev.artist = artist;
    ev.bpm    = bpm;

    if (sets_.isEmpty())
        sets_.append({ QStringLiteral("DJ 1"), ev.time, {} });
    sets_.last().events.append(ev);
    emit eventAdded(ev);
}

bool SessionLog::exportText(const QString& path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&f);

    for (const DjSet& s : sets_) {
        out << QStringLiteral("DJ: %1  %2\n")
               .arg(s.djName, s.start.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        for (const PlayEvent& ev : s.events) {
            QString line = QStringLiteral("%1  [Deck %2]  %3 — %4  (%5 BPM)")
                .arg(ev.time.toString(QStringLiteral("HH:mm")))
                .arg(ev.deckId)
                .arg(ev.title, ev.artist, ev.bpm);
            if (!ev.notes.isEmpty())
                line += QStringLiteral("  // ") + ev.notes;
            out << line << QLatin1Char('\n');
        }
        out << QLatin1Char('\n');
    }
    return true;
}

bool SessionLog::exportCsv(const QString& path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&f);
    out << "DJ,Set Start,Time,Deck,Title,Artist,BPM,Notes\n";

    for (const DjSet& s : sets_) {
        for (const PlayEvent& ev : s.events) {
            auto esc = [](const QString& v) {
                QString r = v; r.replace(QLatin1Char('"'), QStringLiteral("\"\""));
                return QLatin1Char('"') + r + QLatin1Char('"');
            };
            out << esc(s.djName) << QLatin1Char(',')
                << esc(s.start.toString(Qt::ISODate)) << QLatin1Char(',')
                << esc(ev.time.toString(Qt::ISODate)) << QLatin1Char(',')
                << ev.deckId << QLatin1Char(',')
                << esc(ev.title) << QLatin1Char(',')
                << esc(ev.artist) << QLatin1Char(',')
                << esc(ev.bpm) << QLatin1Char(',')
                << esc(ev.notes) << QLatin1Char('\n');
        }
    }
    return true;
}

} // namespace vdj
