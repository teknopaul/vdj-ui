#include "db_server.h"

#include <QDir>
#include <QTimer>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

namespace vdj {

using namespace db;

namespace {

QString hexLine(const QByteArray& b)
{
    return QString::fromLatin1(b.toHex(' '));
}

} // namespace

DbServer::DbServer(const RbLibrary* lib, QObject* parent) : QObject(parent), lib_(lib)
{
    connect(&queryServer_, &QTcpServer::newConnection, this, &DbServer::onQueryConnection);
    connect(&dbServer_, &QTcpServer::newConnection, this, &DbServer::onDbConnection);
}

DbServer::~DbServer()
{
    stop();
}

bool DbServer::start(const Config& cfg, QString* error)
{
    stop();
    cfg_ = cfg;
    handler_.reset(new DbHandler(*lib_, cfg.deviceNumber));
    handler_->setUnknownPolicy(cfg.unknownPolicy);
    if (!cfg.logDir.isEmpty()) QDir().mkpath(cfg.logDir);

    if (!dbServer_.listen(cfg.bindAddress, cfg.dbPort)) {
        if (error) *error = QStringLiteral("cannot listen on dbserver port %1: %2").arg(cfg.dbPort).arg(dbServer_.errorString());
        return false;
    }
    if (!queryServer_.listen(cfg.bindAddress, cfg.queryPort)) {
        if (error) *error = QStringLiteral("cannot listen on port-query port %1: %2").arg(cfg.queryPort).arg(queryServer_.errorString());
        dbServer_.close();
        return false;
    }
    emitLog(QStringLiteral("dbserver: listening on port %1 (query port %2), device number %3")
                .arg(dbPort()).arg(queryPort()).arg(cfg.deviceNumber));
    return true;
}

void DbServer::stop()
{
    queryServer_.close();
    dbServer_.close();
    const auto socks = clients_.keys();
    for (QTcpSocket* s : socks) {
        s->disconnect(this);
        s->abort();
        auto c = clients_.take(s);
        if (c) emit clientClosed(c->ip);
        s->deleteLater();
    }
}

void DbServer::setUnknownPolicy(UnknownPolicy p)
{
    cfg_.unknownPolicy = p;
    if (handler_) handler_->setUnknownPolicy(p);
}

void DbServer::emitLog(const QString& line)
{
    emit logLine(line);
}

void DbServer::tuneKeepAlive(QTcpSocket* s)
{
    s->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    const int fd = int(s->socketDescriptor());
    if (fd < 0) return;
    int idle = cfg_.keepAliveIdleSecs, intvl = cfg_.keepAliveIdleSecs, cnt = 3;
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof intvl);
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof cnt);
}

// ---- port 12523 ----------------------------------------------------------------------------------------------------

void DbServer::onQueryConnection()
{
    while (QTcpSocket* s = queryServer_.nextPendingConnection()) {
        const QString ip = QHostAddress(s->peerAddress().toIPv4Address()).toString();
        auto buf = std::make_shared<QByteArray>();
        auto done = std::make_shared<bool>(false);
        connect(s, &QTcpSocket::readyRead, this, [this, s, ip, buf, done]() {
            buf->append(s->readAll());
            if (*done || buf->size() < int(portQueryRequest().size())) return;
            *done = true;
            if (isPortQueryRequest(reinterpret_cast<const uint8_t*>(buf->constData()), size_t(buf->size()))) {
                ++stats_.portQueries;
                const auto reply = portQueryReply(dbPort());
                s->write(reinterpret_cast<const char*>(reply.data()), qint64(reply.size()));
                emitLog(QStringLiteral("port query from %1 -> %2").arg(ip).arg(dbPort()));
                emit portQueried(ip);
            } else {
                emitLog(QStringLiteral("port query from %1: unexpected %2 bytes: %3").arg(ip).arg(buf->size()).arg(hexLine(buf->left(32))));
            }
            s->disconnectFromHost();
        });
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        QTimer::singleShot(3000, s, [s]() { if (s->state() != QAbstractSocket::UnconnectedState) s->abort(); });
    }
}

// ---- dbserver ------------------------------------------------------------------------------------------------------

void DbServer::openLogs(Client* c)
{
    if (cfg_.logDir.isEmpty()) return;
    const QString base = QStringLiteral("%1/dbserver-%2-%3").arg(cfg_.logDir, c->ip).arg(QDateTime::currentSecsSinceEpoch());
    c->textLog.reset(new QFile(base + ".log"));
    if (!c->textLog->open(QIODevice::WriteOnly | QIODevice::Append)) c->textLog.reset();
    c->binLog.reset(new QFile(base + ".bin"));
    if (!c->binLog->open(QIODevice::WriteOnly)) c->binLog.reset();
}

void DbServer::clientLog(Client* c, const QString& line)
{
    const QString stamped = QDateTime::currentDateTime().toString("hh:mm:ss.zzz") + ' ' + line;
    if (c->textLog) {
        c->textLog->write(stamped.toUtf8() + '\n');
        c->textLog->flush();
    }
    emitLog(QStringLiteral("[%1] %2").arg(c->ip, line));
}

void DbServer::onDbConnection()
{
    while (QTcpSocket* s = dbServer_.nextPendingConnection()) {
        auto c = std::make_shared<Client>();
        c->sock = s;
        c->ip = QHostAddress(s->peerAddress().toIPv4Address()).toString();
        clients_.insert(s, c);
        ++stats_.connections;
        tuneKeepAlive(s);
        openLogs(c.get());
        clientLog(c.get(), QStringLiteral("connected from %1:%2").arg(c->ip).arg(s->peerPort()));
        emit clientConnected(c->ip);

        const auto greeting = encodeGreeting();
        s->write(reinterpret_cast<const char*>(greeting.data()), qint64(greeting.size()));
        clientLog(c.get(), QStringLiteral("SEND greeting"));

        Client* raw = c.get();
        connect(s, &QTcpSocket::readyRead, this, [this, raw]() { readClient(raw); });
        connect(s, &QTcpSocket::disconnected, this, [this, s]() { dropClient(s); });
    }
}

void DbServer::dropClient(QTcpSocket* s)
{
    auto c = clients_.take(s);
    if (!c) return;
    clientLog(c.get(), QStringLiteral("closed after %1 requests").arg(c->session.requests));
    emit clientClosed(c->ip);
    s->deleteLater();
}

void DbServer::closeClientsFor(const QHostAddress& addr)
{
    const quint32 want = addr.toIPv4Address();
    const auto socks = clients_.keys();
    for (QTcpSocket* s : socks) {
        if (s->peerAddress().toIPv4Address() != want) continue;
        auto c = clients_.value(s);
        if (c) clientLog(c.get(), QStringLiteral("closing: peer presence lost"));
        s->disconnectFromHost();
    }
}

void DbServer::logUnknown(Client* c, const Message& m, const QByteArray& raw)
{
    ++stats_.unknownRequests;
    const QString desc = QString::fromStdString(describe(m));
    emit unknownRequest(c->ip, m.type, desc, raw);
    if (cfg_.logDir.isEmpty()) return;
    QFile f(cfg_.logDir + "/unknown-requests.log");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    f.write(QStringLiteral("%1 %2 type=0x%3 argc=%4 %5\n    hex=%6\n")
                .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), c->ip)
                .arg(m.type, 4, 16, QLatin1Char('0')).arg(m.args.size()).arg(desc, QString::fromLatin1(raw.toHex()))
                .toUtf8());
}

void DbServer::readClient(Client* c)
{
    const QByteArray in = c->sock->readAll();
    if (in.isEmpty()) return;
    if (c->binLog) { c->binLog->write(in); c->binLog->flush(); }
    clientLog(c, QStringLiteral("RECV %1 bytes: %2").arg(in.size()).arg(hexLine(in.left(64)) + (in.size() > 64 ? " ..." : "")));
    c->raw.append(in);
    c->parser.feed(reinterpret_cast<const uint8_t*>(in.constData()), size_t(in.size()));

    for (;;) {
        const size_t before = c->parser.consumed();
        Message m;
        const StreamParser::Status st = c->parser.next(m);
        if (st == StreamParser::None) break;
        if (st == StreamParser::Error) {
            clientLog(c, QStringLiteral("framing error at stream offset %1; context: %2")
                             .arg(c->parser.errorOffset())
                             .arg(hexLine(c->raw.mid(int(qMax<qint64>(0, qint64(c->parser.errorOffset()) - qint64(c->rawBase) - 8)), 32))));
            c->sock->disconnectFromHost();
            return;
        }
        if (st == StreamParser::Greeting) {
            clientLog(c, QStringLiteral("RECV greeting"));
            continue;
        }
        const QByteArray rawMsg = c->raw.mid(int(before - c->rawBase), int(c->parser.consumed() - before));
        clientLog(c, QStringLiteral("REQ  %1").arg(QString::fromStdString(describe(m))));

        ++stats_.requests;
        const HandlerResult hr = handler_->handle(m, c->session);
        if (hr.kind == HandlerResult::Unknown) logUnknown(c, m, rawMsg);
        if (hr.kind == HandlerResult::Unimplemented) {
            ++stats_.unimplementedRequests;
            clientLog(c, QStringLiteral("(documented but not implemented: type 0x%1)").arg(m.type, 4, 16, QLatin1Char('0')));
        }
        emit requestHandled(c->ip, m.type, int(hr.kind));

        std::vector<uint8_t> out;
        for (const Message& r : hr.replies) {
            encodeAppend(r, out);
            if (r.type != RepMenuItem) clientLog(c, QStringLiteral("REP  %1").arg(QString::fromStdString(describe(r))));
        }
        if (hr.replies.size() > 3) clientLog(c, QStringLiteral("(%1 reply messages)").arg(hr.replies.size()));
        if (!out.empty()) c->sock->write(reinterpret_cast<const char*>(out.data()), qint64(out.size()));
        if (hr.close) {
            c->sock->disconnectFromHost();
            return;
        }
    }
    // drop the bytes the parser has consumed
    const size_t used = c->parser.consumed() - c->rawBase;
    c->raw.remove(0, int(used));
    c->rawBase += used;
}

} // namespace vdj
