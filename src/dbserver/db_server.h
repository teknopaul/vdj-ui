#ifndef DB_SERVER_H
#define DB_SERVER_H

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <memory>

#include "db_codec.h"
#include "db_handler.h"
#include "rb_library.h"

namespace vdj {

// The TCP side of the pretend-rekordbox mode: the port-query service (12523: "which port is the dbserver on") and the
// dbserver itself (1051). Every connection is logged to <logDir>/dbserver-<ip>-<unixtime>.log (decoded messages and raw
// hex) and .bin (the exact inbound byte stream); requests of unknown type also go to <logDir>/unknown-requests.log,
// which is the main output of the experiment.
class DbServer : public QObject {
    Q_OBJECT

public:
    struct Config {
        quint16 queryPort = 12523;       // 0 = any free port (tests)
        quint16 dbPort = 1051;           // 0 = any free port (tests)
        quint32 deviceNumber = 17;
        db::UnknownPolicy unknownPolicy = db::UnknownPolicy::EmptyOk;
        QString logDir;                  // empty = no log files
        int keepAliveIdleSecs = 10;      // decks probe after 10 s of silence
        QHostAddress bindAddress = QHostAddress::Any;
    };

    struct Stats {
        quint64 connections = 0;
        quint64 requests = 0;
        quint64 unknownRequests = 0;
        quint64 unimplementedRequests = 0;
        quint64 portQueries = 0;
    };

    explicit DbServer(const db::RbLibrary* lib, QObject* parent = nullptr);
    ~DbServer() override;

    bool start(const Config& cfg, QString* error = nullptr);
    void stop();
    bool isRunning() const { return dbServer_.isListening(); }

    quint16 queryPort() const { return queryServer_.serverPort(); }
    quint16 dbPort() const { return dbServer_.serverPort(); }
    int clientCount() const { return clients_.size(); }
    const Stats& stats() const { return stats_; }
    void setUnknownPolicy(db::UnknownPolicy p);

    // Close every dbserver connection from this host (a deck whose UDP presence has gone quiet).
    void closeClientsFor(const QHostAddress& addr);

signals:
    void clientConnected(const QString& ip);
    void clientClosed(const QString& ip);
    void portQueried(const QString& ip);
    void requestHandled(const QString& ip, quint16 type, int kind);      // kind = HandlerResult::Kind
    void unknownRequest(const QString& ip, quint16 type, const QString& description, const QByteArray& raw);
    void logLine(const QString& line);

private slots:
    void onQueryConnection();
    void onDbConnection();

private:
    struct Client {
        QTcpSocket*       sock = nullptr;
        QString           ip;
        db::StreamParser  parser;
        db::Session       session;
        QByteArray        raw;            // inbound bytes not yet consumed by the parser
        size_t            rawBase = 0;    // stream offset of raw[0]
        std::unique_ptr<QFile> textLog, binLog;
        bool              closing = false;
    };

    void readClient(Client* c);
    void dropClient(QTcpSocket* s);
    void tuneKeepAlive(QTcpSocket* s);
    void clientLog(Client* c, const QString& line);
    void openLogs(Client* c);
    void logUnknown(Client* c, const db::Message& m, const QByteArray& raw);
    void emitLog(const QString& line);

    const db::RbLibrary*          lib_ = nullptr;
    Config                        cfg_;
    std::unique_ptr<db::DbHandler> handler_;
    QTcpServer                    queryServer_, dbServer_;
    QHash<QTcpSocket*, std::shared_ptr<Client>> clients_;
    Stats                         stats_;
};

} // namespace vdj

#endif // DB_SERVER_H
