#ifndef RB_MODE_OPTIONS_H
#define RB_MODE_OPTIONS_H

#include <QHostAddress>
#include <QString>
#include <QStringList>

#include "db_handler.h"

namespace vdj {

// Everything the pretend-rekordbox mode needs to start. The port fields default to the real protocol ports; tests
// override them with ephemeral ones.
struct RbModeOptions {
    QString  exportRoot;                 // folder with PIONEER/rekordbox/export.pdb
    QString  iface;                      // network interface name; empty = pick the link-local one
    QString  name = QStringLiteral("rekordbox");    // device name in the keep-alive
    QString  computerName;               // shown on the deck; empty = this host's name
    int      device = 17;
    QString  logDir;                     // empty = no log files
    db::UnknownPolicy unknownPolicy = db::UnknownPolicy::EmptyOk;

    // overrides for tests
    quint16  queryPort = 12523, dbPort = 1051;
    quint16  bindAnnounce = 50000, bindBeat = 50001, bindStatus = 50002;
    quint16  destAnnounce = 50000, destStatus = 50002;
    quint16  portmapPort = 111, mountPort = 0, nfsPort = 2049;      // mount 0 = any free port
    QHostAddress bindAddress = QHostAddress::Any;
    QHostAddress broadcastOverride;                                  // null = the interface's broadcast address
    QHostAddress ipOverride;                                         // null = the interface's address
    bool     checkPortFree = true;       // refuse to start when UDP 50000 is held by someone else
    bool     observeNfs = true;
    int      claimIntervalMs = 110, keepAliveMs = 2000, deckTimeoutMs = 10000;
};

// Parses: --rekordbox-mode <root> [--rb-iface IF] [--rb-name NAME] [--rb-device N] [--rb-log-dir DIR]
//         [--rb-unknown-policy ignore|empty|close]   (args = QCoreApplication::arguments(), program name first)
// Returns false (with *error) on a bad value; returns true with out->exportRoot empty when the mode was not requested.
bool parseRbModeArgs(const QStringList& args, RbModeOptions* out, QString* error);

QString unknownPolicyName(db::UnknownPolicy p);
bool    unknownPolicyFromName(const QString& s, db::UnknownPolicy* p);

} // namespace vdj

#endif // RB_MODE_OPTIONS_H
