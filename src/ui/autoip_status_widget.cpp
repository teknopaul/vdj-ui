#include "autoip_status_widget.h"
#include "network/auto_ip_manager.h"
#include <QLabel>
#include <QHBoxLayout>

namespace vdj {

AutoipStatusWidget::AutoipStatusWidget(AutoIpManager* manager, QWidget* parent)
    : QWidget(parent), manager_(manager)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);

    label_ = new QLabel("AutoIP: off", this);
    label_->setStyleSheet("color: #8a8a8a;");
    layout->addWidget(label_);

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
    });
    connect(manager_, &AutoIpManager::errorOccurred, this,
            [this](const QString& msg) {
        label_->setText("AutoIP: error");
        label_->setStyleSheet("color: #cc4444;");
        setToolTip(msg);
    });
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
