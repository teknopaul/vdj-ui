#ifndef AUTO_IP_MANAGER_H
#define AUTO_IP_MANAGER_H

#include <QObject>

class QProcess;
class QTimer;

namespace vdj {

// Starts and stops the ProLink autoip helper. The helper claims an IPv4LL
// (169.254.0.0/16) address for the selected interface using avahi-autoipd
// and keeps the address alive if the daemon drops it, mirroring the logic
// of the old `adj/bin/autoip` shell script. A polkit GUI prompt (pkexec)
// supplies the root privileges; the helper runs as a subprocess.
class AutoIpManager : public QObject {
    Q_OBJECT

public:
    explicit AutoIpManager(QObject* parent = nullptr);
    ~AutoIpManager() override;

    void start(const QString& iface);
    void stop();

    // Fallback when avahi-autoipd did not deliver: let NetworkManager do the link-local address
    // (`nmcli con mod <profile> ipv4.link-local enabled` + `con up`), then watch the interface for it.
    // The profile keeps link-local enabled afterwards; stop() only stops watching.
    void startViaNetworkManager(const QString& iface);
    static bool networkManagerAvailable();

    bool active() const { return active_; }
    QString iface() const { return iface_; }
    QString ip() const { return ip_; }
    QString lastError() const { return lastError_; }

signals:
    void started(const QString& iface, const QString& ip);
    void stopped(const QString& iface, const QString& ip);
    void ipChanged(const QString& ip);
    void errorOccurred(const QString& message);
    // No 169.254.x.x address showed up on the interface in time (the helper has been stopped); the UI can offer nmcli
    void noAddress(const QString& iface);

private:
    void readClaimedIp();
    void onRunnerFinished(int exitCode);

    static constexpr const char* kStopFile = "/tmp/vdj-autoip-stop";
    static constexpr const char* kDatFile = "/var/tmp/autoip.dat";
    static constexpr const char* kPidFile = "/var/tmp/autoip-maintain.pid";

    QProcess* runner_ = nullptr;
    QTimer* pollTimer_ = nullptr;
    QString iface_;
    QString ip_;
    QString lastError_;
    bool active_ = false;
    bool stopped_ = false;
    bool startedOnce_ = false;
    bool viaNm_ = false;
    QProcess* nmProc_ = nullptr;
};

} // namespace vdj

#endif // AUTO_IP_MANAGER_H