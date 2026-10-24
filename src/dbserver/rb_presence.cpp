#include "rb_presence.h"

#include <cstring>

namespace vdj {

using namespace db;

RbPresence::RbPresence(QObject* parent) : QObject(parent)
{
    connect(&claimTimer_, &QTimer::timeout, this, &RbPresence::onClaimTimer);
    connect(&keepAliveTimer_, &QTimer::timeout, this, &RbPresence::onKeepAliveTimer);
    connect(&livenessTimer_, &QTimer::timeout, this, &RbPresence::onLivenessTimer);
    connect(&statusTimer_, &QTimer::timeout, this, &RbPresence::onStatusTimer);
    connect(&announceTimer_, &QTimer::timeout, this, &RbPresence::onAnnounceTimer);
    connect(&announce_, &QUdpSocket::readyRead, this, &RbPresence::onAnnounce);
    connect(&beat_, &QUdpSocket::readyRead, this, &RbPresence::onBeat);
    connect(&status_, &QUdpSocket::readyRead, this, &RbPresence::onStatus);
}

RbPresence::~RbPresence()
{
    stop();
}

bool RbPresence::start(const Config& cfg, QString* error)
{
    stop();
    cfg_ = cfg;
    const auto mode = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
    struct Sock { QUdpSocket* s; quint16 port; const char* what; } socks[] = {
        {&announce_, cfg.bindAnnounce, "announce"}, {&beat_, cfg.bindBeat, "beat"}, {&status_, cfg.bindStatus, "status"}};
    for (const Sock& s : socks) {
        if (!s.s->bind(cfg.bindAddress, s.port, mode)) {
            if (error) *error = QStringLiteral("cannot bind UDP %1 (%2): %3").arg(s.port).arg(s.what).arg(s.s->errorString());
            stop();
            return false;
        }
    }
    running_ = true;
    decks_.clear();
    counts_.clear();
    livenessTimer_.start(500);
    startClaim();
    return true;
}

void RbPresence::stop()
{
    claimTimer_.stop();
    keepAliveTimer_.stop();
    livenessTimer_.stop();
    statusTimer_.stop();
    announceTimer_.stop();
    announce_.close();
    beat_.close();
    status_.close();
    running_ = false;
    claimed_ = false;
}

void RbPresence::startClaim()
{
    claimed_ = false;
    claimStep_ = 0;
    keepAliveTimer_.stop();
    statusTimer_.stop();
    announceTimer_.stop();
    // The real rekordbox PC (capture prolink-w-pc-rekordbox): stage 1 x3, then 6 rounds of stage 2 cycling the
    // candidate numbers 17,18,41,42,43,44 (counter = round), about 0.11 s apart, no hello and no stage 3,
    // then keep-alives. The first candidate is the number we keep.
    candidates_.assign(1, cfg_.identity.device);
    for (uint8_t d : {uint8_t(18), uint8_t(41), uint8_t(42), uint8_t(43), uint8_t(44)})
        if (d != cfg_.identity.device) candidates_.push_back(d);
    log(QStringLiteral("claiming device number %1 as \"%2\"").arg(cfg_.identity.device).arg(QString::fromStdString(cfg_.identity.name)));
    onClaimTimer();
    claimTimer_.start(cfg_.claimIntervalMs);
}

void RbPresence::onClaimTimer()
{
    int total;
    if (cfg_.deckStyleClaim) {
        // 9 packets: stage 1 x3, stage 2 x3, stage 3 x3
        const int stage = claimStep_ / 3 + 1;
        const uint8_t counter = uint8_t(claimStep_ % 3 + 1);
        if (stage == 1) sendBroadcast(claimStage1(cfg_.identity, counter), cfg_.destAnnounce);
        else if (stage == 2) sendBroadcast(claimStage2(cfg_.identity, counter), cfg_.destAnnounce);
        else sendBroadcast(claimStage3(cfg_.identity, counter), cfg_.destAnnounce);
        total = 9;
    } else {
        const int n = int(candidates_.size());
        if (claimStep_ < 3) {
            sendBroadcast(claimStage1(cfg_.identity, uint8_t(claimStep_ + 1)), cfg_.destAnnounce);
        } else {
            const int i = claimStep_ - 3;
            Identity id = cfg_.identity;
            id.device = candidates_[size_t(i % n)];
            sendBroadcast(claimStage2(id, uint8_t(i / n + 1)), cfg_.destAnnounce);
        }
        total = 3 + n * 6;
    }
    if (++claimStep_ < total) return;

    claimTimer_.stop();
    claimed_ = true;
    log(QStringLiteral("device number %1 claimed").arg(cfg_.identity.device));
    emit claimedDevice(cfg_.identity.device);
    onKeepAliveTimer();
    keepAliveTimer_.start(cfg_.keepAliveMs);
    statusTimer_.start(cfg_.statusMs);
    if (cfg_.announceMs > 0) announceTimer_.start(cfg_.announceMs);
    // the real PC told each deck its computer name directly, 30 ms after its first keep-alive (not as a broadcast)
    for (const Deck& d : decks_) if (!d.lost) sendName(QHostAddress(d.ip));
}

void RbPresence::sendName(const QHostAddress& to)
{
    sendTo(to, cfg_.destStatus, namePacket(cfg_.identity));
}

void RbPresence::onAnnounceTimer()
{
    for (const Deck& d : decks_)
        if (!d.lost) sendTo(QHostAddress(d.ip), cfg_.destStatus, pollReply(cfg_.identity));
}

void RbPresence::onStatusTimer()
{
    sendBroadcast(pcStatus(cfg_.identity, cfg_.bpmX100), cfg_.destStatus);
}

void RbPresence::onKeepAliveTimer()
{
    // byte 0x30 counts the devices on the network including ourselves (real decks and the PC go 1, 2, 3 as devices join)
    Identity id = cfg_.identity;
    int peers = 1;
    for (const Deck& d : decks_) if (!d.lost) ++peers;
    id.keepAliveTail[0] = uint8_t(peers);
    sendBroadcast(keepAlive(id), cfg_.destAnnounce);
}

void RbPresence::sendBroadcast(const std::vector<uint8_t>& p, quint16 port)
{
    QUdpSocket& s = port == cfg_.destStatus ? status_ : announce_;
    s.writeDatagram(reinterpret_cast<const char*>(p.data()), qint64(p.size()), cfg_.broadcast, port);
}

void RbPresence::sendTo(const QHostAddress& to, quint16 port, const std::vector<uint8_t>& p)
{
    status_.writeDatagram(reinterpret_cast<const char*>(p.data()), qint64(p.size()), to, port);
}

bool RbPresence::isOwn(const QHostAddress& a) const
{
    const quint32 mine = (quint32(cfg_.identity.ip[0]) << 24) | (quint32(cfg_.identity.ip[1]) << 16) | (quint32(cfg_.identity.ip[2]) << 8) | cfg_.identity.ip[3];
    return a.toIPv4Address() == mine;
}

void RbPresence::count(const QHostAddress& from, quint16 port, int type)
{
    ++counts_[QStringLiteral("%1:%2 type 0x%3").arg(from.toString()).arg(port).arg(type, 2, 16, QLatin1Char('0'))];
}

void RbPresence::renumber()
{
    int next = cfg_.identity.device + 1;
    if (next > 30) next = 17;
    cfg_.identity.device = uint8_t(next);
    log(QStringLiteral("device number collision, trying %1").arg(next));
    emit collision(next);
    startClaim();
}

void RbPresence::onAnnounce()
{
    while (announce_.hasPendingDatagrams()) {
        QByteArray d;
        d.resize(int(announce_.pendingDatagramSize()));
        QHostAddress from;
        quint16 port = 0;
        announce_.readDatagram(d.data(), d.size(), &from, &port);
        const auto* p = reinterpret_cast<const uint8_t*>(d.constData());
        if (isOwn(from) || !hasHeader(p, size_t(d.size()))) continue;
        count(from, kPortAnnounce, p[10]);

        KeepAliveInfo k;
        ClaimInfo c;
        if (parseKeepAlive(p, size_t(d.size()), &k)) {
            if (k.device == cfg_.identity.device && claimed_) {      // somebody else uses our number
                renumber();
                continue;
            }
            const QString ip = from.toString();
            Deck& deck = decks_[ip];
            const bool fresh = deck.ip.isEmpty() || deck.lost;
            deck.ip = ip;
            deck.name = QString::fromStdString(k.name);
            deck.device = k.device;
            deck.deviceType = k.deviceType;
            if (deck.firstSeen.isNull()) deck.firstSeen = QDateTime::currentDateTime();
            deck.lastSeen = QDateTime::currentDateTime();
            deck.lost = false;
            if (fresh) {
                log(QStringLiteral("deck %1 \"%2\" device %3 seen").arg(ip, deck.name).arg(k.device));
                emit deckSeen(ip, deck.name, k.device);
                if (claimed_) sendName(from);                                              // tell the new deck our name
            }
        } else if (parseClaim(p, size_t(d.size()), &c) && c.stage >= 2 && c.device == cfg_.identity.device) {
            renumber();
        }
    }
}

void RbPresence::onBeat()
{
    while (beat_.hasPendingDatagrams()) {
        QByteArray d;
        d.resize(int(beat_.pendingDatagramSize()));
        QHostAddress from;
        quint16 port = 0;
        beat_.readDatagram(d.data(), d.size(), &from, &port);
        const auto* p = reinterpret_cast<const uint8_t*>(d.constData());
        if (isOwn(from) || !hasHeader(p, size_t(d.size()))) continue;
        count(from, kPortBeat, p[10]);
    }
}

void RbPresence::onStatus()
{
    while (status_.hasPendingDatagrams()) {
        QByteArray d;
        d.resize(int(status_.pendingDatagramSize()));
        QHostAddress from;
        quint16 port = 0;
        status_.readDatagram(d.data(), d.size(), &from, &port);
        const auto* p = reinterpret_cast<const uint8_t*>(d.constData());
        if (isOwn(from) || !hasHeader(p, size_t(d.size()))) continue;
        count(from, kPortStatus, p[10]);

        PollInfo poll;
        if (parsePoll(p, size_t(d.size()), &poll)) {
            // the deck asks a new device for a sign of life; the real PC answered with a type-0x16 packet carrying its number
            log(QStringLiteral("poll from %1 (device %2): %3").arg(from.toString()).arg(poll.device).arg(claimed_ ? "answering" : "ignored, not claimed yet"));
            if (claimed_) sendTo(from, cfg_.destStatus, pollReply(cfg_.identity));
            continue;
        }
        MediaQuery q;
        if (parseMediaQuery(p, size_t(d.size()), &q)) {
            const bool ours = q.targetDevice == cfg_.identity.device && q.slot == cfg_.media.slot && claimed_;
            log(QStringLiteral("media query from %1 (device %2) for device %3 slot %4: %5")
                    .arg(from.toString()).arg(q.askerDevice).arg(q.targetDevice).arg(q.slot).arg(ours ? "answering" : "ignored"));
            emit mediaQueried(from.toString(), q.slot, ours);
            if (ours) sendTo(from, cfg_.destStatus, mediaResponse(cfg_.identity, cfg_.media));
        }
    }
}

void RbPresence::onLivenessTimer()
{
    const QDateTime now = QDateTime::currentDateTime();
    for (auto it = decks_.begin(); it != decks_.end(); ++it) {
        Deck& d = it.value();
        if (!d.lost && d.lastSeen.msecsTo(now) > cfg_.deckTimeoutMs) {
            d.lost = true;
            log(QStringLiteral("deck %1 \"%2\" lost (no keep-alive for %3 ms)").arg(d.ip, d.name).arg(cfg_.deckTimeoutMs));
            emit deckLost(d.ip);
        }
    }
}

} // namespace vdj
