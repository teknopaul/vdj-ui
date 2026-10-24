#include "rb_netif.h"

#include <QNetworkInterface>

namespace vdj {

namespace {

bool usable(const QNetworkInterface& i)
{
    return i.flags().testFlag(QNetworkInterface::IsUp) && i.flags().testFlag(QNetworkInterface::IsRunning) &&
           !i.flags().testFlag(QNetworkInterface::IsLoopBack);
}

bool fill(const QNetworkInterface& i, RbNetIf* out, bool linkLocalOnly)
{
    for (const QNetworkAddressEntry& e : i.addressEntries()) {
        if (e.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
        if (linkLocalOnly && (e.ip().toIPv4Address() >> 16) != 0xa9fe) continue;
        out->name = i.name();
        out->ip = e.ip();
        out->broadcast = e.broadcast().isNull() ? QHostAddress::Broadcast : e.broadcast();
        const QStringList parts = i.hardwareAddress().split(':');
        for (int k = 0; k < 6; ++k) out->mac[k] = k < parts.size() ? quint8(parts[k].toUInt(nullptr, 16)) : 0;
        return true;
    }
    return false;
}

} // namespace

bool pickInterface(const QString& wanted, RbNetIf* out, QString* error)
{
    const auto all = QNetworkInterface::allInterfaces();
    if (!wanted.isEmpty()) {
        for (const auto& i : all)
            if (i.name() == wanted && (fill(i, out, true) || fill(i, out, false))) return true;   // decks live on 169.254/16: prefer it when the interface also has a routed address
        if (error) *error = QStringLiteral("interface %1 has no IPv4 address").arg(wanted);
        return false;
    }
    for (const auto& i : all)
        if (usable(i) && fill(i, out, true)) return true;
    for (const auto& i : all)
        if (usable(i) && fill(i, out, false)) return true;
    if (error) *error = QStringLiteral("no network interface with an IPv4 address is up");
    return false;
}

} // namespace vdj
