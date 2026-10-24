#ifndef PACKET_LOGGER_H
#define PACKET_LOGGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QDateTime>
#include <QFile>

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

    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }
    void setLogFile(const QString& path);
    QString logFilePath() const;

    void log(const QString& type, const QString& detail);

    const QVector<PacketEntry>& entries() const { return entries_; }
    int maxEntries() const { return maxEntries_; }

signals:
    void entryAdded();

private:
    PacketLogger(QObject* parent = nullptr);

    bool enabled_ = false;
    int maxEntries_ = 5000;
    QVector<PacketEntry> entries_;
    QFile file_;
    QString filePath_;
};

} // namespace vdj

#endif // PACKET_LOGGER_H