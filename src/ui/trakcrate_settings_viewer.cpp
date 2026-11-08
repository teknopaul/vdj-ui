#include "trakcrate_settings_viewer.h"

#include "trakcrate/fsutil.h"
#include "trakcrate/settings_files.h"
#include "trakcrate/settings_sets.h"
#include "trakcrate/settings_view.h"

#include <map>

#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace vdj {

TrakcrateSettingsViewer::TrakcrateSettingsViewer(const trakcrate::Library& lib, QWidget* parent) : QDialog(parent), lib_(lib)
{
    setWindowTitle(QStringLiteral("Settings"));
    resize(820, 620);
    auto* top = new QVBoxLayout(this);
    auto* pick = new QHBoxLayout;
    a_ = new QComboBox;
    b_ = new QComboBox;
    file_ = new QComboBox;
    for (const trakcrate::SettingsSet& s : trakcrate::listSettingsSets(lib_)) {
        a_->addItem(QString::fromStdString(s.name));
        b_->addItem(QString::fromStdString(s.name));
    }
    b_->insertItem(0, QStringLiteral("(no comparison)"), QString());
    b_->setCurrentIndex(0);
    for (const std::string& f : trakcrate::settingsFileNames()) file_->addItem(QString::fromStdString(f));
    pick->addWidget(new QLabel(QStringLiteral("Settings:")));
    pick->addWidget(a_, 1);
    pick->addWidget(new QLabel(QStringLiteral("Compare with:")));
    pick->addWidget(b_, 1);
    pick->addWidget(new QLabel(QStringLiteral("File:")));
    pick->addWidget(file_, 1);
    top->addLayout(pick);

    table_ = new QTableWidget;
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->hide();
    top->addWidget(table_, 3);
    hex_ = new QPlainTextEdit;
    hex_->setReadOnly(true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);       // only the hex dump is fixed width
    mono.setStyleHint(QFont::Monospace);
    mono.setFixedPitch(true);
    hex_->setFont(mono);
    hex_->setStyleSheet(QStringLiteral("QPlainTextEdit { font-family: 'DejaVu Sans Mono', 'Liberation Mono', monospace; }"));      // the app stylesheet sets a proportional family on every QWidget, which beats setFont
    hex_->setLineWrapMode(QPlainTextEdit::NoWrap);
    top->addWidget(hex_, 2);
    status_ = new QLabel;
    top->addWidget(status_);

    auto* nameRow = new QHBoxLayout;
    nameRow->addWidget(new QLabel(QStringLiteral("Name on the players (djprofile.nxs):")));
    userName_ = new QLineEdit;
    userName_->setMaxLength(int(trakcrate::kDjprofileNameMax));
    userName_->setToolTip(QStringLiteral("Changes the name in this set's djprofile.nxs. Not yet tried on a real player."));
    save_ = new QPushButton(QStringLiteral("Save name"));
    nameRow->addWidget(userName_, 1);
    nameRow->addWidget(save_);
    top->addLayout(nameRow);
    auto* close = new QPushButton(QStringLiteral("Close"));
    top->addWidget(close, 0, Qt::AlignRight);

    connect(a_, &QComboBox::currentIndexChanged, this, [this] { reload(); });
    connect(b_, &QComboBox::currentIndexChanged, this, [this] { reload(); });
    connect(file_, &QComboBox::currentIndexChanged, this, [this] { reload(); });
    connect(save_, &QPushButton::clicked, this, [this] {
        QString err;
        if (!saveUserName(&err)) QMessageBox::warning(this, QStringLiteral("Settings"), err);
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    reload();
}

QString TrakcrateSettingsViewer::set() const { return a_->currentText(); }
QString TrakcrateSettingsViewer::compareSet() const { return b_->currentIndex() <= 0 ? QString() : b_->currentText(); }
int TrakcrateSettingsViewer::rowCount() const { return table_->rowCount(); }
int TrakcrateSettingsViewer::differingRows() const { return differing_; }

void TrakcrateSettingsViewer::setSet(const QString& n) { const int i = a_->findText(n); if (i >= 0) a_->setCurrentIndex(i); }
void TrakcrateSettingsViewer::setCompareSet(const QString& n)
{
    const int i = n.isEmpty() ? 0 : b_->findText(n);
    if (i >= 0) b_->setCurrentIndex(i);
}
void TrakcrateSettingsViewer::setFile(const QString& f) { const int i = file_->findText(f); if (i >= 0) file_->setCurrentIndex(i); }

// The file of a set, or the built in bytes when the set lacks it (that is what an export would write).
bool TrakcrateSettingsViewer::read(const QString& set, const QString& file, std::string* bytes, bool* builtIn) const
{
    *builtIn = false;
    std::error_code ec;
    const trakcrate::fs::path p = trakcrate::settingsSetDir(lib_, set.toStdString()) / file.toStdString();
    if (trakcrate::fs::is_regular_file(p, ec) && trakcrate::readFile(p, bytes) && !bytes->empty()) return true;
    trakcrate::SettingsFiles sf;
    if (!trakcrate::resolveSettingsFiles(lib_, std::string(), &sf)) return false;
    trakcrate::SettingsKind k;
    if (!trakcrate::settingsKindForName(file.toStdString(), &k)) return false;
    switch (k) {
    case trakcrate::SettingsKind::DevSetting: *bytes = trakcrate::defaultDevSetting(); break;
    case trakcrate::SettingsKind::MySetting: *bytes = trakcrate::defaultMySetting(); break;
    case trakcrate::SettingsKind::Djprofile: *bytes = trakcrate::defaultDjprofile(); break;
    case trakcrate::SettingsKind::Rbfltr: *bytes = trakcrate::defaultRbfltr(); break;
    case trakcrate::SettingsKind::ExportExt: *bytes = trakcrate::defaultExportExt(); break;
    case trakcrate::SettingsKind::MySetting2:        // not part of a set: nothing built in
    case trakcrate::SettingsKind::DjmMySetting: return false;
    }
    *builtIn = true;
    return true;
}

void TrakcrateSettingsViewer::reload()
{
    differing_ = 0;
    table_->clear();
    table_->setRowCount(0);
    hex_->clear();
    trakcrate::SettingsKind kind;
    if (!trakcrate::settingsKindForName(file_->currentText().toStdString(), &kind) || a_->count() == 0) {
        status_->setText(QStringLiteral("No settings yet."));
        userName_->setEnabled(false);
        save_->setEnabled(false);
        return;
    }
    std::string bytesA, bytesB;
    bool builtA = false, builtB = false;
    read(set(), file_->currentText(), &bytesA, &builtA);
    const bool cmp = !compareSet().isEmpty();
    if (cmp) read(compareSet(), file_->currentText(), &bytesB, &builtB);

    // the name edit follows set A's profile
    {
        std::string prof;
        bool b = false;
        read(set(), QStringLiteral("djprofile.nxs"), &prof, &b);
        trakcrate::DjProfile p;
        const bool ok = trakcrate::parseDjprofile(prof, &p);
        userName_->setText(ok ? QString::fromStdString(p.userName) : QString());
        userName_->setEnabled(ok);
        save_->setEnabled(ok);
    }

    const std::vector<trakcrate::SettingsRow> rows = trakcrate::describeSettingsFile(kind, bytesA);
    std::map<std::string, std::string> other;
    if (cmp)
        for (const trakcrate::SettingsRow& r : trakcrate::describeSettingsFile(kind, bytesB)) other[r.field] = r.value;
    QStringList heads = {QStringLiteral("Field"), cmp ? set() : QStringLiteral("Value")};
    if (cmp) heads << compareSet();
    heads << QStringLiteral("Note");
    table_->setColumnCount(heads.size());
    table_->setHorizontalHeaderLabels(heads);
    table_->setRowCount(int(rows.size()));
    const QBrush diff(QColor(255, 200, 80, 90)), unknown(QColor(128, 128, 128, 40));
    for (size_t i = 0; i < rows.size(); ++i) {
        const trakcrate::SettingsRow& r = rows[i];
        int col = 0;
        auto cell = [&](const QString& t) { auto* it = new QTableWidgetItem(t); table_->setItem(int(i), col++, it); return it; };
        QList<QTableWidgetItem*> items;
        items << cell(QString::fromStdString(r.field)) << cell(QString::fromStdString(r.value));
        bool differs = false;
        if (cmp) {
            auto o = other.find(r.field);
            items << cell(o == other.end() ? QStringLiteral("-") : QString::fromStdString(o->second));
            differs = o == other.end() || o->second != r.value;
        }
        items << cell(QString::fromStdString(r.note));
        if (differs) ++differing_;
        for (auto* it : items) {
            if (differs) it->setBackground(diff);
            else if (!r.known) it->setBackground(unknown);
        }
    }
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setStretchLastSection(true);
    hex_->setPlainText(QString::fromStdString(trakcrate::hexDump(bytesA)));
    QString st = QStringLiteral("%1 / %2%3").arg(set(), file_->currentText(), builtA ? QStringLiteral(" (the set has no such file: the built in one is shown)") : QString());
    if (cmp) st += differing_ ? QStringLiteral(". %1 field(s) differ from %2").arg(differing_).arg(compareSet()) : QStringLiteral(". Identical to %1").arg(compareSet());
    status_->setText(st + QStringLiteral(". Grey rows are not understood yet; nothing here is editable except the name."));
}

bool TrakcrateSettingsViewer::saveUserName(QString* err)
{
    std::string e;
    const bool ok = trakcrate::setSettingsSetUserName(lib_, set().toStdString(), userName_->text().trimmed().toStdString(), &e);
    if (!ok) {
        if (err) *err = QString::fromStdString(e);
        return false;
    }
    emit userNameChanged(set());
    reload();
    return true;
}

} // namespace vdj
