#include "discovery_line.h"

namespace vdj {

QString discoveryTypeName(uint8_t type)
{
    switch (type) {
    case 0x00: return QStringLiteral("stage1-discovery");
    case 0x02: return QStringLiteral("id-use-req");
    case 0x03: return QStringLiteral("id-use-resp");
    case 0x04: return QStringLiteral("id-set-req");
    case 0x05: return QStringLiteral("id-set-resp");
    case 0x06: return QStringLiteral("keep-alive");
    case 0x08: return QStringLiteral("id-collision");
    case 0x0a: return QStringLiteral("discovery");
    default:   return QStringLiteral("unknown");
    }
}

QString formatDiscoveryLine(uint8_t type, int pid, bool known, const QString& ip, const QString& model, int suppressed)
{
    QString s = QStringLiteral("p%1%2 type=0x%3 %4 model=%5 ip=%6 known=%7")
                    .arg(pid)
                    .arg(pid > 32 ? QStringLiteral("(over backline)") : QString())
                    .arg(type, 2, 16, QLatin1Char('0'))
                    .arg(discoveryTypeName(type), model, ip.isEmpty() ? QStringLiteral("?") : ip)
                    .arg(known ? 1 : 0);
    if (suppressed > 0) s += QStringLiteral(" x%1").arg(suppressed);
    return s;
}

bool DiscoveryLineLimiter::allow(int pid, uint8_t type, qint64 nowMs, int* suppressed)
{
    Entry& e = seen_[pid * 256 + type];
    if (e.lastMs >= 0 && nowMs - e.lastMs < intervalMs_) {
        ++e.skipped;
        return false;
    }
    if (suppressed) *suppressed = e.skipped;
    e.skipped = 0;
    e.lastMs = nowMs;
    return true;
}

} // namespace vdj
