#include "mix_tree_dock.h"

#include "trakcrate/mix_file.h"

#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace trakcrate;

namespace vdj {

namespace {
void fill(QTreeWidgetItem* parent, const MixTreeEntry& e)
{
    for (const MixTreeEntry& c : e.children) {
        auto* it = new QTreeWidgetItem(parent, QStringList(QString::fromStdString(c.name)));
        it->setData(0, Qt::UserRole, QString::fromStdString(c.path.string()));
        it->setData(0, Qt::UserRole + 1, c.folder);
        if (c.folder) fill(it, c);
    }
}
}

MixTreeDock::MixTreeDock(LibraryProvider provider, QWidget* parent) : QWidget(parent), provider_(std::move(provider))
{
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(2, 2, 2, 2);
    auto* refresh = new QPushButton(QStringLiteral("Refresh"));
    l->addWidget(refresh);
    tree_ = new QTreeWidget;
    tree_->setHeaderHidden(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    l->addWidget(tree_, 1);
    askText_ = [this](const QString& title, const QString& label, const QString& initial, QString* out) {
        bool ok = false;
        const QString t = QInputDialog::getText(this, title, label, QLineEdit::Normal, initial, &ok);
        if (ok) *out = t;
        return ok;
    };
    askYesNo_ = [this](const QString& title, const QString& text) { return QMessageBox::question(this, title, text) == QMessageBox::Yes; };
    connect(refresh, &QPushButton::clicked, this, [this] { this->refresh(); });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) {
        if (!it->data(0, Qt::UserRole + 1).toBool()) emit openRequested(it->data(0, Qt::UserRole).toString());
    });
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &MixTreeDock::contextMenu);
    this->refresh();
}

void MixTreeDock::refresh()
{
    tree_->clear();
    const Library* lib = provider_ ? provider_() : nullptr;
    if (!lib || !lib->isOpen()) return;
    fill(tree_->invisibleRootItem(), listMixes(*lib));
    tree_->expandAll();
}

bool MixTreeDock::insideMixes(const QString& path) const
{
    const Library* lib = provider_ ? provider_() : nullptr;
    if (!lib || path.isEmpty()) return false;
    const fs::path root = lib->mixesDir().lexically_normal();
    const fs::path p = fs::path(path.toStdString()).lexically_normal();
    const fs::path rel = p.lexically_relative(root);
    return !rel.empty() && *rel.begin() != ".." && rel != ".";
}

bool MixTreeDock::renameMix(const QString& path, const QString& newName)
{
    const Library* lib = provider_ ? provider_() : nullptr;
    const std::string nn = newName.toStdString();
    if (!lib || !insideMixes(path) || !isSafeName(nn) || nn[0] == '.' || nn.find('/') != std::string::npos) {
        emit status(QStringLiteral("That name cannot be used"));
        return false;
    }
    const fs::path from = fs::path(path.toStdString());
    const fs::path to = from.parent_path() / (nn + ".mix");
    std::error_code ec;
    if (fs::exists(to, ec)) {
        emit status(QStringLiteral("A mix with that name exists"));
        return false;
    }
    fs::rename(from, to, ec);
    if (ec) {
        emit status(QStringLiteral("Cannot rename: ") + QString::fromStdString(ec.message()));
        return false;
    }
    refresh();
    emit renamed(path, QString::fromStdString(to.string()));
    return true;
}

bool MixTreeDock::deleteEntry(const QString& path)
{
    if (!insideMixes(path)) return false;
    const fs::path p(path.toStdString());
    std::error_code ec;
    const bool folder = fs::is_directory(p, ec);
    if (folder && !fs::is_empty(p, ec)) {
        emit status(QStringLiteral("Only an empty folder can be deleted"));
        return false;
    }
    if (!askYesNo_(QStringLiteral("Delete"), QStringLiteral("Delete \"%1\"?").arg(QString::fromStdString(p.stem().string())))) return false;
    fs::remove(p, ec);
    if (ec) {
        emit status(QStringLiteral("Cannot delete: ") + QString::fromStdString(ec.message()));
        return false;
    }
    refresh();
    emit deleted(path);
    return true;
}

bool MixTreeDock::newFolder(const QString& parentPath, const QString& name)
{
    const Library* lib = provider_ ? provider_() : nullptr;
    if (!lib) return false;
    const std::string n = name.toStdString();
    if (!isSafeName(n) || n[0] == '.') {
        emit status(QStringLiteral("That name cannot be used"));
        return false;
    }
    fs::path parent = lib->mixesDir();
    if (!parentPath.isEmpty()) {
        if (!insideMixes(parentPath)) return false;
        parent = fs::path(parentPath.toStdString());
    }
    std::error_code ec;
    fs::create_directories(parent / n, ec);
    if (ec) {
        emit status(QStringLiteral("Cannot make the folder: ") + QString::fromStdString(ec.message()));
        return false;
    }
    refresh();
    return true;
}

void MixTreeDock::contextMenu(const QPoint& pos)
{
    QTreeWidgetItem* it = tree_->itemAt(pos);
    const QString path = it ? it->data(0, Qt::UserRole).toString() : QString();
    const bool folder = it && it->data(0, Qt::UserRole + 1).toBool();
    QMenu menu(this);
    QAction* open = it && !folder ? menu.addAction(QStringLiteral("Open")) : nullptr;
    QAction* rename = it && !folder ? menu.addAction(QStringLiteral("Rename...")) : nullptr;
    QAction* del = it ? menu.addAction(QStringLiteral("Delete...")) : nullptr;
    QAction* mk = menu.addAction(QStringLiteral("New folder..."));
    QAction* a = menu.exec(tree_->viewport()->mapToGlobal(pos));
    if (!a) return;
    if (a == open) emit openRequested(path);
    else if (a == rename) {
        QString n;
        if (askText_(QStringLiteral("Rename"), QStringLiteral("New name"), QString::fromStdString(fs::path(path.toStdString()).stem().string()), &n)) renameMix(path, n);
    } else if (a == del) deleteEntry(path);
    else if (a == mk) {
        QString n;
        if (askText_(QStringLiteral("New folder"), QStringLiteral("Name"), QString(), &n)) newFolder(folder ? path : QString(), n);
    }
}

} // namespace vdj
