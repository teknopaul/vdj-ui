#ifndef RB_NETIF_H
#define RB_NETIF_H

#include <QHostAddress>
#include <QString>

namespace vdj {

struct RbNetIf {
    QString      name;
    QHostAddress ip, broadcast;
    quint8       mac[6] = {0};
};

// Pick the interface that faces the decks: the named one, else the first up interface with a link-local (169.254.x.x)
// IPv4 address, else the first up non-loopback interface with an IPv4 address. (The same idea as the network dialog of
// the GUI, copied here so the dbserver library does not depend on the widgets code.)
bool pickInterface(const QString& wanted, RbNetIf* out, QString* error);

} // namespace vdj

#endif // RB_NETIF_H
