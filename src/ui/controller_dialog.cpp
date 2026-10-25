#include "controller_dialog.h"

#include "midi/sampler_midi_control.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <memory>

namespace vdj {

static const char* const kRowNames[ControllerDialog::kRows] = {
    "mute", "solo", "rec", "select", "knob", "fader", "track back", "track forward", "mode toggle" };

QString ControllerDialog::opNameFor(int row, int strip)
{
    if (row >= 0 && row < 6 && strip >= 1 && strip <= kStrips)
        return SamplerCtlMap::opName(ControlAction::Strip, Control(row), strip);
    if (strip != 1)
        return QString();
    if (row == 6) return SamplerCtlMap::opName(ControlAction::TrackBack, Control::Mute, 0);
    if (row == 7) return SamplerCtlMap::opName(ControlAction::TrackForward, Control::Mute, 0);
    if (row == 8) return SamplerCtlMap::opName(ControlAction::ModeToggle, Control::Mute, 0);
    return QString();
}

ControllerDialog::ControllerDialog(SamplerMidiControl* control, QWidget* parent)
    : QDialog(parent)
    , control_(control)
{
    setWindowTitle(tr("Sampler controller"));
    resize(820, 470);

    path_ = new QLabel(this);
    path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* openButton = new QPushButton(tr("Open another .adjmm..."), this);
    source_ = new QLineEdit(this);
    source_->setPlaceholderText(tr("part of the MIDI client's name, empty = any"));
    source_->setToolTip(tr("Only events from a MIDI client whose name contains this are used (the #! source line)"));
    pickup_ = new QCheckBox(tr("Pickup"), this);
    pickup_->setToolTip(tr("A fader or knob does nothing until it reaches the value on screen (no jumps)"));
    recForward_ = new QCheckBox(tr("rec = next sample"), this);
    recForward_->setToolTip(tr("rec steps to the next sample and select to the previous one; untick to swap them"));
    anyChannel_ = new QCheckBox(tr("any channel"), this);
    anyChannel_->setChecked(true);
    anyChannel_->setToolTip(tr("Learn writes channel 0 (any); untick to write the channel the controller sent"));
    fill_ = new QCheckBox(tr("learn strip 1 fills the row"), this);
    fill_->setToolTip(tr("Learning strip 1 of a row also fills strips 2-8 with controller number + 1, + 2 ..."));

    table_ = new QTableWidget(kRows, kStrips, this);
    QStringList cols, rows;
    for (int s = 1; s <= kStrips; ++s) cols << QString::number(s);
    for (const char* r : kRowNames) rows << tr(r);
    table_->setHorizontalHeaderLabels(cols);
    table_->setVerticalHeaderLabels(rows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSelectionBehavior(QAbstractItemView::SelectItems);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setToolTip(tr("Select a cell and press Learn (or double click it), then move or press that control"));

    learn_ = new QPushButton(tr("Learn"), this);
    clear_ = new QPushButton(tr("Clear cell"), this);
    reset_ = new QPushButton(tr("Reset to the example"), this);
    reset_->setToolTip(tr("Fills the table with the example mapping (a guess at the nanoKONTROL layout); Save writes it"));
    save_ = new QPushButton(tr("Save"), this);
    live_ = new QLabel(tr("Last event: -"), this);
    status_ = new QLabel(this);
    auto* close = new QPushButton(tr("Close"), this);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("File:"), this));
    top->addWidget(path_, 1);
    top->addWidget(openButton);
    auto* opts = new QHBoxLayout;
    opts->addWidget(new QLabel(tr("Source:"), this));
    opts->addWidget(source_, 1);
    opts->addWidget(pickup_);
    opts->addWidget(recForward_);
    auto* learnRow = new QHBoxLayout;
    learnRow->addWidget(learn_);
    learnRow->addWidget(clear_);
    learnRow->addWidget(anyChannel_);
    learnRow->addWidget(fill_);
    learnRow->addStretch(1);
    learnRow->addWidget(reset_);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(live_, 1);
    bottom->addWidget(save_);
    bottom->addWidget(close);
    auto* l = new QVBoxLayout(this);
    l->addLayout(top);
    l->addLayout(opts);
    l->addWidget(table_, 1);
    l->addLayout(learnRow);
    l->addWidget(status_);
    l->addLayout(bottom);

    connect(openButton, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(this, tr("Controller map"), QString(), tr("adj midi maps (*.adjmm);;All files (*)"));
        if (f.isEmpty() || !control_->loadFromFile(f))
            return;
        reloadFromFile();
        emit saved(f);
    });
    connect(learn_, &QPushButton::clicked, this, [this]() { learnSelected(); });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int r, int c) { startLearn(r, c + 1); });
    connect(clear_, &QPushButton::clicked, this, [this]() {
        const QModelIndex i = table_->currentIndex();
        const QString n = opNameFor(i.row(), i.column() + 1);
        if (n.isEmpty()) return;
        AdjmmOp none;
        setOp(n, none);
    });
    connect(reset_, &QPushButton::clicked, this, [this]() { resetToExample(); });
    connect(save_, &QPushButton::clicked, this, [this]() { saveFile(); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(control_, &SamplerMidiControl::learned, this, &ControllerDialog::onLearned);
    connect(control_, &SamplerMidiControl::eventSeen, this, [this](const QString& d) { live_->setText(tr("Last event: %1").arg(d)); });
    connect(this, &QDialog::finished, this, [this]() { stopLearning(); });

    reloadFromFile();
}

ControllerDialog::~ControllerDialog()
{
    stopLearning();
}

void ControllerDialog::reloadFromFile()
{
    path_->setText(control_->path().isEmpty() ? tr("(none)") : control_->path());
    ops_.clear();
    std::unique_ptr<AdjmmMap> m(control_->path().isEmpty() ? nullptr : AdjmmMap::readFromFile(control_->path()));
    orig_.clear();
    origDirectives_.clear();
    if (m) {
        ops_ = m->named;
        orig_ = m->named;
        origDirectives_ = m->directives;
    }
    source_->setText(control_->map().source());
    pickup_->setChecked(control_->map().pickup());
    recForward_->setChecked(control_->map().recIsForward());
    fillTable();
}

void ControllerDialog::resetToExample()
{
    std::unique_ptr<AdjmmMap> m(AdjmmMap::readFromFile(SamplerMidiControl::examplePath()));
    if (!m)
        return;
    for (auto& kv : ops_)
        kv.second = AdjmmOp();                       // anything the example does not set is removed on Save
    for (const auto& kv : m->named)
        ops_[kv.first] = kv.second;
    const SamplerCtlMap ex = SamplerCtlMap::fromAdjmm(*m);
    source_->setText(ex.source());
    pickup_->setChecked(ex.pickup());
    recForward_->setChecked(ex.recIsForward());
    fillTable();
    status_->setText(tr("Example loaded - Save to keep it"));
}

QString ControllerDialog::cellText(int row, int strip) const
{
    const auto it = ops_.find(opNameFor(row, strip));
    if (it == ops_.end() || it->second.controller == ADJMM_UNSET)
        return QStringLiteral("-");
    const AdjmmOp& op = it->second;
    const QString ch = op.channel == ADJMM_ANY_CHANNEL ? tr("any") : tr("ch %1").arg(op.channel);
    if (op.controller == ADJMM_NOTEON)
        return QStringLiteral("%1 / N %2").arg(ch).arg(op.value);
    return QStringLiteral("%1 / CC %2").arg(ch).arg(op.controller);
}

void ControllerDialog::fillTable()
{
    for (int r = 0; r < kRows; ++r)
        for (int s = 1; s <= kStrips; ++s) {
            auto* item = table_->item(r, s - 1);
            if (!item) {
                item = new QTableWidgetItem;
                item->setTextAlignment(Qt::AlignCenter);
                table_->setItem(r, s - 1, item);
            }
            const bool used = !opNameFor(r, s).isEmpty();
            item->setText(used ? cellText(r, s) : QString());
            item->setFlags(used ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::NoItemFlags);
        }
}

void ControllerDialog::setOp(const QString& name, const AdjmmOp& op)
{
    ops_[name] = op;
    fillTable();
}

void ControllerDialog::learnSelected()
{
    const QModelIndex i = table_->currentIndex();
    if (!i.isValid()) {
        status_->setText(tr("Select a cell first"));
        return;
    }
    startLearn(i.row(), i.column() + 1);
}

void ControllerDialog::startLearn(int row, int strip)
{
    if (opNameFor(row, strip).isEmpty())
        return;
    learnRow_ = row;
    learnStrip_ = strip;
    control_->setLearning(true);
    table_->setCurrentCell(row, strip - 1);
    status_->setText(tr("Learning %1: move or press the control...").arg(opNameFor(row, strip)));
    learn_->setText(tr("Learning..."));
}

void ControllerDialog::stopLearning()
{
    learnRow_ = -1;
    control_->setLearning(false);
    learn_->setText(tr("Learn"));
}

void ControllerDialog::onLearned(int channel, int controller, bool isNote, int value)
{
    if (learnRow_ < 0)
        return;
    const int row = learnRow_, strip = learnStrip_;
    const bool analog = row == 4 || row == 5;
    if (analog && isNote) {
        status_->setText(tr("A knob or fader must send a controller (CC), not a note - try again"));
        return;
    }
    stopLearning();
    AdjmmOp op;
    op.channel = anyChannel_->isChecked() ? ADJMM_ANY_CHANNEL : uint8_t(channel);
    op.controller = isNote ? ADJMM_NOTEON : uint8_t(controller);
    op.value = isNote ? uint8_t(controller) : analog ? ADJMM_ANY_VALUE : uint8_t(value);
    setOp(opNameFor(row, strip), op);
    // "learn strip 1 fills the row": strips 2..8 follow with the next numbers
    if (strip == 1 && fill_->isChecked() && row < 6) {
        for (int s = 2; s <= kStrips; ++s) {
            AdjmmOp o = op;
            const int n = (isNote ? controller : controller) + (s - 1);
            if (n > 127)
                break;
            if (isNote) o.value = uint8_t(n); else o.controller = uint8_t(n);
            ops_[opNameFor(row, s)] = o;
        }
        fillTable();
    }
    status_->setText(tr("%1 = %2").arg(opNameFor(row, strip), cellText(row, strip)));
}

bool ControllerDialog::saveFile()
{
    QString path = control_->path();
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("Save controller map"), QString(), tr("adj midi maps (*.adjmm)"));
        if (path.isEmpty())
            return false;
    }
    // only what differs from the file is written, so the lines the human did not touch stay as they were
    std::map<QString, AdjmmOp> changed;
    for (const auto& kv : ops_) {
        const auto o = orig_.find(kv.first);
        const AdjmmOp& n = kv.second;
        if (o == orig_.end()) {
            if (n.controller != ADJMM_UNSET) changed[kv.first] = n;
        } else if (o->second.channel != n.channel || o->second.controller != n.controller || o->second.value != n.value) {
            changed[kv.first] = n;
        }
    }
    QHash<QString, QString> all;
    all.insert(QStringLiteral("source"), source_->text().trimmed());
    all.insert(QStringLiteral("pickup"), pickup_->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
    all.insert(QStringLiteral("rec_is_forward"), recForward_->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
    QHash<QString, QString> dirs;
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        const QString was = origDirectives_.value(it.key());
        const bool had = origDirectives_.contains(it.key());
        const bool isDefault = it.key() == QLatin1String("source") ? it.value().isEmpty() : it.value() == QLatin1String("1");
        if (had ? was != it.value() : !isDefault)
            dirs.insert(it.key(), it.value());
    }
    QString err;
    if (!writeSamplerOps(path, changed, dirs, &err)) {
        status_->setText(err);
        return false;
    }
    if (!control_->loadFromFile(path)) {
        status_->setText(tr("Saved, but cannot read %1").arg(path));
        return false;
    }
    reloadFromFile();
    status_->setText(tr("Saved %1").arg(path));
    emit saved(path);
    return true;
}

} // namespace vdj
