#include "debug_panel.h"
#include "debug/packet_logger.h"
#include <QListWidget>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace vdj {

DebugPanel::DebugPanel(PacketLogger* logger, QWidget* parent)
    : QWidget(parent), logger_(logger)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* top = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("Packet Log"), this);
    filter_ = new QComboBox(this);
    filter_->addItems({ QStringLiteral("ALL"), QStringLiteral("UPDATE"),
                        QStringLiteral("BEAT"), QStringLiteral("DISCOVERY"),
                        QStringLiteral("USER"), QStringLiteral("NFS"),
                        QStringLiteral("SYS") });
    countLabel_ = new QLabel(this);
    top->addWidget(title);
    top->addStretch(1);
    top->addWidget(filter_);
    top->addWidget(countLabel_);
    layout->addLayout(top);

    list_ = new QListWidget(this);
    list_->setStyleSheet(QStringLiteral(
        "QListWidget { background:#0e0e0e; color:#9cf; "
        "font-family:monospace; font-size:10pt; }"));
    layout->addWidget(list_, 1);

    setMinimumSize(420, 220);

    connect(filter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        currentFilter_ = filter_->currentText();
        refresh();
    });
    connect(logger_, &PacketLogger::entryAdded, this, &DebugPanel::refresh);

    refresh();
}

void DebugPanel::refresh()
{
    list_->clear();
    const auto& entries = logger_->entries();
    int shown = 0;
    for (int i = entries.size() - 1; i >= 0; --i) {
        const PacketEntry& e = entries[i];
        if (currentFilter_ != QStringLiteral("ALL") && e.type != currentFilter_)
            continue;
        if (shown >= 500)
            break;
        QString line = e.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"))
                     + QStringLiteral(" [") + e.type + QStringLiteral("] ")
                     + e.detail;
        list_->addItem(line);
        shown++;
    }
    countLabel_->setText(QStringLiteral("%1 shown").arg(shown));
}

void DebugPanel::setFilter(const QString& type)
{
    int idx = filter_->findText(type);
    if (idx >= 0)
        filter_->setCurrentIndex(idx);
}

} // namespace vdj