#include "session_panel.h"
#include "../session_log.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace vdj {

SessionPanel::SessionPanel(SessionLog* log, QWidget* parent)
    : QWidget(parent), log_(log)
{
    setMinimumWidth(240);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* topRow = new QHBoxLayout;
    djEdit_ = new QLineEdit(QStringLiteral("DJ 1"), this);
    auto* newSetBtn = new QPushButton(QStringLiteral("New Set"), this);
    topRow->addWidget(new QLabel(QStringLiteral("DJ:"), this));
    topRow->addWidget(djEdit_, 1);
    topRow->addWidget(newSetBtn);
    layout->addLayout(topRow);

    list_ = new QListWidget(this);
    list_->setAlternatingRowColors(true);
    layout->addWidget(list_, 1);

    auto* exportBtn = new QPushButton(QStringLiteral("Export…"), this);
    layout->addWidget(exportBtn);

    connect(newSetBtn,  &QPushButton::clicked, this, &SessionPanel::onNewSet);
    connect(exportBtn,  &QPushButton::clicked, this, &SessionPanel::onExport);
    connect(log_, &SessionLog::eventAdded, this, &SessionPanel::onEventAdded);
}

void SessionPanel::onNewSet()
{
    log_->newSet(djEdit_->text().trimmed().isEmpty()
                 ? QStringLiteral("DJ") : djEdit_->text().trimmed());
    list_->addItem(QStringLiteral("── New Set: %1 ──").arg(djEdit_->text()));
}

void SessionPanel::onExport()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Tracklist"),
        QString(), QStringLiteral("Text (*.txt);;CSV (*.csv);;All (*)"));
    if (path.isEmpty()) return;

    bool ok = path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)
              ? log_->exportCsv(path)
              : log_->exportText(path);

    if (!ok)
        QMessageBox::warning(this, QStringLiteral("Export Failed"),
                             QStringLiteral("Could not write to: ") + path);
}

void SessionPanel::onEventAdded(const PlayEvent& ev)
{
    if (ev.title.isEmpty()) return;  // newSet sentinel
    QString text = QStringLiteral("%1  [%2]  %3 — %4")
        .arg(ev.time.toString(QStringLiteral("HH:mm")))
        .arg(ev.deckId)
        .arg(ev.title, ev.artist);
    list_->addItem(text);
    list_->scrollToBottom();
}

} // namespace vdj
