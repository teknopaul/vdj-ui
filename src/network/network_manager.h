#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <QObject>
#include <QString>
#include <QSet>
#include <cstdint>
#include <cmath>
#include <functional>

extern "C" {
#include <atomic>
#include <cdj/vdj.h>
#include <cdj/vdj_discovery.h>
}

namespace vdj {

struct LinkMemberInfo {
    uint8_t playerId = 0;
    float   bpm = 0.0f;
    int32_t pitch = 0;
    uint8_t barPos = 0;
    uint8_t active = 0;
    uint8_t masterState = 0;
    uint8_t playState = 0;
    bool    onair = false;
    QString ip;
};

class LinkExport;

class NetworkManager : public QObject {
    Q_OBJECT

public:
    explicit NetworkManager(QObject* parent = nullptr);
    ~NetworkManager() override;

    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    bool connectToLink(const QString& iface, uint32_t flags = VDJ_FLAG_AUTO_ID | VDJ_FLAG_DEV_CDJ);
    void disconnectFromLink();
    bool isConnected() const { return v_ != nullptr; }
    bool isJoining() const { return joining_; }     // the claim sequence is running, connected() is not emitted yet
    uint8_t localPlayerId() const;
    QString ipForPlayer(int playerId) const;
    // Called on the network thread for every beat packet; returns false when there is nothing to report.
    void setBeatProbe(std::function<bool(double*)> probe) { beatProbe_ = std::move(probe); }
    double sequencerOffsetNow() const
    {
        double ms = 0.0;
        return (beatProbe_ && beatProbe_(&ms)) ? ms : std::nan("");
    }

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);
    // What the join of the link is doing right now ("Claiming player number 2 (stage 2, packet 1 of 3)"); the real players
    // flash the number while this runs
    void discoveryPhase(const QString& text);
    void deckJoined(int playerId, const QString& ip, const QString& model);
    void deckLeft(int playerId);
    void statusUpdated(int playerId, double bpm, int pitch, int flags, int masterState, int active);
    // sequencerOffsetMs: where the deck's beat fell relative to the VDJ sequencer's beat, measured the moment the
    // packet arrived (NaN when no probe is set or the sequencer is not running)
    void beatReceived(int playerId, double bpm, int barPos, double sequencerOffsetMs);
    void masterChanged(int playerId);
    void linkExportLog(const QString& line);        // what the export does (media queries, dbserver, NFS)
    void linkExportChanged(bool running);
    void linkExportNeedsPort111();                  // the export runs but UDP 111 is not bound (needs privileges)

public slots:
    void setLocalBpm(double bpm);
    void setLocalPitch(double percentage);
    void setLocalPlaying(bool playing);
    void broadcastBeat(double bpm, int barPos);
    void requestMaster();
    void sendKeepalive();

    // LINK export (off by default): serve a pendrive / rekordbox export folder to the other players the way a deck serves its USB stick.
    // An empty folder switches it off. Takes effect at the next connect (or at once when connected).
    void setLinkExportFolder(const QString& folder);
    QString linkExportFolder() const { return linkExportFolder_; }
    bool linkExportRunning() const;
    // start sending our status to the link, once; called when a master is known or the wait times out
    void startStatusThread();
    // from the library's update thread: a packet on the status port that is not a status
    void rawStatusPortPacket(const QByteArray& packet);

private:
    static constexpr int kStatusStartTimeoutMs = 5000;
    friend void onDiscovery(vdj_t* v, cdj_discovery_packet_t* d_pkt);
    void teardown();
    void startLinkExport();
    void stopLinkExport();

    vdj_t* v_ = nullptr;
    vdj_discovery_ph discoveryPh_ = nullptr;
    vdj_discovery_unicast_ph discoveryUnicastPh_ = nullptr;
    vdj_update_ph updatePh_ = nullptr;
    vdj_beat_ph beatPh_ = nullptr;
    vdj_beat_unicast_ph beatUnicastPh_ = nullptr;
    QSet<int> knownMembers_;
    std::atomic<bool> statusStarted_{true};
    std::atomic<bool> joining_{false};   // vdj_exec_discovery() is running: deck events wait until connected() has been emitted
    std::function<bool(double*)> beatProbe_;
    QString linkExportFolder_;
    LinkExport* linkExport_ = nullptr;
};

} // namespace vdj

#endif // NETWORK_MANAGER_H
