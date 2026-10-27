#include "trakcrate_pendrive_dialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vdj {

TrakcratePendriveDialog::TrakcratePendriveDialog(const trakcrate::Library& lib, QWidget* parent) : QDialog(parent), lib_(lib)
{
    setWindowTitle(QStringLiteral("New pendrive"));
    name_ = new QLineEdit;
    name_->setPlaceholderText(QStringLiteral("Name of the pendrive"));
    tree_ = new QTreeWidget;
    tree_->setHeaderLabels({QStringLiteral("Contents"), QStringLiteral("Tracks")});
    tree_->setRootIsDecorated(true);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setStretchLastSection(false);
    tree_->setMinimumHeight(260);
    for (const char* group : {"Folders", "Playlists"}) {
        auto* g = new QTreeWidgetItem(tree_, QStringList(QString::fromLatin1(group)));
        g->setFlags(Qt::ItemIsEnabled | Qt::ItemIsAutoTristate | Qt::ItemIsUserCheckable);
        g->setCheckState(0, Qt::Unchecked);
    }
    for (const std::string& f : lib_.listFolders()) {
        auto* it = new QTreeWidgetItem(tree_->topLevelItem(0), QStringList{QString::fromStdString(f), QString::number(lib_.listTracks(f).size())});
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        it->setCheckState(0, Qt::Unchecked);
        it->setTextAlignment(1, Qt::AlignRight);
    }
    for (const std::string& p : lib_.listPlaylists()) {
        auto* it = new QTreeWidgetItem(tree_->topLevelItem(1), QStringList{QString::fromStdString(p), QString::number(lib_.playlistTracks(p).size())});
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        it->setCheckState(0, Qt::Unchecked);
        it->setTextAlignment(1, Qt::AlignRight);
    }
    tree_->expandAll();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(QStringLiteral("Create"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Tick the folders and playlists to put on the pendrive. Each one becomes a playlist on it; you can reorder, add and "
                                                "remove later.")));
    layout->addWidget(name_);
    layout->addWidget(tree_, 1);
    layout->addWidget(buttons);
    resize(480, 460);

    connect(name_, &QLineEdit::textChanged, this, &TrakcratePendriveDialog::check);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    check();
}

void TrakcratePendriveDialog::check()
{
    const QString n = name().trimmed();
    ok_->setEnabled(!n.isEmpty() && !n.contains(QLatin1Char('/')) && n != QLatin1String(".") && n != QLatin1String("..") &&
                    !lib_.hasDrive(n.toStdString()));
    ok_->setToolTip(lib_.hasDrive(n.toStdString()) ? QStringLiteral("A pendrive with that name exists") : QString());
}

QString TrakcratePendriveDialog::name() const { return name_->text().trimmed(); }
void TrakcratePendriveDialog::setName(const QString& n) { name_->setText(n); }

QStringList TrakcratePendriveDialog::ticked(int group) const
{
    QStringList out;
    const QTreeWidgetItem* g = tree_->topLevelItem(group);
    for (int i = 0; i < g->childCount(); ++i)
        if (g->child(i)->checkState(0) == Qt::Checked) out << g->child(i)->text(0);
    return out;
}

QStringList TrakcratePendriveDialog::folders() const { return ticked(0); }
QStringList TrakcratePendriveDialog::playlists() const { return ticked(1); }

void TrakcratePendriveDialog::tick(int group, const QString& name, bool on)
{
    QTreeWidgetItem* g = tree_->topLevelItem(group);
    for (int i = 0; i < g->childCount(); ++i)
        if (g->child(i)->text(0) == name) g->child(i)->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
}

void TrakcratePendriveDialog::tickFolder(const QString& n, bool on) { tick(0, n, on); }
void TrakcratePendriveDialog::tickPlaylist(const QString& n, bool on) { tick(1, n, on); }

} // namespace vdj
