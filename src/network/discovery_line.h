#ifndef DISCOVERY_LINE_H
#define DISCOVERY_LINE_H

#include <QHash>
#include <QString>
#include <cstdint>

namespace vdj {

// The packet-log line for a packet on the discovery port (50000), and the rate limit that keeps a deck's 1.5 s keep-alives from flooding it.
// Pure: no libcdj, so it can be tested.

// Name of the discovery packet type byte (0x06 keep-alive, 0x02 id-use request, ... see libcdj cdj.h); "type 0xNN" when not known.
QString discoveryTypeName(uint8_t type);

// `p33(over backline) type=0x06 keep-alive model=DJM-900NXS2 ip=169.254.1.2 known=1 x4`
// ip empty -> "?"; suppressed > 0 -> " xN" (identical lines left out since the last one).
QString formatDiscoveryLine(uint8_t type, int pid, bool known, const QString& ip, const QString& model, int suppressed);

// At most one line per (pid, type) every `intervalMs`; `suppressed` is how many were left out before this one.
class DiscoveryLineLimiter {
public:
    explicit DiscoveryLineLimiter(qint64 intervalMs = 5000) : intervalMs_(intervalMs) {}
    bool allow(int pid, uint8_t type, qint64 nowMs, int* suppressed);
private:
    struct Entry { qint64 lastMs = -1; int skipped = 0; };
    qint64 intervalMs_;
    QHash<int, Entry> seen_;
};

} // namespace vdj

#endif // DISCOVERY_LINE_H
