#include "sampler_panel.h"
#include "sample_tree_model.h"
#include "sampler_module_widget.h"
#include "sample_chooser.h"
#include "midi/alsa_clock.h"
#include "debug/packet_logger.h"

#include <QComboBox>
#include <algorithm>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace vdj {

namespace {
constexpr double kImmediateMarginMs = 20.0;
}

SamplerPanel::SamplerPanel(AlsaClock* clock, const QString& dataDir, QWidget* parent)
    : QWidget(parent)
    , clock_(clock)
    , source_(clock)
    , scheduler_(&source_, &cache_, [this](const Trigger& t) { engine_.post(t); })
    , midiOut_(clock)
    , presets_(dataDir)
    , kits_(dataDir)
{
    scheduler_.setStopSink([this](int tag, qint64 ns) { engine_.stopTagAt(tag, ns); });
    scheduler_.setMidiSink([this](const MidiNoteEvent& e) { midiOut_.send(e); });
    scheduler_.setImmediateLeadMs(engine_.latencyMs() + kImmediateMarginMs);

    // left: the sample folder
    treeModel_ = new SampleTreeModel(this);
    tree_ = new QTreeView(this);
    tree_->setModel(treeModel_);
    for (int c = 1; c < treeModel_->columnCount(); ++c)
        tree_->hideColumn(c);
    tree_->setHeaderHidden(true);
    tree_->setToolTip(tr("Double click a .wav to hear it"));
    folderButton_ = new QPushButton(tr("Sample folder..."), this);
    auto* left = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(folderButton_);
    leftLayout->addWidget(tree_, 1);

    // right: presets / add / master, then the modules
    add_ = new QToolButton(this);
    add_->setText(tr("Add module"));
    add_->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(add_);
    for (auto t : { SamplerModule::Bombomatic, SamplerModule::Dembomatik, SamplerModule::DrumMachine, SamplerModule::StabPlayer, SamplerModule::AcidLoop, SamplerModule::OneShot, SamplerModule::MidiMatic }) {
        const QString name = SamplerModule::create(t)->typeName();
        menu->addAction(name, this, [this, t]() { addModule(t); });
    }
    add_->setMenu(menu);
    presetBox_ = new QComboBox(this);
    presetBox_->setMinimumWidth(140);
    presetBox_->setToolTip(tr("Saved combinations of modules and settings"));
    savePreset_ = new QPushButton(tr("Save"), this);
    deletePreset_ = new QPushButton(tr("Delete"), this);
    columnsBox_ = new QComboBox(this);
    columnsBox_->addItem(tr("2 across"), 2);
    columnsBox_->addItem(tr("4 across"), 4);
    columnsBox_->setToolTip(tr("Modules per row"));
    master_ = new QSlider(Qt::Horizontal, this);
    master_->setRange(0, 150);
    master_->setValue(100);
    master_->setMaximumWidth(120);
    master_->setToolTip(tr("Sampler volume"));
    status_ = new QLabel(this);
    auto* top = new QHBoxLayout;
    top->addWidget(add_);
    top->addWidget(new QLabel(tr("Preset"), this));
    top->addWidget(presetBox_);
    top->addWidget(savePreset_);
    top->addWidget(deletePreset_);
    top->addStretch(1);
    top->addWidget(columnsBox_);
    top->addWidget(status_);
    top->addWidget(new QLabel(tr("Vol"), this));
    top->addWidget(master_);

    modulesHost_ = new QWidget(this);
    modulesLayout_ = new QGridLayout(modulesHost_);
    modulesLayout_->setContentsMargins(0, 0, 0, 0);
    modulesLayout_->setSpacing(4);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(modulesHost_);
    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addLayout(top);
    rightLayout->addWidget(scroll, 1);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->addWidget(split);
    setMinimumHeight(150);

    connect(folderButton_, &QPushButton::clicked, this, [this]() { chooseFolder(); });
    connect(tree_, &QTreeView::doubleClicked, this, [this](const QModelIndex& i) {
        const QString p = treeModel_->samplePath(i);
        if (!p.isEmpty())
            previewSample(p);
    });
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeView::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex i = tree_->indexAt(pos);
        if (QMenu* m = sendToMenu(i, tree_)) {
            m->exec(tree_->viewport()->mapToGlobal(pos));
            m->deleteLater();
        }
    });
    connect(columnsBox_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { setColumns(columnsBox_->itemData(i).toInt()); });
    connect(master_, &QSlider::valueChanged, this, [this](int v) {
        engine_.setMasterGain(float(v) / 100.0f);
        scheduleSave();
    });
    connect(presetBox_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (i > 0)
            loadPresetByName(presetBox_->itemText(i));
    });
    connect(savePreset_, &QPushButton::clicked, this, [this]() {
        const QString current = presetBox_->currentIndex() > 0 ? presetBox_->currentText() : QString();
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Name"), QLineEdit::Normal, current, &ok);
        if (ok && !name.trimmed().isEmpty() && !savePresetAs(name))
            QMessageBox::warning(this, tr("Save preset"), tr("Cannot save the preset \"%1\"").arg(name));
    });
    connect(deletePreset_, &QPushButton::clicked, this, [this]() {
        if (presetBox_->currentIndex() <= 0)
            return;
        const QString name = presetBox_->currentText();
        if (QMessageBox::question(this, tr("Delete preset"), tr("Delete the preset \"%1\"?").arg(name)) == QMessageBox::Yes) {
            presets_.remove(name);
            refreshPresetList();
        }
    });
    connect(&engine_, &SamplerEngine::deviceError, this, [this](const QString& m) { emit message(m); });
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(300);
    connect(&saveTimer_, &QTimer::timeout, this, [this]() { saveCurrentNow(); });
    pollTimer_.setInterval(100);
    connect(&pollTimer_, &QTimer::timeout, this, [this]() { poll(); });
    pollTimer_.start();

    refreshPresetList();
    SamplerState st;
    if (presets_.loadCurrent(&st))
        applyState(st);
    poll();
}

SamplerPanel::~SamplerPanel()
{
    shutdown();
}

void SamplerPanel::shutdown()
{
    if (shutDown_)
        return;
    shutDown_ = true;
    pollTimer_.stop();
    if (saveTimer_.isActive()) {
        saveTimer_.stop();
        saveCurrentNow();
    }
    scheduler_.stop();
    engine_.stop();
    running_ = false;
}

void SamplerPanel::setLatencyMs(double ms)
{
    engine_.setLatencyMs(ms);
    scheduler_.setImmediateLeadMs(engine_.latencyMs() + kImmediateMarginMs);
}

void SamplerPanel::ensureRunning()
{
    if (running_ || shutDown_ || !audioEnabled_)
        return;
    running_ = true;
    engine_.start();
    scheduler_.start();
}

SamplerModuleWidget* SamplerPanel::addModule(SamplerModule::Type type)
{
    auto m = SamplerModule::create(type);
    if (!m)
        return nullptr;
    scheduler_.addModule(m);
    auto* w = new SamplerModuleWidget(m, this, modulesHost_);
    connectModuleWidget(w);
    widgets_.append(w);
    relayout();
    scheduleSave();
    return w;
}

void SamplerPanel::connectModuleWidget(SamplerModuleWidget* w)
{
    connect(w, &SamplerModuleWidget::removeRequested, this, [this](int id) { removeModule(id); });
}

void SamplerPanel::removeModule(int moduleId)
{
    for (int i = 0; i < widgets_.size(); ++i) {
        if (widgets_[i]->module()->id() != moduleId)
            continue;
        scheduler_.removeModule(moduleId);
        engine_.stopTag(moduleId);
        SamplerModuleWidget* w = widgets_.takeAt(i);
        modulesLayout_->removeWidget(w);
        w->hide();
        w->deleteLater();
        relayout();
        scheduleSave();
        return;
    }
}

void SamplerPanel::moduleEdited(int moduleId)
{
    Q_UNUSED(moduleId);
    if (!loading_)
        scheduleSave();
}

QList<MidiPort> SamplerPanel::midiDestinations()
{
    router_.open();
    QList<MidiPort> out;
    for (const MidiPort& p : router_.outputs())
        if (clock_ == nullptr || p.clientId != clock_->clientId())
            out.append(p);
    return out;
}

SamplerState SamplerPanel::state() const
{
    std::vector<std::shared_ptr<SamplerModule>> mods;
    for (SamplerModuleWidget* w : widgets_)
        mods.push_back(w->module());
    SamplerState st = samplerStateFrom(mods, folder_, masterGain());
    st.columns = columns();
    return st;
}

double SamplerPanel::masterGain() const
{
    return double(master_->value()) / 100.0;
}

void SamplerPanel::applyState(const SamplerState& st)
{
    loading_ = true;
    for (SamplerModuleWidget* w : widgets_) {
        scheduler_.removeModule(w->module()->id());
        engine_.stopTag(w->module()->id());
        modulesLayout_->removeWidget(w);
        w->hide();
        w->deleteLater();
    }
    widgets_.clear();
    master_->setValue(int(std::lround(st.masterGain * 100.0)));
    setColumns(st.columns);
    setSampleFolder(st.folder);
    QStringList warnings;
    for (const auto& m : samplerModulesFrom(st, &warnings)) {
        scheduler_.addModule(m);
        auto* w = new SamplerModuleWidget(m, this, modulesHost_);
        connectModuleWidget(w);
        widgets_.append(w);
        if (m->enabled())
            ensureRunning();
    }
    if (!warnings.isEmpty())
        emit message(tr("Sampler preset: %1").arg(warnings.join(QStringLiteral("; "))));
    relayout();
    loading_ = false;
    scheduleSave();
}

bool SamplerPanel::toggleModuleAt(int index)
{
    if (index < 0 || index >= widgets_.size())
        return false;
    widgets_[index]->onButton()->click();
    return true;
}

int SamplerPanel::columns() const
{
    return columnsBox_->currentData().toInt() == 4 ? 4 : 2;
}

void SamplerPanel::setColumns(int columns)
{
    const int i = columnsBox_->findData(columns == 4 ? 4 : 2);
    if (i == columnsBox_->currentIndex()) {
        relayout();
        return;
    }
    columnsBox_->setCurrentIndex(i);
    relayout();
    scheduleSave();
}

QRect SamplerPanel::gridCell(int index) const
{
    // the same flow as relayout(): left to right, then down; a module that wants a whole row starts a new row and fills it
    const int cols = columns();
    int row = 0, col = 0;
    for (int i = 0; i < widgets_.size(); ++i) {
        const bool full = widgets_[i]->wantsFullRow();
        if (full && col != 0) {
            ++row;
            col = 0;
        }
        const QRect r(col, row, full ? cols : 1, 1);
        if (i == index)
            return r;
        col += r.width();
        if (col >= cols) {
            col = 0;
            ++row;
        }
    }
    return QRect();
}

// Modules fill the grid left to right, then down; every column the same width, the spare height below the last row.
void SamplerPanel::relayout()
{
    const int cols = columns();
    for (SamplerModuleWidget* w : widgets_)
        modulesLayout_->removeWidget(w);
    for (int c = 0; c < 4; ++c)
        modulesLayout_->setColumnStretch(c, c < cols ? 1 : 0);
    for (int r = 0; r <= widgets_.size() + 1; ++r)
        modulesLayout_->setRowStretch(r, 0);
    int lastRow = 0;
    for (int i = 0; i < widgets_.size(); ++i) {
        const QRect r = gridCell(i);
        modulesLayout_->addWidget(widgets_[i], r.y(), r.x(), 1, r.width());
        lastRow = std::max(lastRow, r.y());
        widgets_[i]->setKeyLabel(i < kFunctionKeys ? QStringLiteral("F%1").arg(i + 1) : QString());
        widgets_[i]->show();
    }
    modulesLayout_->setRowStretch(lastRow + 1, 1);
}

void SamplerPanel::setSampleFolder(const QString& folder)
{
    folder_ = folder;
    tree_->setRootIndex(treeModel_->setRootFolder(folder));
    folderButton_->setToolTip(folder);
    for (SamplerModuleWidget* w : widgets_)
        if (w->sampleChooser())
            w->sampleChooser()->setFolder(folder);
    scheduleSave();
}

void SamplerPanel::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Sample folder"), folder_);
    if (!dir.isEmpty())
        setSampleFolder(dir);
}

void SamplerPanel::previewSample(const QString& path)
{
    if (!audioEnabled_)
        return;
    QString err;
    auto s = cache_.get(path, &err);
    if (!s) {
        emit message(tr("Cannot play %1: %2").arg(path, err));
        return;
    }
    ensureRunning();
    engine_.stopTag(kPreviewTag);
    Trigger t;
    t.sample = s;
    t.tag = kPreviewTag;
    t.steadyNs = BeatSource::steadyNowNs() + qint64((engine_.latencyMs() + kImmediateMarginMs) * 1e6);
    engine_.post(t);
}

QMenu* SamplerPanel::sendToMenu(const QModelIndex& index, QWidget* parent)
{
    const QString path = treeModel_->samplePath(index);
    if (path.isEmpty())
        return nullptr;
    auto* menu = new QMenu(parent);
    menu->addAction(tr("Preview"), this, [this, path]() { previewSample(path); });
    menu->addSeparator();
    int n = 0;
    for (SamplerModuleWidget* w : widgets_) {
        if (!w->sampleChooser())
            continue;                                // a MIDI module has no sample
        ++n;
        menu->addAction(tr("Send to %1").arg(w->displayName()), this, [w, path]() { w->setSample(path); });
    }
    if (n == 0)
        menu->addAction(tr("Send to... (add a module first)"))->setEnabled(false);
    return menu;
}

void SamplerPanel::scheduleSave()
{
    if (!loading_ && !shutDown_)
        saveTimer_.start();
}

void SamplerPanel::saveCurrentNow()
{
    presets_.saveCurrent(state());
}

void SamplerPanel::refreshPresetList()
{
    const QString keep = presetBox_->currentIndex() > 0 ? presetBox_->currentText() : QString();
    QSignalBlocker block(presetBox_);
    presetBox_->clear();
    presetBox_->addItem(tr("(presets)"));
    presetBox_->addItems(presets_.list());
    const int i = presetBox_->findText(keep);
    presetBox_->setCurrentIndex(i > 0 ? i : 0);
    deletePreset_->setEnabled(presetBox_->count() > 1);
}

bool SamplerPanel::savePresetAs(const QString& name)
{
    if (!presets_.save(name, state()))
        return false;
    refreshPresetList();
    const int i = presetBox_->findText(SamplerPresets::sanitise(name));
    if (i > 0)
        presetBox_->setCurrentIndex(i);
    return true;
}

bool SamplerPanel::loadPresetByName(const QString& name)
{
    SamplerState st;
    QString err;
    if (!presets_.load(name, &st, &err)) {
        emit message(tr("Cannot load preset: %1").arg(err));
        return false;
    }
    applyState(st);
    return true;
}

void SamplerPanel::poll()
{
    const double beat = scheduler_.currentBeat();
    if (source_.running())
        status_->setText(tr("Sequencer %1 BPM").arg(source_.bpm(), 0, 'f', 1));
    else
        status_->setText(tr("Sequencer stopped"));
    for (SamplerModuleWidget* w : widgets_)
        w->refresh(beat);

    // timing self-test: while the debug mode records, say how far each sound was rendered from the time it was scheduled for
    const bool debug = PacketLogger::instance().isEnabled();
    engine_.setTimingEnabled(debug);
    if (debug) {
        for (const auto& t : engine_.drainTiming())
            PacketLogger::instance().log(QStringLiteral("SYS"), QStringLiteral("SAMPLER module %1: rendered %2 ms from its scheduled time")
                                                                   .arg(t.tag).arg(double(t.errorNs) / 1e6, 0, 'f', 3));
    }
}

} // namespace vdj
