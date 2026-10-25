#include "sample_chooser.h"
#include "sample_tree_model.h"
#include "audio/wav_info.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace vdj {

SampleChooser::SampleChooser(QWidget* parent)
    : QWidget(parent)
{
    name_ = new QLabel(this);
    name_->setMinimumWidth(60);
    name_->setTextInteractionFlags(Qt::NoTextInteraction);
    choose_ = new QToolButton(this);
    choose_->setText(QStringLiteral("..."));
    choose_->setToolTip(tr("Choose a sample"));
    up_ = new QToolButton(this);
    up_->setArrowType(Qt::UpArrow);
    up_->setToolTip(tr("Previous sample in this folder"));
    down_ = new QToolButton(this);
    down_->setArrowType(Qt::DownArrow);
    down_->setToolTip(tr("Next sample in this folder"));
    for (QToolButton* b : { choose_, up_, down_ })
        b->setFocusPolicy(Qt::NoFocus);

    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(3);
    l->addWidget(name_, 1);
    l->addWidget(up_);
    l->addWidget(down_);
    l->addWidget(choose_);

    connect(up_, &QToolButton::clicked, this, [this]() { step(-1); });
    connect(down_, &QToolButton::clicked, this, [this]() { step(+1); });
    connect(choose_, &QToolButton::clicked, this, [this]() { choose(); });
    updateLabel();
}

void SampleChooser::setSample(const QString& path)
{
    path_ = path;
    updateLabel();
}

void SampleChooser::updateLabel()
{
    if (path_.isEmpty()) {
        name_->setText(tr("(no sample)"));
        name_->setToolTip(QString());
    } else {
        const bool missing = !QFileInfo::exists(path_);
        name_->setText(QFileInfo(path_).fileName() + (missing ? tr("  (missing)") : QString()));
        name_->setToolTip(path_);
    }
    const bool any = !path_.isEmpty();
    up_->setEnabled(any);
    down_->setEnabled(any);
}

void SampleChooser::step(int dir)
{
    const QString next = stepSample(path_, dir);
    if (next.isEmpty() || next == path_)
        return;
    path_ = next;
    updateLabel();
    emit sampleChanged(path_);
}

void SampleChooser::choose()
{
    const QString start = !path_.isEmpty() ? QFileInfo(path_).absolutePath() : folder_;
    SampleBrowserDialog dlg(!folder_.isEmpty() ? folder_ : start, path_, preview_, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString picked = dlg.selected();
    if (picked.isEmpty() || picked == path_)
        return;
    path_ = picked;
    updateLabel();
    emit sampleChanged(path_);
}

SampleBrowserDialog::SampleBrowserDialog(const QString& folder, const QString& current, SampleChooser::Preview preview, QWidget* parent)
    : QDialog(parent)
    , preview_(std::move(preview))
{
    setWindowTitle(tr("Choose a sample"));
    resize(480, 420);
    model_ = new SampleTreeModel(this);
    tree_ = new QTreeView(this);
    tree_->setModel(model_);
    const QString root = !folder.isEmpty() ? folder : (!current.isEmpty() ? QFileInfo(current).absolutePath() : QString());
    tree_->setRootIndex(model_->setRootFolder(root));
    for (int c = 1; c < model_->columnCount(); ++c)
        tree_->hideColumn(c);
    tree_->setHeaderHidden(true);
    if (!current.isEmpty())
        tree_->setCurrentIndex(model_->indexForPath(current));

    previewButton_ = new QPushButton(tr("Preview"), this);
    previewButton_->setIcon(QIcon());
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setEnabled(false);
    buttons->addButton(previewButton_, QDialogButtonBox::ActionRole);

    auto* l = new QVBoxLayout(this);
    l->addWidget(tree_, 1);
    l->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(previewButton_, &QPushButton::clicked, this, [this]() { previewCurrent(); });
    connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this]() {
        const bool sample = !selected().isEmpty();
        ok_->setEnabled(sample);
        previewButton_->setEnabled(sample);
    });
    connect(tree_, &QTreeView::doubleClicked, this, [this](const QModelIndex& i) {
        if (model_->isSample(i))
            previewCurrent();
    });
    previewButton_->setEnabled(!selected().isEmpty());
    ok_->setEnabled(!selected().isEmpty());
}

QString SampleBrowserDialog::selected() const
{
    return model_->samplePath(tree_->currentIndex());
}

void SampleBrowserDialog::previewCurrent()
{
    const QString p = selected();
    if (!p.isEmpty() && preview_)
        preview_(p);
}

} // namespace vdj
