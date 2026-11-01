#include "trakcrate_jobs_strip.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace vdj {

TrakcrateJobsStrip::TrakcrateJobsStrip(QWidget* parent) : QWidget(parent)
{
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(2);
    hide();
}

void TrakcrateJobsStrip::addJob(int id, const QString& title)
{
    if (rows_.contains(id)) return;
    Row r;
    r.widget = new QWidget;
    auto* h = new QHBoxLayout(r.widget);
    h->setContentsMargins(0, 0, 0, 0);
    r.title = new QLabel(title);
    r.title->setMinimumWidth(160);
    r.bar = new QProgressBar;
    r.bar->setMaximumWidth(220);
    r.bar->setRange(0, 0);
    r.current = new QLabel;
    r.cancel = new QPushButton(QStringLiteral("Cancel"));
    r.cancel->setToolTip(QStringLiteral("Stop writing to %1").arg(title));
    h->addWidget(r.title);
    h->addWidget(r.bar);
    h->addWidget(r.current, 1);
    h->addWidget(r.cancel);
    connect(r.cancel, &QPushButton::clicked, this, [this, id]() { emit cancelRequested(id); });
    layout_->addWidget(r.widget);
    r.widget->show();
    rows_.insert(id, r);
    show();
}

void TrakcrateJobsStrip::setProgress(int id, int done, int total, const QString& current)
{
    auto it = rows_.find(id);
    if (it == rows_.end()) return;
    it->bar->setRange(0, total > 0 ? total : 0);
    it->bar->setValue(done);
    if (!current.isEmpty()) it->current->setText(current);
}

void TrakcrateJobsStrip::setCancellable(int id, bool cancellable)
{
    auto it = rows_.find(id);
    if (it != rows_.end()) it->cancel->setVisible(cancellable);
}

void TrakcrateJobsStrip::removeJob(int id)
{
    auto it = rows_.find(id);
    if (it == rows_.end()) return;
    it->widget->hide();
    it->widget->deleteLater();
    rows_.erase(it);
    if (rows_.isEmpty()) hide();
}

} // namespace vdj
