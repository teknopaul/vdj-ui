#ifndef RB_PRESENCE_H
#define RB_PRESENCE_H

#include <QDateTime>
#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QTimer>
#include <vector>
#include <QUdpSocket>

#include "rb_packets.h"

namespace vdj {

// UDP presence of the pretend rekordbox PC: claims a device number (stage 1 x3, stage 2 over six candidate numbers x6, as the real PC does), then sends keep-alives,
// tells the decks its computer name, answers their media-slot queries and keeps a list of the decks it hears.
// Everything it hears is counted per source and packet type so unexpected deck behaviour shows up in the logs.
class RbPresence : public QObject {
    Q_OBJECT

public:
    struct Config {
        db::Identity identity;                          // mac, ip, device (17), names, rekordbox constants
        QHostAddress broadcast = QHostAddress::Broadcast;
        QHostAddress bindAddress = QHostAddress::AnyIPv4;
        db::MediaInfo media;                            // slot 4 description
        quint16 bindAnnounce = db::kPortAnnounce, bindBeat = db::kPortBeat, bindStatus = db::kPortStatus;
        quint16 destAnnounce = db::kPortAnnounce, destStatus = db::kPortStatus;     // where we send (tests redirect)
        int claimIntervalMs = 110;                      // pacing of the real rekordbox PC (a deck uses 300)
        bool deckStyleClaim = false;                    // true: hello-less deck sequence, stage 1/2/3 x 3; false: rekordbox PC sequence
        int keepAliveMs = 2000;
        int deckTimeoutMs = 10000;                      // a deck whose keep-alives stop for this long is lost
    };

    struct Deck {
        QString   ip;
        QString   name;
        int       device = 0;
        int       deviceType = 0;
        QDateTime firstSeen, lastSeen;
        bool      lost = false;
    };

    explicit RbPresence(QObject* parent = nullptr);
    ~RbPresence() override;

    bool start(const Config& cfg, QString* error = nullptr);
    void stop();
    bool isRunning() const { return running_; }
    bool claimed() const { return claimed_; }
    int  deviceNumber() const { return cfg_.identity.device; }
    void setMedia(const db::MediaInfo& m) { cfg_.media = m; }
    quint16 localAnnouncePort() const { return announce_.localPort(); }     // for tests that bind port 0
    quint16 localStatusPort() const { return status_.localPort(); }
    QList<Deck> decks() const { return decks_.values(); }
    QMap<QString, quint64> packetCounts() const { return counts_; }     // "ip:port type" -> count

signals:
    void claimedDevice(int device);
    void deckSeen(const QString& ip, const QString& name, int device);
    void deckLost(const QString& ip);
    void collision(int newDevice);
    void mediaQueried(const QString& ip, quint32 slot, bool answered);
    void logLine(const QString& line);

private slots:
    void onClaimTimer();
    void onKeepAliveTimer();
    void onLivenessTimer();
    void onAnnounce();
    void onBeat();
    void onStatus();

private:
    void startClaim();
    void sendBroadcast(const std::vector<uint8_t>& p, quint16 port);
    void sendTo(const QHostAddress& to, quint16 port, const std::vector<uint8_t>& p);
    bool isOwn(const QHostAddress& a) const;
    void count(const QHostAddress& from, quint16 port, int type);
    void renumber();
    void log(const QString& s) { emit logLine(s); }

    Config cfg_;
    bool running_ = false, claimed_ = false;
    int claimStep_ = 0;
    std::vector<uint8_t> candidates_;                   // device numbers tried in stage 2, first is the one we keep
    QUdpSocket announce_, beat_, status_;
    QTimer claimTimer_, keepAliveTimer_, livenessTimer_;
    QMap<QString, Deck> decks_;
    QMap<QString, quint64> counts_;
};

} // namespace vdj

#endif // RB_PRESENCE_H
