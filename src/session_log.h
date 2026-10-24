#ifndef SESSION_LOG_H
#define SESSION_LOG_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace vdj {

struct PlayEvent {
    QDateTime time;
    int       deckId = 0;
    QString   title;
    QString   artist;
    QString   bpm;
    QString   notes;  // per-event DJ annotation
};

struct DjSet {
    QString         djName;
    QDateTime       start;
    QList<PlayEvent> events;
};

class SessionLog : public QObject {
    Q_OBJECT

public:
    explicit SessionLog(QObject* parent = nullptr);

    void addEvent(int deckId, const QString& title, const QString& artist,
                  const QString& bpm);
    void newSet(const QString& djName);

    const QList<DjSet>& sets() const { return sets_; }
    DjSet* currentSet();

    bool exportText(const QString& path) const;
    bool exportCsv(const QString& path) const;

signals:
    void eventAdded(const PlayEvent& event);
    void setStarted(const QString& djName);

private:
    QList<DjSet> sets_;
    // Track last seen title per deck to detect changes
    QHash<int, QString> lastTitle_;
};

} // namespace vdj

#endif // SESSION_LOG_H
