#include "network_config_dialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QNetworkInterface>
#include <QLabel>
#include <QInputDialog>
#include <QMessageBox>

namespace vdj {

NetworkConfigDialog::NetworkConfigDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("ProLink Network");
    auto* form = new QFormLayout(this);

    ifaceCombo_ = new QComboBox(this);
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const auto& iface : ifaces) {
        if (iface.flags().testFlag(QNetworkInterface::IsUp) &&
            !iface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            ifaceCombo_->addItem(iface.name());
        }
    }
    if (ifaceCombo_->count() == 0)
        ifaceCombo_->addItem("eth0");

    playerIdEdit_ = new QLineEdit("5", this);

    form->addRow("Interface", ifaceCombo_);
    form->addRow("Player ID", playerIdEdit_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    okButton_ = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

QString NetworkConfigDialog::chooseInterface(QWidget* parent, const QString& title, const QString& preselect)
{
    QStringList items;
    int current = 0;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        const QString n = iface.name();
        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack))
            continue;
        // virtual interfaces cannot be cabled to the decks
        static const char* kVirtual[] = { "docker", "veth", "br-", "virbr", "tun", "tap", "vnet", "lxc", "lxd" };
        bool skip = false;
        for (const char* v : kVirtual)
            if (n.startsWith(QLatin1String(v))) skip = true;
        if (skip)
            continue;
        QStringList addrs;
        for (const auto& e : iface.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
                addrs << e.ip().toString();
        const bool up = iface.flags().testFlag(QNetworkInterface::IsUp) && iface.flags().testFlag(QNetworkInterface::IsRunning);
        items << QStringLiteral("%1  (%2%3)").arg(n, up ? QStringLiteral("link up") : QStringLiteral("no link"),
                                                 addrs.isEmpty() ? QString() : QStringLiteral(", ") + addrs.join(QStringLiteral(", ")));
        if (n == preselect)
            current = items.size() - 1;
    }
    if (items.isEmpty()) {
        QMessageBox::warning(parent, title, QStringLiteral("No network interface found."));
        return QString();
    }
    bool ok = false;
    const QString picked = QInputDialog::getItem(parent, title,
        QStringLiteral("Network interface physically connected to the decks (via a switch or router):"),
        items, current, false, &ok);
    return ok ? picked.section(QLatin1Char(' '), 0, 0) : QString();
}

QString NetworkConfigDialog::interface() const
{
    return ifaceCombo_->currentText();
}

void NetworkConfigDialog::setInterface(const QString& iface)
{
    int idx = ifaceCombo_->findText(iface);
    if (idx >= 0) ifaceCombo_->setCurrentIndex(idx);
}

QString NetworkConfigDialog::playerIdText() const
{
    return playerIdEdit_->text();
}

} // namespace vdj
