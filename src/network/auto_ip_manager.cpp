#include "auto_ip_manager.h"

#include <QProcess>
#include <QTimer>
#include <QPointer>
#include <QRegularExpression>
#include <QNetworkInterface>
#include <QFile>
#include <QDebug>
#include <QStandardPaths>

#include <unistd.h>

namespace vdj {

// Runs as root via pkexec. Mirrors adj/bin/autoip: starts avahi-autoipd, saves
// the claimed 169.254.x.x address, and re-adds it if the daemon loses it. The
// loop stops when /tmp/vdj-autoip-stop appears; the EXIT trap then kills the
// daemon and removes only the link-local address it managed.
static const char kMaintainerScript[] = R"SH(
set -u
iface="$1"
stopfile=/tmp/vdj-autoip-stop
datfile=/var/tmp/autoip.dat
pidfile=/var/tmp/autoip-maintain.pid

rm -f "$stopfile" "$datfile"

if ! command -v avahi-autoipd >/dev/null 2>&1; then
  echo "avahi-autoipd is not installed" >&2
  exit 127
fi

avahi-autoipd "$iface" 2>&1 | awk '/^Successfully claimed IP/ {print $5; fflush()}' > "$datfile" &
avahi_pid=$!
echo $$ > "$pidfile"

cleanup() {
  kill "$avahi_pid" 2>/dev/null
  avahi-autoipd -k "$iface" 2>/dev/null
  ip_addr=$(cat "$datfile" 2>/dev/null)
  if [ -n "$ip_addr" ]; then
    ip addr del "$ip_addr/16" dev "$iface" 2>/dev/null
  fi
  rm -f "$pidfile" "$datfile" "$stopfile"
  exit 0
}
trap cleanup EXIT INT TERM

ip link set "$iface" up 2>/dev/null

# avahi-autoipd claims an address but its own callout script can fail (and the address drops at random),
# so add the claimed address ourselves whenever the interface does not carry it.
while true; do
  [ -e "$stopfile" ] && break
  fip=$(cat "$datfile" 2>/dev/null)
  if [ -z "$fip" ]; then
    # the daemon's log line can go missing (it logs to syslog on some setups), but its process title always names
    # the address once it is announcing / running: "avahi-autoipd: [eth0] announcing 169.254.8.230"
    fip=$(ps -eo args= | sed -n "s/^avahi-autoipd: \\[$iface\\] \(announcing\|running\) \(169\.254\.[0-9]*\.[0-9]*\).*/\2/p" | head -1)
    [ -n "$fip" ] && echo "$fip" > "$datfile"
  fi
  if [[ "$fip" =~ ^169\.254\.[0-9]+\.[0-9]+$ ]]; then
    if ! ip -4 -o addr show dev "$iface" | grep -q " $fip/"; then
      ip addr add "$fip/16" broadcast 169.254.255.255 dev "$iface"
    fi
  fi
  sleep 1
done
)SH";

// Runs as the user: enabling link-local on the profile that carries the interface, then re-activating it
static const char kNmScript[] = R"SH(
iface="$1"
con=$(nmcli -g GENERAL.CONNECTION dev show "$iface" 2>/dev/null)
if [ -z "$con" ] || [ "$con" = "--" ]; then
  echo "NetworkManager has no active connection on $iface (cable connected?)" >&2
  exit 2
fi
nmcli con mod "$con" ipv4.link-local enabled || exit 3
nmcli con up "$con" || exit 4
)SH";

// The profile that carries the interface is changed (or one is made); the last line printed is the profile name.
static const char kPermanentScript[] = R"SH(
iface="$1"
con=$(nmcli -g GENERAL.CONNECTION dev show "$iface" 2>/dev/null)
if [ -z "$con" ] || [ "$con" = "--" ]; then
  con="vdj-prolink-$iface"
  nmcli con add type ethernet ifname "$iface" con-name "$con" ipv4.method link-local ipv6.method disabled connection.autoconnect yes || exit 3
else
  nmcli con mod "$con" ipv4.method link-local ipv6.method disabled connection.autoconnect yes connection.interface-name "$iface" || exit 3
fi
nmcli con up "$con" || exit 4
echo "$con"
)SH";

QString AutoIpManager::permanentCommands(const QString& iface)
{
    return QStringLiteral("nmcli con mod <the profile of %1> ipv4.method link-local ipv6.method disabled connection.autoconnect yes connection.interface-name %1\n"
                          "(or, when %1 has no profile: nmcli con add type ethernet ifname %1 con-name vdj-prolink-%1 ipv4.method link-local ...)\n"
                          "nmcli con up <the profile>").arg(iface);
}

void AutoIpManager::makePermanent(const QString& iface)
{
    if (permProc_) return;
    if (iface.isEmpty() || !networkManagerAvailable()) {
        emit permanentDone(false, QStringLiteral("nmcli (NetworkManager) is not available"));
        return;
    }
    permProc_ = new QProcess(this);
    permProc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(permProc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc = QPointer<QProcess>(permProc_)](int code, QProcess::ExitStatus) {
        if (!proc) return;
        const QString out = QString::fromLocal8Bit(proc->readAll()).trimmed();
        proc->deleteLater();
        permProc_ = nullptr;
        if (code == 0) emit permanentDone(true, out.section(QLatin1Char('\n'), -1));
        else emit permanentDone(false, out.isEmpty() ? QStringLiteral("nmcli failed (exit %1)").arg(code) : out);
    });
    connect(permProc_, &QProcess::errorOccurred, this, [this, proc = QPointer<QProcess>(permProc_)](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || !proc) return;
        proc->deleteLater();
        permProc_ = nullptr;
        emit permanentDone(false, QStringLiteral("cannot run /bin/sh"));
    });
    permProc_->start(QStringLiteral("/bin/sh"), { QStringLiteral("-c"), QString::fromLatin1(kPermanentScript), QStringLiteral("vdj-autoip-permanent"), iface });
}

AutoIpManager::AutoIpManager(QObject* parent)
    : QObject(parent)
{
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(400);
    connect(pollTimer_, &QTimer::timeout, this, &AutoIpManager::readClaimedIp);
}

AutoIpManager::~AutoIpManager()
{
    stop();
}

void AutoIpManager::start(const QString& iface)
{
    if (active_) {
        if (iface == iface_)
            return;
        stop();
    }
    if (iface.isEmpty()) {
        lastError_ = QStringLiteral("AutoIP: no network interface selected");
        emit errorOccurred(lastError_);
        return;
    }

    // Fail early and visibly: without the daemon nothing would ever claim an address.
    if (QStandardPaths::findExecutable(QStringLiteral("avahi-autoipd"),
            { QStringLiteral("/usr/sbin"), QStringLiteral("/sbin"), QStringLiteral("/usr/bin"),
              QStringLiteral("/usr/local/sbin") }).isEmpty()) {
        lastError_ = QStringLiteral("AutoIP needs avahi-autoipd: sudo apt install avahi-autoipd");
        emit errorOccurred(lastError_);
        return;
    }

    iface_ = iface;
    ip_.clear();
    active_ = true;
    stopped_ = false;
    startedOnce_ = false;
    lastError_.clear();

    // Ensure no stale marker survives from a previous run.
    ::unlink(kStopFile);

    const QStringList args = {
        QStringLiteral("/bin/bash"),
        QStringLiteral("-c"),
        QString::fromLatin1(kMaintainerScript),
        QStringLiteral("vdj-autoip"),
        iface,
    };

    runner_ = new QProcess(this);
    connect(runner_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        onRunnerFinished(exitCode);
    });
    runner_->start(QStringLiteral("/usr/bin/pkexec"), args);
    if (runner_->waitForStarted(3000)) {
        pollTimer_->start();
        // avahi-autoipd takes a few seconds to probe; give up if nothing was claimed by then
        QTimer::singleShot(40000, this, [this, runner = QPointer<QProcess>(runner_)]() {
            if (active_ && !startedOnce_ && runner == runner_) {
                const QString iface = iface_;
                stop();
                lastError_ = QStringLiteral("AutoIP: no 169.254.x.x address on %1 (is the cable connected?)").arg(iface);
                if (networkManagerAvailable())
                    emit noAddress(iface);
                else
                    emit errorOccurred(lastError_);
            }
        });
    } else {
        onRunnerFinished(runner_->exitCode());
    }
}

bool AutoIpManager::networkManagerAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("nmcli")).isEmpty();
}

void AutoIpManager::startViaNetworkManager(const QString& iface)
{
    if (active_)
        stop();
    if (iface.isEmpty() || !networkManagerAvailable()) {
        lastError_ = QStringLiteral("AutoIP: nmcli (NetworkManager) is not available");
        emit errorOccurred(lastError_);
        return;
    }
    iface_ = iface;
    ip_.clear();
    active_ = true;
    stopped_ = false;
    startedOnce_ = false;
    viaNm_ = true;
    lastError_.clear();

    nmProc_ = new QProcess(this);
    nmProc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(nmProc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc = QPointer<QProcess>(nmProc_)](int code, QProcess::ExitStatus) {
        if (!proc) return;
        const QString out = QString::fromLocal8Bit(proc->readAll()).trimmed();
        proc->deleteLater();
        nmProc_ = nullptr;
        if (code != 0 && active_) {
            lastError_ = QStringLiteral("nmcli failed on %1: %2").arg(iface_, out.isEmpty() ? QStringLiteral("exit %1").arg(code) : out);
            emit errorOccurred(lastError_);
            stop();
        }
    });
    nmProc_->start(QStringLiteral("/bin/sh"), { QStringLiteral("-c"), QString::fromLatin1(kNmScript), QStringLiteral("vdj-autoip-nm"), iface });
    pollTimer_->start();
    // activating a profile can wait for DHCP to time out before the link-local fallback applies
    QTimer::singleShot(60000, this, [this, iface]() {
        if (active_ && viaNm_ && !startedOnce_ && iface == iface_) {
            lastError_ = QStringLiteral("AutoIP: NetworkManager did not assign a 169.254.x.x address on %1").arg(iface);
            stop();
            emit errorOccurred(lastError_);
        }
    });
}

void AutoIpManager::stop()
{
    if (!active_) {
        return;
    }
    stopped_ = true;
    active_ = false;
    pollTimer_->stop();

    if (viaNm_) {
        viaNm_ = false;
        if (nmProc_) {
            nmProc_->disconnect(this);
            nmProc_->kill();
            nmProc_->deleteLater();
            nmProc_ = nullptr;
        }
        emit stopped(iface_, ip_);
        iface_.clear();
        ip_.clear();
        return;
    }

    // The stop marker makes the privileged maintainer tear the address down
    // and exit; SIGTERM as well so it does not wait for the next poll.
    QFile marker(kStopFile);
    marker.open(QIODevice::WriteOnly | QIODevice::Truncate);
    marker.write("stop\n");
    marker.close();

    if (runner_ && runner_->state() != QProcess::NotRunning) {
        runner_->terminate();
        if (!runner_->waitForFinished(5000))
            runner_->kill();
    }
    if (runner_) {
        runner_->deleteLater();
        runner_ = nullptr;
    }

    emit stopped(iface_, ip_);
    iface_.clear();
    ip_.clear();
}

void AutoIpManager::readClaimedIp()
{
    QFile f(QString::fromLatin1(kDatFile));
    QString content;
    if (f.open(QIODevice::ReadOnly)) {
        content = QString::fromLatin1(f.readAll()).trimmed();
        f.close();
    }

    QString claimed;
    const QStringList tokens = content.split(QRegularExpression(QStringLiteral("\\s+")),
                                             Qt::SkipEmptyParts);
    for (const QString& tok : tokens) {
        if (tok.startsWith(QStringLiteral("169.254."))) {
            claimed = tok;
            break;
        }
    }
    if (claimed.isEmpty()) {
        // the daemon's own output can lag or be lost: trust the interface itself
        for (const auto& e : QNetworkInterface::interfaceFromName(iface_).addressEntries()) {
            const QString a = e.ip().toString();
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && a.startsWith(QStringLiteral("169.254."))) {
                claimed = a;
                break;
            }
        }
    }
    if (claimed.isEmpty() || claimed == ip_)
        return;

    ip_ = claimed;
    emit ipChanged(ip_);
    if (!startedOnce_) {
        startedOnce_ = true;
        emit started(iface_, ip_);
    }
}

void AutoIpManager::onRunnerFinished(int exitCode)
{
    pollTimer_->stop();

    if (stopped_) {
        return;
    }
    if (runner_) {
        runner_->deleteLater();
        runner_ = nullptr;
    }

    // The helper can end on its own (avahi-autoipd died, or ifaces went away)
    // or pkexec can refuse (no polkit agent / user cancelled). Either way the
    // autoip address is no longer being maintained.
    active_ = false;
    QString ip = ip_;
    ip_.clear();

    if (exitCode != 0) {
        lastError_ = exitCode == 126 ? QStringLiteral("AutoIP: authorisation cancelled or refused")
                   : exitCode == 127 ? QStringLiteral("AutoIP needs avahi-autoipd: sudo apt install avahi-autoipd")
                   : QStringLiteral("AutoIP exited (code %1)").arg(exitCode);
        emit errorOccurred(lastError_);
    }
    emit stopped(iface_, ip);
    iface_.clear();
}

} // namespace vdj