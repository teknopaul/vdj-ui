#include "virtual_pendrives_dialog.h"

#include "export/pendrive_registry.h"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace vdj {

enum { RoleFolder = Qt::UserRole, RoleName, RoleKind };      // kind: 0 group, 1 virtual, 2 physical

VirtualPendrivesDialog::VirtualPendrivesDialog(PendriveRegistry* registry, QWidget* parent)
    : QDialog(parent), registry_(registry)
{
    setWindowTitle(QStringLiteral("Pendrives"));
    resize(640, 420);
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(QStringLiteral("Folders with a rekordbox export (PIONEER/rekordbox/export.pdb) and the pendrives plugged into this PC. "
                                             "Export one to the decks like inserting a stick; export another to swap them (the decks refuse while a "
                                             "deck is using the current one)."), this));
    static_cast<QLabel*>(lay->itemAt(0)->widget())->setWordWrap(true);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({ QStringLiteral("Name"), QStringLiteral("Folder"), QStringLiteral("State") });
    tree_->setRootIsDecorated(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    lay->addWidget(tree_, 1);

    auto* row1 = new QHBoxLayout;
    addBtn_ = new QPushButton(QStringLiteral("&Add Folder..."), this);
    renameBtn_ = new QPushButton(QStringLiteral("&Rename..."), this);
    removeBtn_ = new QPushButton(QStringLiteral("Re&move"), this);
    row1->addWidget(addBtn_); row1->addWidget(renameBtn_); row1->addWidget(removeBtn_); row1->addStretch(1);
    lay->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    openBtn_ = new QPushButton(QStringLiteral("&Open in Browser"), this);
    exportBtn_ = new QPushButton(QStringLiteral("&Export to Decks (Insert)"), this);
    ejectBtn_ = new QPushButton(QStringLiteral("E&ject (USB STOP)"), this);
    row2->addWidget(openBtn_); row2->addWidget(exportBtn_); row2->addWidget(ejectBtn_); row2->addStretch(1);
    lay->addLayout(row2);

    exportedLabel_ = new QLabel(this);
    lay->addWidget(exportedLabel_);

    connect(addBtn_, &QPushButton::clicked, this, &VirtualPendrivesDialog::addFolder);
    connect(removeBtn_, &QPushButton::clicked, this, &VirtualPendrivesDialog::removeSelected);
    connect(renameBtn_, &QPushButton::clicked, this, &VirtualPendrivesDialog::renameSelected);
    connect(openBtn_, &QPushButton::clicked, this, [this]() { const Choice c = currentChoice(); if (c.valid) emit openRequested(c.folder); });
    connect(exportBtn_, &QPushButton::clicked, this, [this]() { const Choice c = currentChoice(); if (c.valid) emit exportRequested(c.folder, c.name); });
    connect(ejectBtn_, &QPushButton::clicked, this, &VirtualPendrivesDialog::ejectRequested);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &VirtualPendrivesDialog::updateButtons);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { const Choice c = currentChoice(); if (c.valid) emit openRequested(c.folder); });
    connect(registry_, &PendriveRegistry::changed, this, &VirtualPendrivesDialog::rebuild);
    connect(registry_, &PendriveRegistry::physicalChanged, this, &VirtualPendrivesDialog::rebuild);
    rebuild();
}

void VirtualPendrivesDialog::setExported(const QString& folder, const QString& state)
{
    exportedFolder_ = folder;
    exportedState_ = state;
    exportedLabel_->setText(folder.isEmpty() ? QStringLiteral("Nothing is exported to the decks.")
                                             : QStringLiteral("Exported: %1 (%2)").arg(folder, state));
    rebuild();
}

void VirtualPendrivesDialog::rebuild()
{
    const Choice keep = currentChoice();
    tree_->clear();
    auto stateOf = [this](const QString& folder, bool hasDb) {
        if (!exportedFolder_.isEmpty() && QDir::cleanPath(folder) == QDir::cleanPath(exportedFolder_)) return exportedState_;
        return hasDb ? QStringLiteral("ready") : QStringLiteral("no rekordbox export");
    };
    auto* virt = new QTreeWidgetItem(tree_, { QStringLiteral("HD Pendrive Copies") });
    virt->setData(0, RoleKind, 0);
    for (const auto& p : registry_->virtualPendrives()) {
        const bool ok = PendriveRegistry::isRekordboxFolder(p.folder);
        auto* it = new QTreeWidgetItem(virt, { p.name, p.folder, ok ? stateOf(p.folder, true) : QStringLiteral("folder missing") });
        it->setData(0, RoleFolder, p.folder); it->setData(0, RoleName, p.name); it->setData(0, RoleKind, ok ? 1 : -1);
    }
    auto* phys = new QTreeWidgetItem(tree_, { QStringLiteral("Plugged into this PC") });
    phys->setData(0, RoleKind, 0);
    for (const auto& p : PendriveRegistry::scanPhysical()) {
        auto* it = new QTreeWidgetItem(phys, { p.name, p.mountPoint, stateOf(p.mountPoint, p.hasRekordbox) });
        it->setData(0, RoleFolder, p.mountPoint); it->setData(0, RoleName, p.name); it->setData(0, RoleKind, p.hasRekordbox ? 2 : -2);
    }
    tree_->expandAll();
    if (keep.valid) {                                                   // keep the selection across refreshes
        for (QTreeWidgetItemIterator i(tree_); *i; ++i)
            if ((*i)->data(0, RoleFolder).toString() == keep.folder) { tree_->setCurrentItem(*i); break; }
    }
    updateButtons();
}

VirtualPendrivesDialog::Choice VirtualPendrivesDialog::currentChoice() const
{
    Choice c;
    const auto items = tree_->selectedItems();
    if (items.isEmpty()) return c;
    const int kind = items.first()->data(0, RoleKind).toInt();
    if (kind != 1 && kind != 2) return c;
    c.valid = true;
    c.physical = kind == 2;
    c.folder = items.first()->data(0, RoleFolder).toString();
    c.name = items.first()->data(0, RoleName).toString();
    return c;
}

void VirtualPendrivesDialog::updateButtons()
{
    const auto items = tree_->selectedItems();
    const int kind = items.isEmpty() ? 0 : items.first()->data(0, RoleKind).toInt();
    const Choice c = currentChoice();
    openBtn_->setEnabled(c.valid);
    exportBtn_->setEnabled(c.valid);
    renameBtn_->setEnabled(kind == 1 || kind == -1);
    removeBtn_->setEnabled(kind == 1 || kind == -1);
    ejectBtn_->setEnabled(!exportedFolder_.isEmpty());
}

void VirtualPendrivesDialog::addFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select a folder with PIONEER/rekordbox/export.pdb"));
    if (dir.isEmpty()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("HD Pendrive Copy"), QStringLiteral("Name:"), QLineEdit::Normal,
                                               PendriveRegistry::defaultNameFor(dir), &ok);
    if (!ok) return;
    const QString err = registry_->add(name, dir);
    if (!err.isEmpty()) QMessageBox::warning(this, QStringLiteral("Add Folder"), err);
}

void VirtualPendrivesDialog::removeSelected()
{
    const auto items = tree_->selectedItems();
    if (items.isEmpty()) return;
    const int idx = registry_->indexOfFolder(items.first()->data(0, RoleFolder).toString());
    if (idx >= 0) registry_->remove(idx);
}

void VirtualPendrivesDialog::renameSelected()
{
    const auto items = tree_->selectedItems();
    if (items.isEmpty()) return;
    const int idx = registry_->indexOfFolder(items.first()->data(0, RoleFolder).toString());
    if (idx < 0) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("Name:"), QLineEdit::Normal,
                                               registry_->virtualPendrives().at(idx).name, &ok);
    if (!ok) return;
    const QString err = registry_->rename(idx, name);
    if (!err.isEmpty()) QMessageBox::warning(this, QStringLiteral("Rename"), err);
}

} // namespace vdj
