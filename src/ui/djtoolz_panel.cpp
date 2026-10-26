#include "djtoolz_panel.h"
#include "sample_tree_model.h"
#include "djtoolz_tool_widget.h"
#include "drum_editor.h"
#include "djtoolz_controller.h"
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

DjtoolzPanel::DjtoolzPanel(AlsaClock* clock, const QString& dataDir, QWidget* parent)
    : QWidget(parent)
    , clock_(clock)
    , source_(clock)
    , scheduler_(&source_, &cache_, [this](const Trigger& t) { engine_.post(t); })
    , midiOut_(clock)
    , presets_(dataDir)
    , kits_(dataDir)
{
    engine_.setFxRack(&fxRack_);
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

    // right: presets / add / master, then the tools
    add_ = new QToolButton(this);
    add_->setText(tr("Add tool"));
    add_->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(add_);
    for (auto t : { DjtoolzTool::Bombomatic, DjtoolzTool::Dembomatik, DjtoolzTool::DrumMachine, DjtoolzTool::StabPlayer, DjtoolzTool::AcidLoop, DjtoolzTool::OneShot, DjtoolzTool::MidiMatic, DjtoolzTool::Fx }) {
        const QString name = DjtoolzTool::create(t)->typeName();
        menu->addAction(name, this, [this, t]() { addTool(t); });
    }
    add_->setMenu(menu);
    presetBox_ = new QComboBox(this);
    presetBox_->setMinimumWidth(140);
    presetBox_->setToolTip(tr("Saved combinations of tools and settings"));
    loadPreset_ = new QPushButton(tr("Load"), this);
    loadPreset_->setToolTip(tr("Load the selected preset"));
    savePreset_ = new QPushButton(tr("Save"), this);
    savePreset_->setToolTip(tr("Save the current setup over the selected preset"));
    newPreset_ = new QPushButton(tr("New"), this);
    newPreset_->setToolTip(tr("Save the current setup as a new preset"));
    clearPreset_ = new QPushButton(tr("Clear"), this);
    clearPreset_->setToolTip(tr("Remove all the tools (nothing is saved)"));
    deletePreset_ = new QPushButton(tr("Delete"), this);
    columnsBox_ = new QComboBox(this);
    columnsBox_->addItem(tr("2 across"), 2);
    columnsBox_->addItem(tr("4 across"), 4);
    columnsBox_->setToolTip(tr("Tools per row"));
    controlModeBox_ = new QComboBox(this);
    controlModeBox_->addItem(tr("Controller: Tools"), 0);
    controlModeBox_->addItem(tr("Controller: DR-6S"), 1);
    controlModeBox_->setToolTip(tr("What the MIDI controller's strips drive: the tools, or the voices of a DR-6S"));
    controllerButton_ = new QPushButton(tr("Controller..."), this);
    controllerButton_->setToolTip(tr("The MIDI controller that drives the tools: which buttons, knobs and faders do what, and MIDI learn"));
    controller_ = new DjtoolzController(this);
    master_ = new QSlider(Qt::Horizontal, this);
    master_->setRange(0, 150);
    master_->setValue(100);
    master_->setMaximumWidth(120);
    master_->setToolTip(tr("DJ Tools volume"));
    status_ = new QLabel(this);
    auto* top = new QHBoxLayout;
    top->addWidget(add_);
    top->addWidget(new QLabel(tr("Preset"), this));
    top->addWidget(presetBox_);
    top->addWidget(loadPreset_);
    top->addWidget(savePreset_);
    top->addWidget(newPreset_);
    top->addWidget(deletePreset_);
    top->addWidget(clearPreset_);
    top->addStretch(1);
    top->addWidget(controlModeBox_);
    top->addWidget(controllerButton_);
    top->addWidget(columnsBox_);
    top->addWidget(status_);
    top->addWidget(new QLabel(tr("Vol"), this));
    top->addWidget(master_);

    toolsHost_ = new QWidget(this);
    toolsLayout_ = new QGridLayout(toolsHost_);
    toolsLayout_->setContentsMargins(0, 0, 0, 0);
    toolsLayout_->setSpacing(4);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(toolsHost_);
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

    connect(controlModeBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        controller_->setMode(controlModeBox_->itemData(i).toInt() == 1 ? DjtoolzController::DrumMachine : DjtoolzController::Tools);
        scheduleSave();
    });
    connect(controllerButton_, &QPushButton::clicked, this, &DjtoolzPanel::controllerDialogRequested);
    connect(controller_, &DjtoolzController::modeChanged, this, [this](int m) { setControlMode(m); });
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
    connect(presetBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updatePresetButtons(); });
    connect(loadPreset_, &QPushButton::clicked, this, [this]() {
        if (presetBox_->currentIndex() > 0)
            loadPresetByName(presetBox_->currentText());
    });
    connect(savePreset_, &QPushButton::clicked, this, [this]() {
        if (presetBox_->currentIndex() <= 0)
            return;
        const QString name = presetBox_->currentText();
        if (!savePresetAs(name))
            QMessageBox::warning(this, tr("Save preset"), tr("Cannot save the preset \"%1\"").arg(name));
    });
    connect(newPreset_, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New preset"), tr("Name"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty() && !savePresetAs(name))
            QMessageBox::warning(this, tr("New preset"), tr("Cannot save the preset \"%1\"").arg(name));
    });
    connect(clearPreset_, &QPushButton::clicked, this, &DjtoolzPanel::clearTools);
    connect(deletePreset_, &QPushButton::clicked, this, [this]() {
        if (presetBox_->currentIndex() <= 0)
            return;
        const QString name = presetBox_->currentText();
        if (QMessageBox::question(this, tr("Delete preset"), tr("Delete the preset \"%1\"?").arg(name)) == QMessageBox::Yes) {
            presets_.remove(name);
            refreshPresetList();
        }
    });
    connect(&engine_, &DjtoolzEngine::deviceError, this, [this](const QString& m) { emit message(m); });
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(300);
    connect(&saveTimer_, &QTimer::timeout, this, [this]() { saveCurrentNow(); });
    pollTimer_.setInterval(100);
    connect(&pollTimer_, &QTimer::timeout, this, [this]() { poll(); });
    pollTimer_.start();

    refreshPresetList();
    DjtoolzState st;
    if (presets_.loadCurrent(&st))
        applyState(st);
    poll();
}

DjtoolzPanel::~DjtoolzPanel()
{
    shutdown();
}

void DjtoolzPanel::shutdown()
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

void DjtoolzPanel::setLatencyMs(double ms)
{
    engine_.setLatencyMs(ms);
    scheduler_.setImmediateLeadMs(engine_.latencyMs() + kImmediateMarginMs);
}

void DjtoolzPanel::ensureRunning()
{
    if (running_ || shutDown_ || !audioEnabled_)
        return;
    running_ = true;
    engine_.start();
    scheduler_.start();
}

DjtoolzToolWidget* DjtoolzPanel::addTool(DjtoolzTool::Type type)
{
    auto m = DjtoolzTool::create(type);
    if (!m)
        return nullptr;
    scheduler_.addTool(m);
    if (m->isEffect())
        fxRack_.add(std::static_pointer_cast<FxTool>(m));
    auto* w = new DjtoolzToolWidget(m, this, toolsHost_);
    connectToolWidget(w);
    widgets_.append(w);
    relayout();
    scheduleSave();
    return w;
}

void DjtoolzPanel::connectToolWidget(DjtoolzToolWidget* w)
{
    connect(w, &DjtoolzToolWidget::removeRequested, this, [this](int id) { removeTool(id); });
    connect(w, &DjtoolzToolWidget::touched, this, [this](int id) { lastTouchedId_ = id; });
}

void DjtoolzPanel::removeTool(int toolId)
{
    for (int i = 0; i < widgets_.size(); ++i) {
        if (widgets_[i]->tool()->id() != toolId)
            continue;
        scheduler_.removeTool(toolId);
        fxRack_.remove(toolId);
        engine_.stopTag(toolId);
        DjtoolzToolWidget* w = widgets_.takeAt(i);
        toolsLayout_->removeWidget(w);
        w->hide();
        w->deleteLater();
        relayout();
        scheduleSave();
        return;
    }
}

DjtoolzToolWidget* DjtoolzPanel::drumTarget() const
{
    DjtoolzToolWidget* first = nullptr;
    for (DjtoolzToolWidget* w : widgets_) {
        if (!w->drumEditor())
            continue;
        if (w->tool()->id() == lastTouchedId_)
            return w;
        if (!first)
            first = w;
    }
    return first;
}

void DjtoolzPanel::setControlMode(int mode)
{
    const int i = controlModeBox_->findData(mode == 1 ? 1 : 0);
    if (i != controlModeBox_->currentIndex())
        controlModeBox_->setCurrentIndex(i);          // its signal tells the controller
}

void DjtoolzPanel::toolEdited(int toolId)
{
    if (!loading_)
        lastTouchedId_ = toolId;
    controller_->softwareChanged(toolId);
    if (!loading_)
        scheduleSave();
}

QList<MidiPort> DjtoolzPanel::midiDestinations()
{
    router_.open();
    QList<MidiPort> out;
    for (const MidiPort& p : router_.outputs())
        if (clock_ == nullptr || p.clientId != clock_->clientId())
            out.append(p);
    return out;
}

DjtoolzState DjtoolzPanel::state() const
{
    std::vector<std::shared_ptr<DjtoolzTool>> mods;
    for (DjtoolzToolWidget* w : widgets_)
        mods.push_back(w->tool());
    DjtoolzState st = djtoolzStateFrom(mods, folder_, masterGain());
    st.columns = columns();
    st.controlMode = controlModeBox_->currentData().toInt();
    return st;
}

double DjtoolzPanel::masterGain() const
{
    return double(master_->value()) / 100.0;
}

void DjtoolzPanel::applyState(const DjtoolzState& st)
{
    loading_ = true;
    for (DjtoolzToolWidget* w : widgets_) {
        scheduler_.removeTool(w->tool()->id());
        fxRack_.remove(w->tool()->id());
        engine_.stopTag(w->tool()->id());
        toolsLayout_->removeWidget(w);
        w->hide();
        w->deleteLater();
    }
    widgets_.clear();
    master_->setValue(int(std::lround(st.masterGain * 100.0)));
    setColumns(st.columns);
    setControlMode(st.controlMode);
    setSampleFolder(st.folder);
    QStringList warnings;
    for (const auto& m : djtoolzToolsFrom(st, &warnings)) {
        scheduler_.addTool(m);
    if (m->isEffect())
        fxRack_.add(std::static_pointer_cast<FxTool>(m));
        auto* w = new DjtoolzToolWidget(m, this, toolsHost_);
        connectToolWidget(w);
        widgets_.append(w);
        if (m->enabled())
            ensureRunning();
    }
    if (!warnings.isEmpty())
        emit message(tr("DJ Tools preset: %1").arg(warnings.join(QStringLiteral("; "))));
    relayout();
    loading_ = false;
    controller_->resetPickups();
    scheduleSave();
}

bool DjtoolzPanel::toggleToolAt(int index)
{
    if (index < 0 || index >= widgets_.size())
        return false;
    widgets_[index]->onButton()->click();
    return true;
}

int DjtoolzPanel::columns() const
{
    return columnsBox_->currentData().toInt() == 4 ? 4 : 2;
}

void DjtoolzPanel::setColumns(int columns)
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

QRect DjtoolzPanel::gridCell(int index) const
{
    // the same flow as relayout(): left to right, then down; a tool that wants a whole row starts a new row and fills it
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

// Tools fill the grid left to right, then down; every column the same width, the spare height below the last row.
void DjtoolzPanel::relayout()
{
    const int cols = columns();
    for (DjtoolzToolWidget* w : widgets_)
        toolsLayout_->removeWidget(w);
    for (int c = 0; c < 4; ++c)
        toolsLayout_->setColumnStretch(c, c < cols ? 1 : 0);
    for (int r = 0; r <= widgets_.size() + 1; ++r)
        toolsLayout_->setRowStretch(r, 0);
    int lastRow = 0;
    for (int i = 0; i < widgets_.size(); ++i) {
        const QRect r = gridCell(i);
        toolsLayout_->addWidget(widgets_[i], r.y(), r.x(), 1, r.width());
        lastRow = std::max(lastRow, r.y());
        widgets_[i]->setKeyLabel(i < kFunctionKeys ? QStringLiteral("F%1").arg(i + 1) : QString());
        widgets_[i]->show();
    }
    toolsLayout_->setRowStretch(lastRow + 1, 1);
}

void DjtoolzPanel::setSampleFolder(const QString& folder)
{
    folder_ = folder;
    tree_->setRootIndex(treeModel_->setRootFolder(folder));
    folderButton_->setToolTip(folder);
    for (DjtoolzToolWidget* w : widgets_)
        if (w->sampleChooser())
            w->sampleChooser()->setFolder(folder);
    scheduleSave();
}

void DjtoolzPanel::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Sample folder"), folder_);
    if (!dir.isEmpty())
        setSampleFolder(dir);
}

void DjtoolzPanel::previewSample(const QString& path)
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

QMenu* DjtoolzPanel::sendToMenu(const QModelIndex& index, QWidget* parent)
{
    const QString path = treeModel_->samplePath(index);
    if (path.isEmpty())
        return nullptr;
    auto* menu = new QMenu(parent);
    menu->addAction(tr("Preview"), this, [this, path]() { previewSample(path); });
    menu->addSeparator();
    int n = 0;
    for (DjtoolzToolWidget* w : widgets_) {
        if (DrumEditor* drums = w->drumEditor()) {   // the DR-6S has six places for a sample: a submenu
            ++n;
            QMenu* sub = menu->addMenu(tr("Send to %1").arg(w->displayName()));
            for (int v = 0; v < kDrumVoices; ++v)
                sub->addAction(QString::fromLatin1(kDrumVoiceNames[v]), this, [drums, v, path]() { drums->setVoiceSample(v, path); });
            continue;
        }
        if (!w->sampleChooser())
            continue;                                // a MIDI tool has no sample
        ++n;
        menu->addAction(tr("Send to %1").arg(w->displayName()), this, [w, path]() { w->setSample(path); });
    }
    if (n == 0)
        menu->addAction(tr("Send to... (add a tool first)"))->setEnabled(false);
    return menu;
}

void DjtoolzPanel::scheduleSave()
{
    if (!loading_ && !shutDown_)
        saveTimer_.start();
}

void DjtoolzPanel::saveCurrentNow()
{
    presets_.saveCurrent(state());
}

void DjtoolzPanel::refreshPresetList()
{
    const QString keep = presetBox_->currentIndex() > 0 ? presetBox_->currentText() : QString();
    QSignalBlocker block(presetBox_);
    presetBox_->clear();
    presetBox_->addItem(tr("(presets)"));
    presetBox_->addItems(presets_.list());
    const int i = presetBox_->findText(keep);
    presetBox_->setCurrentIndex(i > 0 ? i : 0);
    updatePresetButtons();
}

void DjtoolzPanel::updatePresetButtons()
{
    const bool selected = presetBox_->currentIndex() > 0;
    loadPreset_->setEnabled(selected);
    savePreset_->setEnabled(selected);
    deletePreset_->setEnabled(selected);
}

void DjtoolzPanel::clearTools()
{
    DjtoolzState st = state();
    st.tools.clear();
    applyState(st);
    scheduleSave();
}

bool DjtoolzPanel::savePresetAs(const QString& name)
{
    if (!presets_.save(name, state()))
        return false;
    refreshPresetList();
    const int i = presetBox_->findText(DjtoolzPresets::sanitise(name));
    if (i > 0)
        presetBox_->setCurrentIndex(i);
    return true;
}

bool DjtoolzPanel::loadPresetByName(const QString& name)
{
    DjtoolzState st;
    QString err;
    if (!presets_.load(name, &st, &err)) {
        emit message(tr("Cannot load preset: %1").arg(err));
        return false;
    }
    applyState(st);
    return true;
}

void DjtoolzPanel::poll()
{
    const double beat = scheduler_.currentBeat();
    if (source_.running())
        status_->setText(tr("Sequencer %1 BPM").arg(source_.bpm(), 0, 'f', 1));
    else
        status_->setText(tr("Sequencer stopped"));
    for (DjtoolzToolWidget* w : widgets_)
        w->refresh(beat);

    // timing self-test: while the debug mode records, say how far each sound was rendered from the time it was scheduled for
    const bool debug = PacketLogger::instance().isEnabled();
    engine_.setTimingEnabled(debug);
    if (debug) {
        for (const auto& t : engine_.drainTiming())
            PacketLogger::instance().log(QStringLiteral("SYS"), QStringLiteral("DJTOOLZ tool %1: rendered %2 ms from its scheduled time")
                                                                   .arg(t.tag).arg(double(t.errorNs) / 1e6, 0, 'f', 3));
    }
}

} // namespace vdj
