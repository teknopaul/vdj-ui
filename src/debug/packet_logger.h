#ifndef PACKET_LOGGER_H
#define PACKET_LOGGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <atomic>

namespace vdj {

struct PacketEntry {
    QDateTime timestamp;
    QString type;   // UPDATE / BEAT / DISCOVERY / USER / NFS / SYS
    QString detail;
};

class PacketLogger : public QObject {
    Q_OBJECT

public:
    static PacketLogger& instance();

    // Nothing is recorded while disabled (debug mode off): no memory, no file, no work on the network threads.
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }
    // The high-rate lines (UPDATE, BEAT, DISCOVERY: every packet of every deck) can be switched off on their own while the rest (USER, LINK, SYS, NFS) is kept.
    void setPacketLevel(bool on) { packets_ = on; }
    bool packetLevel() const { return enabled_ && packets_; }   // callers check this before they build the text of a packet line
    void setLogFile(const QString& path);
    QString logFilePath() const;

    void log(const QString& type, const QString& detail);

    // A copy, taken under the lock: log() is called from the library's threads.
    QVector<PacketEntry> entries() const;
    int entryCount() const;
    int maxEntries() const { return maxEntries_; }

signals:
    void entryAdded();

private:
    PacketLogger(QObject* parent = nullptr);

    std::atomic<bool> enabled_{false};
    std::atomic<bool> packets_{true};
    int maxEntries_ = 5000;
    mutable QMutex mutex_;                  // guards entries_, file_ and filePath_
    QVector<PacketEntry> entries_;
    QFile file_;
    QString filePath_;
};

} // namespace vdj

#endif // PACKET_LOGGER_H