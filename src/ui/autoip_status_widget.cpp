#include "autoip_status_widget.h"
#include "network/auto_ip_manager.h"
#include <QLabel>
#include <QHBoxLayout>
#include <QNetworkInterface>
#include <QTimer>

namespace vdj {

AutoipStatusWidget::AutoipStatusWidget(AutoIpManager* manager, QWidget* parent)
    : QWidget(parent), manager_(manager)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);

    label_ = new QLabel("AutoIP: off", this);
    label_->setStyleSheet("color: #8a8a8a;");
    layout->addWidget(label_);

    auto* poll = new QTimer(this);
    poll->setInterval(2000);
    connect(poll, &QTimer::timeout, this, &AutoipStatusWidget::refreshExternal);
    poll->start();

    connect(manager_, &AutoIpManager::started, this,
            [this](const QString&, const QString& ip) {
        updateDisplay(ip, QString());
    });
    connect(manager_, &AutoIpManager::ipChanged, this,
            [this](const QString& ip) {
        updateDisplay(ip, QString());
    });
    connect(manager_, &AutoIpManager::stopped, this,
            [this](const QString&, const QString&) {
        label_->setText("AutoIP: off");
        label_->setStyleSheet("color: #8a8a8a;");
        setToolTip(QString());
        refreshExternal();
    });
    connect(manager_, &AutoIpManager::errorOccurred, this,
            [this](const QString& msg) {
        label_->setText("AutoIP: error");
        label_->setStyleSheet("color: #cc4444;");
        setToolTip(msg);
    });
}

void AutoipStatusWidget::setInterface(const QString& iface)
{
    iface_ = iface;
    refreshExternal();
}

// Not our helper's business who assigned the address: if the interface has one, the link-local setup works
void AutoipStatusWidget::refreshExternal()
{
    if (manager_->active() || iface_.isEmpty())
        return;
    QString found;
    for (const auto& e : QNetworkInterface::interfaceFromName(iface_).addressEntries()) {
        const QString a = e.ip().toString();
        if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && a.startsWith(QLatin1String("169.254."))) { found = a; break; }
    }
    if (!found.isEmpty()) {
        label_->setText("AutoIP: " + found);
        label_->setStyleSheet("color: #5a8a5a;");
        setToolTip(QStringLiteral("%1 on %2 (not managed by vdj-ui, e.g. NetworkManager link-local)").arg(found, iface_));
    } else if (label_->text() != QLatin1String("AutoIP: error")) {   // an error stays visible until something changes
        label_->setText("AutoIP: off");
        label_->setStyleSheet("color: #8a8a8a;");
        setToolTip(QString());
    }
}

void AutoipStatusWidget::updateDisplay(const QString& ip, const QString& error)
{
    if (!error.isEmpty()) {
        label_->setText("AutoIP: error");
        label_->setStyleSheet("color: #cc4444;");
        setToolTip(error);
    } else if (!ip.isEmpty()) {
        label_->setText("AutoIP: " + ip);
        label_->setStyleSheet("color: #5a8a5a;");
        setToolTip(ip);
    } else {
        label_->setText("AutoIP: starting\xe2\x80\xa6");
        label_->setStyleSheet("color: #8a8a5a;");
    }
}

} // namespace vdj
