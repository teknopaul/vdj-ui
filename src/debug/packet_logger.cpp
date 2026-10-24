#include "packet_logger.h"
#include <QDir>
#include <QStandardPaths>

namespace vdj {

PacketLogger& PacketLogger::instance()
{
    static PacketLogger logger;
    return logger;
}

PacketLogger::PacketLogger(QObject* parent)
    : QObject(parent)
{
}

void PacketLogger::setEnabled(bool enabled)
{
    if (enabled_ == enabled)
        return;

    if (enabled && filePath_.isEmpty()) {
        QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (base.isEmpty())
            base = QDir::homePath() + QStringLiteral("/.cache/vdj-ui");
        QDir(base).mkpath(QStringLiteral("."));
        filePath_ = base + QStringLiteral("/packets.log");
    }

    if (enabled) {
        file_.setFileName(filePath_);
        file_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    } else if (file_.isOpen()) {
        file_.close();
    }
    enabled_ = enabled;
}

void PacketLogger::setLogFile(const QString& path)
{
    filePath_ = path;
    if (enabled_ && file_.isOpen()) {
        file_.close();
        file_.setFileName(path);
        file_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
}

QString PacketLogger::logFilePath() const
{
    return filePath_;
}

void PacketLogger::log(const QString& type, const QString& detail)
{
    PacketEntry e;
    e.timestamp = QDateTime::currentDateTime();
    e.type = type;
    e.detail = detail;

    if (entries_.size() >= maxEntries_)
        entries_.remove(0, entries_.size() - maxEntries_ + 1);
    entries_.append(e);

    if (file_.isOpen()) {
        QByteArray line = e.timestamp.toString(Qt::ISODateWithMs).toUtf8();
        line += " [";
        line += type.toUtf8();
        line += "] ";
        line += detail.toUtf8();
        line += "\n";
        file_.write(line);
        file_.flush();
    }

    emit entryAdded();
}

} // namespace vdj