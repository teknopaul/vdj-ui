#ifndef RAW_TCP_LOG_H
#define RAW_TCP_LOG_H

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QString>

namespace vdj {

// A byte-exact log of one TCP socket that does not depend on the message parser understanding anything: one line per socket event
// with a time relative to the connect, RX and TX in full hex (32 bytes per line). Used for the dbserver (1051) and the port query (12523)
// sockets when "Log raw TCP of the dbserver" is on.
class RawTcpLog {
public:
    bool open(const QString& path, const QString& header)
    {
        file_.setFileName(path);
        if (!file_.open(QIODevice::WriteOnly | QIODevice::Append)) return false;
        clock_.start();
        write(QStringLiteral("# %1 %2").arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), header));
        return true;
    }
    bool isOpen() const { return file_.isOpen(); }

    void event(const QString& text) { write(stamp() + text); }
    void rx(const QByteArray& b) { rxBytes_ += b.size(); data(QStringLiteral("RX"), b); }
    void tx(const QByteArray& b) { txBytes_ += b.size(); data(QStringLiteral("TX"), b); }
    void close(const QString& who, const QString& why, size_t unframed)
    {
        event(QStringLiteral("CLOSE %1 (%2) after %3 rx / %4 tx bytes").arg(who, why).arg(rxBytes_).arg(txBytes_));
        if (unframed) event(QStringLiteral("UNFRAMED %1 bytes left at close").arg(unframed));
        file_.close();
    }
    qint64 rxBytes() const { return rxBytes_; }
    qint64 txBytes() const { return txBytes_; }

private:
    QString stamp() const { return QStringLiteral("+%1 ").arg(double(clock_.nsecsElapsed()) / 1e6, 0, 'f', 1); }
    void data(const QString& dir, const QByteArray& b)
    {
        QString s = stamp() + QStringLiteral("%1 %2 bytes").arg(dir).arg(b.size());
        for (int i = 0; i < b.size(); i += 32)
            s += QStringLiteral("\n    ") + QString::fromLatin1(b.mid(i, 32).toHex(' '));
        write(s);
    }
    void write(const QString& line)
    {
        if (!file_.isOpen()) return;
        file_.write(line.toUtf8() + '\n');
        file_.flush();
    }
    QFile file_;
    QElapsedTimer clock_;
    qint64 rxBytes_ = 0, txBytes_ = 0;
};

} // namespace vdj

#endif // RAW_TCP_LOG_H
