#include "mix_filters_dialog.h"

#include "audio/ladspa_host.h"
#include "studio_mix_window.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

using namespace trakcrate;

namespace vdj {

namespace {
QString filterTitle(const MixFilter& f)
{
    if (f.kind == MixFilterKind::BuiltinCompressor) return QStringLiteral("Compressor / limiter (built in)");
    LadspaPluginInfo info;
    return LadspaHost::find(QString::fromStdString(f.plugin), &info) ? info.name : QString::fromStdString(f.plugin) + QStringLiteral(" (not installed)");
}
}

MixFiltersDialog::MixFiltersDialog(StudioMixWindow* window) : QDialog(window), w_(window)
{
    setWindowTitle(QStringLiteral("EQ and master filters"));
    resize(480, 520);
    auto* top = new QVBoxLayout(this);
    auto* eq = new QFormLayout;
    bass_ = new QDoubleSpinBox;
    bass_->setRange(20, 2000);
    bass_->setSuffix(QStringLiteral(" Hz"));
    high_ = new QDoubleSpinBox;
    high_->setRange(200, 16000);
    high_->setSuffix(QStringLiteral(" Hz"));
    slope_ = new QComboBox;
    for (int s : {12, 24, 48}) slope_->addItem(QStringLiteral("%1 dB/octave").arg(s), s);
    eq->addRow(QStringLiteral("Bass crossover"), bass_);
    eq->addRow(QStringLiteral("High crossover"), high_);
    eq->addRow(QStringLiteral("Slope"), slope_);
    top->addLayout(eq);
    top->addWidget(new QLabel(QStringLiteral("<b>Master filters</b> (in order; tick = on)")));
    list_ = new QListWidget;
    top->addWidget(list_, 1);
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("Add LADSPA..."));
    auto* rem = new QPushButton(QStringLiteral("Remove"));
    auto* up = new QPushButton(QStringLiteral("Up"));
    auto* down = new QPushButton(QStringLiteral("Down"));
    for (QPushButton* b : {add, rem, up, down}) row->addWidget(b);
    top->addLayout(row);
    controls_ = new QWidget;
    controlsLayout_ = new QVBoxLayout(controls_);
    top->addWidget(controls_);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    top->addWidget(box);

    connect(bass_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (filling_) return;
        const double v = bass_->value();
        w_->edit(QStringLiteral("Bass crossover"), [v](Mix& m) { m.bassHz = v; }, QStringLiteral("bassHz"));
    });
    connect(high_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (filling_) return;
        const double v = high_->value();
        w_->edit(QStringLiteral("High crossover"), [v](Mix& m) { m.highHz = v; }, QStringLiteral("highHz"));
    });
    connect(slope_, QOverload<int>::of(&QComboBox::activated), this, [this] {
        const int v = slope_->currentData().toInt();
        w_->edit(QStringLiteral("EQ slope"), [v](Mix& m) { m.slopeDb = v; });
    });
    connect(list_, &QListWidget::currentRowChanged, this, [this] { showControls(); });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        if (filling_) return;
        const int row = list_->row(it);
        const bool on = it->checkState() == Qt::Checked;
        w_->edit(QStringLiteral("Filter on / off"), [row, on](Mix& m) { if (row >= 0 && size_t(row) < m.filters.size()) m.filters[size_t(row)].on = on; });
    });
    connect(rem, &QPushButton::clicked, this, [this] { removeCurrent(); });
    connect(up, &QPushButton::clicked, this, [this] { moveCurrent(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveCurrent(1); });
    connect(add, &QPushButton::clicked, this, [this] {
        QDialog d(this);
        d.setWindowTitle(QStringLiteral("Add a LADSPA plugin"));
        auto* l = new QVBoxLayout(&d);
        auto* search = new QLineEdit;
        search->setPlaceholderText(QStringLiteral("Search"));
        auto* list = new QListWidget;
        for (const LadspaPluginInfo& p : LadspaHost::plugins()) {
            auto* it = new QListWidgetItem(p.name + QStringLiteral("  (") + p.maker + QStringLiteral(")"));
            it->setData(Qt::UserRole, p.key);
            list->addItem(it);
        }
        connect(search, &QLineEdit::textChanged, &d, [list](const QString& t) {
            for (int i = 0; i < list->count(); ++i) list->item(i)->setHidden(!list->item(i)->text().contains(t, Qt::CaseInsensitive));
        });
        auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        connect(list, &QListWidget::itemDoubleClicked, &d, &QDialog::accept);
        l->addWidget(search);
        l->addWidget(list);
        l->addWidget(b);
        if (d.exec() == QDialog::Accepted && list->currentItem()) addLadspa(list->currentItem()->data(Qt::UserRole).toString());
    });
    // deferred: the edit may come from a widget (a spin box, a list item) that refresh() would delete while it is still emitting
    connect(w_, &StudioMixWindow::mixChanged, this, [this] { QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection); });
    refresh();
}

void MixFiltersDialog::refresh()
{
    filling_ = true;
    const Mix& m = w_->mix();
    bass_->setValue(m.bassHz);
    high_->setValue(m.highHz);
    slope_->setCurrentIndex(std::max(0, slope_->findData(m.slopeDb)));
    const int keep = list_->currentRow();
    list_->clear();
    for (const MixFilter& f : m.filters) {
        auto* it = new QListWidgetItem(filterTitle(f));
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(f.on ? Qt::Checked : Qt::Unchecked);
        list_->addItem(it);
    }
    if (list_->count()) list_->setCurrentRow(std::max(0, std::min(keep, list_->count() - 1)));
    filling_ = false;
    showControls();
}

void MixFiltersDialog::showControls()
{
    while (QLayoutItem* it = controlsLayout_->takeAt(0)) {
        if (QWidget* w = it->widget()) { w->hide(); w->deleteLater(); }
        delete it;
    }
    const int row = list_->currentRow();
    const Mix& m = w_->mix();
    if (row < 0 || size_t(row) >= m.filters.size()) return;
    const MixFilter& f = m.filters[size_t(row)];
    auto* form = new QFormLayout;
    auto* host = new QWidget;
    host->setLayout(form);
    auto addSpin = [&](const QString& label, const std::string& name, double lo, double hi, double def, bool integer, const QString& suffix) {
        auto* s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(integer ? 0 : 3);
        s->setSuffix(suffix);
        auto it = f.params.find(name);
        s->setValue(it != f.params.end() ? it->second : def);
        form->addRow(label, s);
        connect(s, &QDoubleSpinBox::editingFinished, this, [this, s, row, name] {
            const double v = s->value();
            w_->edit(QStringLiteral("Filter setting"), [row, name, v](Mix& mm) { if (row >= 0 && size_t(row) < mm.filters.size()) mm.filters[size_t(row)].params[name] = v; }, QStringLiteral("filter%1:%2").arg(row).arg(QString::fromStdString(name)));
        });
    };
    if (f.kind == MixFilterKind::BuiltinCompressor) {
        addSpin(QStringLiteral("Threshold"), "threshold", -40, 0, -3, false, QStringLiteral(" dBFS"));
        addSpin(QStringLiteral("Ratio"), "ratio", 1, 40, 10, false, QStringLiteral(" : 1"));
        addSpin(QStringLiteral("Attack"), "attack", 0, 100, 1, false, QStringLiteral(" ms"));
        addSpin(QStringLiteral("Release"), "release", 1, 2000, 80, false, QStringLiteral(" ms"));
    } else {
        LadspaPluginInfo info;
        if (LadspaHost::find(QString::fromStdString(f.plugin), &info))
            for (const LadspaControl& c : info.controls) addSpin(c.name, c.name.toStdString(), c.lo, c.hi, c.def, c.integer, QString());
    }
    controlsLayout_->addWidget(host);
}

void MixFiltersDialog::addLadspa(const QString& key)
{
    LadspaPluginInfo info;
    LadspaHost::find(key, &info);
    MixFilter f;
    f.kind = MixFilterKind::Ladspa;
    f.plugin = key.toStdString();
    for (const LadspaControl& c : info.controls) f.params[c.name.toStdString()] = c.def;
    w_->edit(QStringLiteral("Add filter"), [f](Mix& m) { m.filters.push_back(f); });
}

void MixFiltersDialog::removeCurrent()
{
    const int row = list_->currentRow();
    w_->edit(QStringLiteral("Remove filter"), [row](Mix& m) { if (row >= 0 && size_t(row) < m.filters.size()) m.filters.erase(m.filters.begin() + row); });
}

void MixFiltersDialog::moveCurrent(int delta)
{
    const int row = list_->currentRow();
    w_->edit(QStringLiteral("Move filter"), [row, delta](Mix& m) {
        const int to = row + delta;
        if (row >= 0 && to >= 0 && size_t(row) < m.filters.size() && size_t(to) < m.filters.size()) std::swap(m.filters[size_t(row)], m.filters[size_t(to)]);
    });
    list_->setCurrentRow(std::max(0, std::min(list_->count() - 1, row + delta)));
}

} // namespace vdj
