#ifndef NFS_OBSERVER_H
#define NFS_OBSERVER_H

#include <QHostAddress>
#include <QObject>
#include <QStringList>
#include <QTcpServer>
#include <QUdpSocket>

namespace vdj {

// Answers the minimum of ONC-RPC needed to see what a deck wants from a "rekordbox PC": it logs every portmap, mount and
// NFS call (program, procedure, decoded arguments such as the MNT path, LOOKUP name or READ range) and refuses all of them,
// so a deck that wants the audio over NFS shows its hand without ever being served a byte. It never sends file data.
//   portmap GETPORT -> our mount / nfs ports, portmap NULL -> ok, DUMP -> empty
//   mount   NULL -> ok, MNT -> status 13 (access denied)
//   nfs     NULL -> ok, every other procedure -> NFSERR_ACCES (13)
//   anything else -> PROG_UNAVAIL
class NfsObserver : public QObject {
    Q_OBJECT

public:
    struct Config {
        quint16 portmapPort = 111;        // needs root (or setcap cap_net_bind_service); 0 = any free port
        quint16 altPortmapPort = 50111;   // where the decks look for a rekordbox PC's portmapper (seen in captures); no root needed, 0 = off
        quint16 mountPort = 50112;        // fixed, advertised through GETPORT (a deck keeps using the port it was told); 0 = any free port
        quint16 nfsPort = 2049;
        QHostAddress bindAddress = QHostAddress::Any;
        QString logFile;                  // optional
    };

    explicit NfsObserver(QObject* parent = nullptr);
    ~NfsObserver() override;

    bool start(const Config& cfg, QString* warnings = nullptr);    // false only when nothing could be bound
    void stop();
    bool portmapActive() const { return portmap_.state() == QAbstractSocket::BoundState; }
    quint16 portmapPort() const { return portmap_.localPort(); }
    quint16 mountPort() const { return mount_.localPort(); }
    quint16 nfsPort() const { return nfs_.localPort(); }

    quint64 calls() const { return calls_; }
    QStringList mountPaths() const { return mountPaths_; }
    QStringList nfsFiles() const { return nfsFiles_; }      // LOOKUP names and READ targets seen

    // exposed for tests: decode one call, producing the reply bytes; returns false when it is not an RPC call
    static bool handleCall(const QByteArray& call, quint16 mountPort, quint16 nfsPort, QByteArray* reply, QString* description,
                           QString* mntPath = nullptr, QString* nfsName = nullptr);

signals:
    void event(const QString& line);
    void mountRequested(const QString& ip, const QString& path);
    void nfsRequested(const QString& ip, const QString& what);

private:
    void onDatagram(QUdpSocket* s, const char* what);
    void say(const QString& line);

    Config cfg_;
    QUdpSocket portmap_, altPortmap_, mount_, nfs_;
    QTcpServer tcpPortmap_, tcpNfs_;
    quint64 calls_ = 0;
    QStringList mountPaths_, nfsFiles_;
};

} // namespace vdj

#endif // NFS_OBSERVER_H
