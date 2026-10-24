#include "rekordbox_mode_panel.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "config/config_manager.h"

namespace vdj {

namespace {
const QString kSection = QStringLiteral("rekordbox_mode");
}

RekordboxModePanel::RekordboxModePanel(ConfigManager* config, QWidget* parent) : QWidget(parent), config_(config)
{
    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(QStringLiteral("Pretend to be a rekordbox PC and serve a local export to the decks. "
                                            "Experimental: do not run together with a normal link connection or ADJ."), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* form = new QFormLayout;
    exportEdit_ = new QLineEdit(this);
    exportEdit_->setPlaceholderText(QStringLiteral("folder with PIONEER/rekordbox/export.pdb"));
    auto* browse = new QPushButton(QStringLiteral("Browse..."), this);
    auto* exportRow = new QHBoxLayout;
    exportRow->addWidget(exportEdit_, 1);
    exportRow->addWidget(browse);
    form->addRow(QStringLiteral("Export folder"), exportRow);

    ifaceCombo_ = new QComboBox(this);
    ifaceCombo_->addItem(QStringLiteral("(automatic: link-local)"), QString());
    for (const auto& i : QNetworkInterface::allInterfaces())
        if (!i.flags().testFlag(QNetworkInterface::IsLoopBack)) ifaceCombo_->addItem(i.name(), i.name());
    form->addRow(QStringLiteral("Interface"), ifaceCombo_);

    deviceSpin_ = new QSpinBox(this);
    deviceSpin_->setRange(7, 254);
    deviceSpin_->setValue(17);
    form->addRow(QStringLiteral("Device number"), deviceSpin_);

    nameEdit_ = new QLineEdit(this);
    nameEdit_->setPlaceholderText(QStringLiteral("this computer's host name"));
    form->addRow(QStringLiteral("Name shown on decks"), nameEdit_);

    policyCombo_ = new QComboBox(this);
    policyCombo_->addItem(QStringLiteral("answer with an empty result"), QStringLiteral("empty"));
    policyCombo_->addItem(QStringLiteral("ignore (no reply)"), QStringLiteral("ignore"));
    policyCombo_->addItem(QStringLiteral("close the connection"), QStringLiteral("close"));
    form->addRow(QStringLiteral("Unknown requests"), policyCombo_);

    logEdit_ = new QLineEdit(this);
    auto* logBrowse = new QPushButton(QStringLiteral("Browse..."), this);
    auto* logRow = new QHBoxLayout;
    logRow->addWidget(logEdit_, 1);
    logRow->addWidget(logBrowse);
    form->addRow(QStringLiteral("Log folder"), logRow);
    root->addLayout(form);

    startBtn_ = new QPushButton(QStringLiteral("Start"), this);
    statusLabel_ = new QLabel(QStringLiteral("stopped"), this);
    countersLabel_ = new QLabel(this);
    auto* ctl = new QHBoxLayout;
    ctl->addWidget(startBtn_);
    ctl->addWidget(statusLabel_, 1);
    root->addLayout(ctl);
    root->addWidget(countersLabel_);

    decksTable_ = new QTableWidget(0, 4, this);
    decksTable_->setHorizontalHeaderLabels({QStringLiteral("Deck IP"), QStringLiteral("Name"), QStringLiteral("Device"), QStringLiteral("Last seen")});
    decksTable_->horizontalHeader()->setStretchLastSection(true);
    decksTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    decksTable_->setMaximumHeight(110);
    root->addWidget(decksTable_);

    auto* unknownBox = new QGroupBox(QStringLiteral("Unknown requests (double-click for the raw bytes)"), this);
    auto* ub = new QVBoxLayout(unknownBox);
    unknownList_ = new QListWidget(unknownBox);
    unknownList_->setMaximumHeight(110);
    auto* copyBtn = new QPushButton(QStringLiteral("Copy unknown requests"), unknownBox);
    ub->addWidget(unknownList_);
    ub->addWidget(copyBtn);
    root->addWidget(unknownBox);

    logView_ = new QPlainTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(2000);
    logView_->setMinimumHeight(120);
    root->addWidget(logView_, 1);

    connect(browse, &QPushButton::clicked, this, &RekordboxModePanel::browseExport);
    connect(logBrowse, &QPushButton::clicked, this, &RekordboxModePanel::browseLogDir);
    connect(startBtn_, &QPushButton::clicked, this, &RekordboxModePanel::onStartStop);
    connect(copyBtn, &QPushButton::clicked, this, &RekordboxModePanel::copyUnknown);
    connect(unknownList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { showUnknown(unknownList_->currentRow()); });
    connect(&mode_, &RbMode::logLine, this, &RekordboxModePanel::addLog);
    connect(&mode_.server(), &DbServer::unknownRequest, this, &RekordboxModePanel::addUnknown);
    connect(&mode_, &RbMode::runningChanged, this, [this](bool r) { setRunningUi(r); emit runningChanged(r); });

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(1000);
    connect(refreshTimer_, &QTimer::timeout, this, &RekordboxModePanel::refresh);

    loadSettings();
}

RekordboxModePanel::~RekordboxModePanel()
{
    mode_.stop();
}

// ---- options and settings ------------------------------------------------------------------------------------------

void RekordboxModePanel::applyOptions(const RbModeOptions& o)
{
    if (!o.exportRoot.isEmpty()) exportEdit_->setText(o.exportRoot);
    if (!o.iface.isEmpty()) {
        const int i = ifaceCombo_->findData(o.iface);
        if (i >= 0) ifaceCombo_->setCurrentIndex(i);
    }
    deviceSpin_->setValue(o.device);
    if (!o.computerName.isEmpty()) nameEdit_->setText(o.computerName);
    if (!o.logDir.isEmpty()) logEdit_->setText(o.logDir);
    const int p = policyCombo_->findData(unknownPolicyName(o.unknownPolicy));
    if (p >= 0) policyCombo_->setCurrentIndex(p);
}

RbModeOptions RekordboxModePanel::options() const
{
    RbModeOptions o = base_;
    o.exportRoot = exportEdit_->text().trimmed();
    o.iface = ifaceCombo_->currentData().toString();
    o.device = deviceSpin_->value();
    o.computerName = nameEdit_->text().trimmed();
    o.logDir = logEdit_->text().trimmed();
    unknownPolicyFromName(policyCombo_->currentData().toString(), &o.unknownPolicy);
    return o;
}

void RekordboxModePanel::loadSettings()
{
    logEdit_->setText(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/rbmode"));
    if (!config_) return;
    exportEdit_->setText(config_->loadValue(kSection, QStringLiteral("export_root")));
    const QString iface = config_->loadValue(kSection, QStringLiteral("interface"));
    if (!iface.isEmpty() && ifaceCombo_->findData(iface) >= 0) ifaceCombo_->setCurrentIndex(ifaceCombo_->findData(iface));
    deviceSpin_->setValue(config_->loadValue(kSection, QStringLiteral("device"), QStringLiteral("17")).toInt());
    nameEdit_->setText(config_->loadValue(kSection, QStringLiteral("name")));
    const QString logDir = config_->loadValue(kSection, QStringLiteral("log_dir"));
    if (!logDir.isEmpty()) logEdit_->setText(logDir);
    const int p = policyCombo_->findData(config_->loadValue(kSection, QStringLiteral("unknown_policy"), QStringLiteral("empty")));
    if (p >= 0) policyCombo_->setCurrentIndex(p);
}

void RekordboxModePanel::saveSettings() const
{
    if (!config_) return;
    const RbModeOptions o = options();
    config_->saveValue(kSection, QStringLiteral("export_root"), o.exportRoot);
    config_->saveValue(kSection, QStringLiteral("interface"), o.iface);
    config_->saveValue(kSection, QStringLiteral("device"), QString::number(o.device));
    config_->saveValue(kSection, QStringLiteral("name"), o.computerName);
    config_->saveValue(kSection, QStringLiteral("log_dir"), o.logDir);
    config_->saveValue(kSection, QStringLiteral("unknown_policy"), unknownPolicyName(o.unknownPolicy));
}

// ---- start / stop --------------------------------------------------------------------------------------------------

void RekordboxModePanel::onStartStop()
{
    if (mode_.isRunning()) {
        stopMode();
        return;
    }
    QString err;
    if (!startMode(&err)) QMessageBox::warning(this, QStringLiteral("Rekordbox PC mode"), err);
}

bool RekordboxModePanel::startMode(QString* error)
{
    auto fail = [&](const QString& m) {
        if (error) *error = m;
        statusLabel_->setText(QStringLiteral("not started: ") + m);
        return false;
    };
    if (linkConnected_ && linkConnected_())
        return fail(QStringLiteral("the normal link connection (Network > Connect) is up; disconnect it first, both need UDP ports 50000-50002"));
    const RbModeOptions o = options();
    if (o.exportRoot.isEmpty()) return fail(QStringLiteral("choose the export folder (the one containing PIONEER/rekordbox/export.pdb)"));
    QString err;
    statusLabel_->setText(QStringLiteral("loading ..."));
    QApplication::processEvents();
    if (!mode_.start(o, &err)) return fail(err);
    saveSettings();
    refresh();
    return true;
}

void RekordboxModePanel::stopMode()
{
    mode_.stop();
    refresh();
}

void RekordboxModePanel::setRunningUi(bool running)
{
    startBtn_->setText(running ? QStringLiteral("Stop") : QStringLiteral("Start"));
    for (QWidget* w : {static_cast<QWidget*>(exportEdit_), static_cast<QWidget*>(ifaceCombo_), static_cast<QWidget*>(deviceSpin_),
                       static_cast<QWidget*>(nameEdit_), static_cast<QWidget*>(logEdit_)})
        w->setEnabled(!running);
    if (running) refreshTimer_->start(); else refreshTimer_->stop();
    statusLabel_->setText(running ? QStringLiteral("running as device %1 on %2 (%3), %4 tracks")
                                        .arg(mode_.presence().deviceNumber()).arg(mode_.localIp(), mode_.interfaceName())
                                        .arg(mode_.library().trackCount())
                                  : QStringLiteral("stopped"));
}

// ---- live view -----------------------------------------------------------------------------------------------------

void RekordboxModePanel::refresh()
{
    if (mode_.isRunning()) {
        statusLabel_->setText(QStringLiteral("running as device %1%2 on %3 (%4), %5 tracks")
                                  .arg(mode_.presence().deviceNumber())
                                  .arg(mode_.presence().claimed() ? QString() : QStringLiteral(" (claiming ...)"))
                                  .arg(mode_.localIp(), mode_.interfaceName()).arg(mode_.library().trackCount()));
    }
    const DbServer::Stats& st = mode_.server().stats();
    countersLabel_->setText(QStringLiteral("TCP: %1 connections (%2 open), %3 requests, %4 unknown, %5 not implemented, %6 port queries   |   NFS observer: %7 calls")
                                .arg(st.connections).arg(mode_.server().clientCount()).arg(st.requests).arg(st.unknownRequests)
                                .arg(st.unimplementedRequests).arg(st.portQueries).arg(mode_.nfs().calls()));
    const auto decks = mode_.presence().decks();
    decksTable_->setRowCount(decks.size());
    for (int r = 0; r < decks.size(); ++r) {
        const auto& d = decks[r];
        decksTable_->setItem(r, 0, new QTableWidgetItem(d.ip));
        decksTable_->setItem(r, 1, new QTableWidgetItem(d.name));
        decksTable_->setItem(r, 2, new QTableWidgetItem(QString::number(d.device)));
        decksTable_->setItem(r, 3, new QTableWidgetItem(d.lost ? QStringLiteral("LOST (%1)").arg(d.lastSeen.toString("hh:mm:ss"))
                                                                : d.lastSeen.toString("hh:mm:ss")));
    }
}

void RekordboxModePanel::addLog(const QString& line)
{
    logView_->appendPlainText(QDateTime::currentDateTime().toString("hh:mm:ss ") + line);
}

void RekordboxModePanel::addUnknown(const QString& ip, quint16 type, const QString& description, const QByteArray& raw)
{
    auto* item = new QListWidgetItem(QStringLiteral("%1  %2  type 0x%3  %4").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), ip)
                                         .arg(type, 4, 16, QLatin1Char('0')).arg(description));
    item->setData(Qt::UserRole, QString::fromLatin1(raw.toHex(' ')));
    unknownList_->addItem(item);
}

void RekordboxModePanel::showUnknown(int row)
{
    if (row < 0) return;
    QListWidgetItem* it = unknownList_->item(row);
    QMessageBox::information(this, QStringLiteral("Unknown request"), it->text() + QStringLiteral("\n\nraw bytes:\n") + it->data(Qt::UserRole).toString());
}

void RekordboxModePanel::copyUnknown()
{
    QStringList lines;
    for (int i = 0; i < unknownList_->count(); ++i)
        lines << unknownList_->item(i)->text() + QStringLiteral("\n    hex=") + unknownList_->item(i)->data(Qt::UserRole).toString();
    QApplication::clipboard()->setText(lines.join('\n'));
}

void RekordboxModePanel::browseExport()
{
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Select the rekordbox export folder"),
                                                        exportEdit_->text().isEmpty() ? QStringLiteral("/var/tmp") : exportEdit_->text());
    if (!d.isEmpty()) exportEdit_->setText(d);
}

void RekordboxModePanel::browseLogDir()
{
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Select the log folder"), logEdit_->text());
    if (!d.isEmpty()) logEdit_->setText(d);
}

QString RekordboxModePanel::statusText() const { return statusLabel_->text(); }
int RekordboxModePanel::unknownCount() const { return unknownList_->count(); }
int RekordboxModePanel::deckRows() const { return decksTable_->rowCount(); }

} // namespace vdj
