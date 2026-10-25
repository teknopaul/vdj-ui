#include "pendrive_toolbar.h"
#include "export/pendrive_registry.h"

#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QToolButton>

namespace vdj {

namespace {
constexpr int RoleFolder = Qt::UserRole;
constexpr int RoleName = Qt::UserRole + 1;
}

PendriveToolBar::PendriveToolBar(PendriveRegistry* registry, QWidget* parent)
    : QWidget(parent), registry_(registry)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(2, 1, 2, 1);
    lay->setSpacing(4);

    combo_ = new QComboBox(this);
    combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo_->setMinimumContentsLength(12);
    combo_->setToolTip(QStringLiteral("Pendrives to look at: your virtual ones and those plugged into this PC. Choosing one opens it in the viewer only."));
    lay->addWidget(combo_, 1);

    stopBtn_ = new QToolButton(this);
    stopBtn_->setText(QStringLiteral("USB STOP"));
    stopBtn_->setToolTip(QStringLiteral("Unmount the pendrive the decks see, like pressing USB STOP on a deck"));
    insertBtn_ = new QToolButton(this);
    insertBtn_->setText(QStringLiteral("Insert"));
    insertBtn_->setToolTip(QStringLiteral("Export the chosen pendrive to the decks"));
    for (QToolButton* b : { stopBtn_, insertBtn_ }) {
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setAutoRaise(false);
        lay->addWidget(b);
    }

    connect(combo_, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        const QString f = selectedFolder();
        if (!f.isEmpty()) { viewed_ = f; emit openRequested(f); }
    });
    connect(stopBtn_, &QToolButton::clicked, this, &PendriveToolBar::stopRequested);
    connect(insertBtn_, &QToolButton::clicked, this, [this]() {
        const QString f = selectedFolder();
        if (f.isEmpty()) return;
        emit insertRequested(f, combo_->currentData(RoleName).toString());
    });
    connect(registry_, &PendriveRegistry::changed, this, &PendriveToolBar::rebuild);
    connect(registry_, &PendriveRegistry::physicalChanged, this, &PendriveToolBar::rebuild);
    rebuild();
}

QString PendriveToolBar::selectedFolder() const
{
    return combo_->currentData(RoleFolder).toString();
}

void PendriveToolBar::rebuild()
{
    const bool was = combo_->blockSignals(true);
    combo_->clear();
    combo_->addItem(QStringLiteral("(choose a pendrive)"), QString());
    auto add = [this](const QString& label, const QString& folder, const QString& name) {
        const bool isExp = !exported_.isEmpty() && QDir::cleanPath(folder) == QDir::cleanPath(exported_);
        combo_->addItem(isExp ? label + QStringLiteral("  ▶ in the decks") : label, folder);
        combo_->setItemData(combo_->count() - 1, name, RoleName);
        combo_->setItemData(combo_->count() - 1, folder, Qt::ToolTipRole);
    };
    for (const auto& p : registry_->virtualPendrives())
        add(p.name, p.folder, p.name);
    for (const auto& p : PendriveRegistry::scanPhysical())
        if (p.hasRekordbox) add(p.name + QStringLiteral(" (plugged in)"), p.mountPoint, p.name);
    int idx = 0;
    for (int i = 1; i < combo_->count(); ++i)
        if (!viewed_.isEmpty() && QDir::cleanPath(combo_->itemData(i, RoleFolder).toString()) == QDir::cleanPath(viewed_)) { idx = i; break; }
    combo_->setCurrentIndex(idx);
    combo_->blockSignals(was);
}

void PendriveToolBar::setViewed(const QString& folder)
{
    if (viewed_ == folder) return;
    viewed_ = folder;
    rebuild();
}

void PendriveToolBar::setExported(const QString& folder)
{
    if (exported_ == folder) return;
    exported_ = folder;
    rebuild();
}

void PendriveToolBar::setStopHint(bool works, const QString& why)
{
    const QString base = QStringLiteral("Unmount the pendrive the decks see, like pressing USB STOP on a deck");
    stopBtn_->setStyleSheet(works ? QString() : QStringLiteral("QToolButton { color: #707070; }"));
    stopBtn_->setToolTip(works || why.isEmpty() ? base : QStringLiteral("%1\n\nWill be refused now: %2 (press it anyway to make the decks show who uses it)").arg(base, why));
}

} // namespace vdj
