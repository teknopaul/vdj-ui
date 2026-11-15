#include "studio_mix_window.h"

#include "config/config_manager.h"
#include "trakcrate/analysis.h"
#include "trakcrate/mix_file.h"
#include "trakcrate/mix_render.h"
#include "mix_filters_dialog.h"
#include "mix_previewer.h"
#include "mix_render_dialog.h"
#include "mix_stretch_dock.h"
#include "mix_tree_dock.h"
#include "trakcrate_edit_panel.h"
#include "trakcrate/stretch_engine.h"
#include "trakcrate/edit_job.h"
#include "trakcrate_analysis.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QCoreApplication>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QRadioButton>
#include <QTextEdit>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QUndoCommand>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QThread>
#include <QToolBar>
#include <QUndoStack>
#include <QVBoxLayout>

#include <cmath>

namespace vdj {

namespace {
const QString kSection = QStringLiteral("studiomix");

// The whole Mix before and after: the document is small, so undo is a snapshot swap.
class MixSnapshotCommand : public QUndoCommand {
public:
    MixSnapshotCommand(StudioMixWindow* w, trakcrate::Mix before, trakcrate::Mix after, const QString& text, const QString& key)
        : QUndoCommand(text), w_(w), before_(std::move(before)), after_(std::move(after)), key_(key) {}
    void undo() override { w_->applyMix(before_); }
    void redo() override { w_->applyMix(after_); }
    int id() const override { return key_.isEmpty() ? -1 : 7001; }
    bool mergeWith(const QUndoCommand* other) override
    {
        const auto* o = dynamic_cast<const MixSnapshotCommand*>(other);
        if (!o || key_.isEmpty() || o->key_ != key_) return false;
        after_ = o->after_;
        return true;
    }

private:
    StudioMixWindow* w_;
    trakcrate::Mix before_, after_;
    QString key_;
};

QString trackTitle(const trakcrate::Track& t, const trakcrate::fs::path& dir)
{
    const QString title = QString::fromStdString(t.title());
    return title.isEmpty() ? QString::fromStdString(dir.filename().string()) : title;
}
}

// ---- NewMixDialog ------------------------------------------------------------------------------------------------------------------

NewMixDialog::NewMixDialog(const trakcrate::Library* lib, QWidget* parent) : QDialog(parent), lib_(lib)
{
    setWindowTitle(QStringLiteral("New studio mix from a playlist"));
    resize(780, 480);
    auto* top = new QVBoxLayout(this);
    auto* row = new QHBoxLayout;
    playlists_ = new QListWidget;
    tracks_ = new QListWidget;
    tracks_->setSelectionMode(QAbstractItemView::NoSelection);
    row->addWidget(playlists_, 1);
    row->addWidget(tracks_, 2);
    top->addLayout(row, 1);
    note_ = new QLabel;
    top->addWidget(note_);
    auto* buttons = new QHBoxLayout;
    analyse_ = new QPushButton(QStringLiteral("Analyse missing"));
    ok_ = new QPushButton(QStringLiteral("Create mix"));
    auto* cancel = new QPushButton(QStringLiteral("Cancel"));
    ok_->setDefault(true);
    buttons->addWidget(analyse_);
    buttons->addStretch(1);
    buttons->addWidget(ok_);
    buttons->addWidget(cancel);
    top->addLayout(buttons);
    if (lib_)
        for (const std::string& n : lib_->listPlaylists()) playlists_->addItem(QString::fromStdString(n));
    connect(playlists_, &QListWidget::currentRowChanged, this, [this] { fillTracks(); });
    connect(analyse_, &QPushButton::clicked, this, &NewMixDialog::startAnalysis);
    connect(ok_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, [this] { cancel_ = true; reject(); });
    fillTracks();
}

void NewMixDialog::selectPlaylist(const QString& name)
{
    const auto items = playlists_->findItems(name, Qt::MatchExactly);
    if (!items.isEmpty()) playlists_->setCurrentItem(items.first());
}

QString NewMixDialog::playlist() const
{
    return playlists_->currentItem() ? playlists_->currentItem()->text() : QString();
}

void NewMixDialog::fillTracks()
{
    tracks_->clear();
    unanalysed_ = 0;
    int good = 0;
    if (lib_ && playlists_->currentItem()) {
        for (const trakcrate::fs::path& dir : lib_->playlistTracks(playlist().toStdString())) {
            trakcrate::Track t;
            trakcrate::loadTrack(dir, &t);
            const std::string problem = trakcrate::analysedProblem(dir);
            auto* item = new QListWidgetItem(trackTitle(t, dir));
            if (!problem.empty()) {
                item->setText(item->text() + QStringLiteral("  -  ") + QString::fromStdString(problem) + QStringLiteral(" (left out)"));
                item->setForeground(QColor(220, 50, 50));
                ++unanalysed_;
            } else {
                ++good;
            }
            tracks_->addItem(item);
        }
    }
    analyse_->setEnabled(unanalysed_ > 0 && !worker_);
    ok_->setEnabled(good > 0);
    note_->setText(good > 0 ? QString() : QStringLiteral("Studio mixes need analysed tracks."));
}

std::vector<trakcrate::MixTrackInput> NewMixDialog::tracks() const
{
    std::vector<trakcrate::MixTrackInput> out;
    if (!lib_ || !playlists_->currentItem()) return out;
    for (const trakcrate::fs::path& dir : lib_->playlistTracks(playlist().toStdString())) {
        if (!trakcrate::isAnalysed(dir)) continue;
        trakcrate::MixTrackInput in;
        in.file = trakcrate::audioFileOf(dir);
        std::string err;
        if (in.file.empty() || !trakcrate::readTrackFacts(in.file, &in.facts, &err)) continue;
        out.push_back(in);
    }
    return out;
}

void NewMixDialog::startAnalysis()
{
    if (worker_ || !lib_ || !playlists_->currentItem()) return;
    std::vector<trakcrate::fs::path> todo;
    for (const trakcrate::fs::path& dir : lib_->playlistTracks(playlist().toStdString()))
        if (!trakcrate::isAnalysed(dir)) todo.push_back(dir);
    cancel_ = false;
    analyse_->setEnabled(false);
    ok_->setEnabled(false);
    note_->setText(QStringLiteral("Analysing %1 track(s)...").arg(todo.size()));
    worker_ = QThread::create([todo, this] {
        for (const auto& dir : todo) {
            if (cancel_) return;
            trakcrate::AnalyseOptions opts;
            trakcrate::analyseTrack(dir, opts, &cancel_);
        }
    });
    connect(worker_, &QThread::finished, this, [this] {
        worker_->deleteLater();
        worker_ = nullptr;
        fillTracks();
    });
    worker_->start();
}

void NewMixDialog::waitForAnalysis()
{
    if (!worker_) return;
    worker_->wait();
    for (int i = 0; i < 50 && worker_; ++i) QCoreApplication::processEvents();      // delivers finished()
}

// ---- StudioMixWindow --------------------------------------------------------------------------------------------------------------------

StudioMixWindow::StudioMixWindow(LibraryProvider provider, ConfigManager* config, QWidget* parent)
    : QMainWindow(parent), provider_(std::move(provider)), config_(config)
{
    setWindowTitle(QStringLiteral("Studio mix"));
    setWindowFlag(Qt::WindowMinMaxButtonsHint, true);
    resize(1100, 600);
    undo_ = new QUndoStack(this);
    canvas_ = new MixCanvas;
    setCentralWidget(canvas_);
    buildMenus();

    auto* tb = addToolBar(QStringLiteral("Mix"));
    tb->setObjectName(QStringLiteral("mixToolbar"));
    tb->addWidget(new QLabel(QStringLiteral(" Master BPM: ")));
    bpmSpin_ = new QDoubleSpinBox;
    bpmSpin_->setRange(40, 300);
    bpmSpin_->setDecimals(2);
    bpmSpin_->setSingleStep(0.01);
    bpmSpin_->setToolTip(QStringLiteral("Every track is stretched to this tempo"));
    tb->addWidget(bpmSpin_);
    auto* useTrack = new QPushButton(QStringLiteral("Use track's bpm"));
    useTrack->setToolTip(QStringLiteral("Set the master tempo to the tempo of the selected track"));
    tb->addWidget(useTrack);
    connect(bpmSpin_, &QDoubleSpinBox::editingFinished, this, [this] { if (!filling_) setMasterBpm(bpmSpin_->value()); });
    connect(useTrack, &QPushButton::clicked, this, [this] {
        const int l = canvas_->selectedLane();
        if (l >= 0 && size_t(l) < facts_.size() && facts_[size_t(l)].grid.bpm > 0) setMasterBpm(facts_[size_t(l)].grid.bpm);
    });
    tb->addSeparator();
    playlistCombo_ = new QComboBox;
    playlistCombo_->setToolTip(QStringLiteral("New mix from playlist"));
    playlistCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    tb->addWidget(playlistCombo_);
    tb->addSeparator();
    tb->addWidget(new QLabel(QStringLiteral(" Zoom ")));
    zoom_ = new QSlider(Qt::Horizontal);
    zoom_->setRange(0, 1000);
    zoom_->setFixedWidth(160);
    tb->addWidget(zoom_);
    auto* fitBtn = new QPushButton(QStringLiteral("Fit mix period"));
    fitBtn->setToolTip(QStringLiteral("Centre the nearest mix period with 8 bars on each side (F; [ and ] go to the previous / next period)"));
    tb->addWidget(fitBtn);
    connect(fitBtn, &QPushButton::clicked, this, [this] { canvas_->fitPeriod(); canvas_->setFocus(); });

    posLabel_ = new QLabel(QStringLiteral("0:00"));
    hoverLabel_ = new QLabel;
    statusBar()->addWidget(posLabel_);
    statusBar()->addWidget(hoverLabel_, 1);

    // the selected track: volume trim and grid nudge
    auto* trackDock = new QDockWidget(QStringLiteral("Track"), this);
    trackDock->setObjectName(QStringLiteral("mixTrackDock"));
    auto* tw = new QWidget;
    auto* tl = new QVBoxLayout(tw);
    trackLabel_ = new QLabel(QStringLiteral("Click a track"));
    tl->addWidget(trackLabel_);
    trimSpin_ = new QDoubleSpinBox;
    trimSpin_->setRange(0, 200);
    trimSpin_->setDecimals(0);
    trimSpin_->setSuffix(QStringLiteral(" %"));
    trimSpin_->setValue(100);
    trimSpin_->setToolTip(QStringLiteral("Master volume of the track"));
    auto* trimRow = new QHBoxLayout;
    trimRow->addWidget(new QLabel(QStringLiteral("Volume")));
    trimRow->addWidget(trimSpin_);
    tl->addLayout(trimRow);
    auto* nudgeRow = new QHBoxLayout;
    for (double d : {-1.0, -0.1, 0.1, 1.0}) {
        auto* b = new QPushButton(QStringLiteral("%1%2").arg(d > 0 ? "+" : "").arg(d));
        b->setToolTip(QStringLiteral("Move the grid of this track by %1 ms (this mix only; the track's beat grid file is never changed)").arg(d));
        nudgeRow->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, d] { if (canvas_->selectedLane() >= 0) nudgeGrid(canvas_->selectedLane(), d); });
    }
    tl->addLayout(nudgeRow);
    auto* nudgeInfo = new QHBoxLayout;
    nudgeLabel_ = new QLabel(QStringLiteral("+0.0 ms"));
    auto* reset = new QPushButton(QStringLiteral("Reset"));
    nudgeInfo->addWidget(nudgeLabel_);
    nudgeInfo->addWidget(reset);
    tl->addLayout(nudgeInfo);
    connect(reset, &QPushButton::clicked, this, [this] { if (canvas_->selectedLane() >= 0) nudgeGrid(canvas_->selectedLane(), 0); });
    tl->addWidget(new QLabel(QStringLiteral("Curve to draw (keys 1 2 3):")));
    QRadioButton* radios[3];
    const char* curveNames[3] = {"Volume (yellow)", "Bass (red)", "High (cyan)"};
    for (int i = 0; i < 3; ++i) {
        radios[i] = new QRadioButton(QString::fromLatin1(curveNames[i]));
        radios[i]->setChecked(i == canvas_->curveMode());
        tl->addWidget(radios[i]);
        connect(radios[i], &QRadioButton::clicked, this, [this, i] { canvas_->setCurveMode(i); canvas_->setFocus(); });
    }
    connect(canvas_, &MixCanvas::curveModeChanged, this, [radios](int c) { radios[c]->setChecked(true); });
    tl->addStretch(1);
    trackDock->setWidget(tw);
    addDockWidget(Qt::RightDockWidgetArea, trackDock);
    connect(trimSpin_, &QDoubleSpinBox::editingFinished, this, [this] { if (!filling_ && canvas_->selectedLane() >= 0) setTrim(canvas_->selectedLane(), trimSpin_->value()); });

    stretchDock_ = new MixStretchDock;
    auto* sd = new QDockWidget(QStringLiteral("Stretch"), this);
    sd->setObjectName(QStringLiteral("mixStretchDock"));
    sd->setWidget(stretchDock_);
    addDockWidget(Qt::RightDockWidgetArea, sd);
    connect(stretchDock_, &MixStretchDock::defaultChosen, this, [this](const trakcrate::StretchChoice& c) {
        edit(QStringLiteral("Stretch engine"), [c](trakcrate::Mix& m) { m.stretch = c; }, QStringLiteral("stretch-default"));
    });
    connect(stretchDock_, &MixStretchDock::trackChosen, this, [this](int lane, bool off, const trakcrate::StretchChoice& c) {
        edit(QStringLiteral("Track stretch"), [lane, off, c](trakcrate::Mix& m) {
            if (lane < 0 || size_t(lane) >= m.tracks.size()) return;
            m.tracks[size_t(lane)].stretchOff = off;
            m.tracks[size_t(lane)].stretch = c;
        }, QStringLiteral("stretch-track%1").arg(lane));
    });
    connect(stretchDock_, &MixStretchDock::restretchRequested, this, [this] { stretchSignature_.clear(); scheduleStretch(); });
    connect(stretchDock_, &MixStretchDock::auditionRequested, this, &StudioMixWindow::audition);
    connect(canvas_, &MixCanvas::laneClicked, this, [this](int, double) { refreshDocks(); });
    connect(canvas_, &MixCanvas::removeTrackRequested, this, [this](int lane) { removeTrack(lane); });
    connect(canvas_, &MixCanvas::mixTypeChosen, this, [this](int pair, int type) { setMixType(pair, trakcrate::MixType(type)); });
    canvas_->setEditor([this](const QString& text, MixCanvas::ChangeFn change, const QString& key) {
        edit(text, [this, change](trakcrate::Mix& m) { change(m, trakcrate::effectiveTracks(m, facts_)); }, key);
    });

    // cuts and loops of the selected track: the edit panel of the track editor, in a mode where the window keeps the history
    panelUndo_ = new QUndoStack(this);
    regionPanel_ = new TrakcrateEditPanel(panelUndo_);
    regionPanel_->setExternalUndo(true);
    regionPanel_->setMixMode(true);
    auto* regionDock = new QDockWidget(QStringLiteral("Cuts and loops"), this);
    regionDock->setObjectName(QStringLiteral("mixRegionDock"));
    auto* rw = new QWidget;
    auto* rl = new QVBoxLayout(rw);
    rangeButton_ = new QPushButton(QStringLiteral("Select a range on the track (R)"));
    rangeButton_->setCheckable(true);
    rangeButton_->setToolTip(QStringLiteral("While on, a drag on the selected track marks a range (on its beats) to cut or loop instead of editing curves"));
    rl->addWidget(rangeButton_);
    rl->addWidget(regionPanel_, 1);
    regionDock->setWidget(rw);
    addDockWidget(Qt::RightDockWidgetArea, regionDock);
    resizeDocks({regionDock}, {330}, Qt::Horizontal);
    connect(rangeButton_, &QPushButton::toggled, this, [this](bool on) { canvas_->setRegionMode(on); });
    connect(regionPanel_, &TrakcrateEditPanel::rangeConsumed, canvas_, &MixCanvas::clearRange);
    connect(regionPanel_, &TrakcrateEditPanel::status, this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });
    connect(canvas_, &MixCanvas::rangeDragged, this, [this](int lane, double a, double b) {
        if (lane < 0 || size_t(lane) >= facts_.size()) return;
        const trakcrate::EffectiveTrack e = trakcrate::effectiveTracks(mix_, facts_)[size_t(lane)];
        // the panel works on the stretched track: the edited timeline is mapped back through the plan
        regionPanel_->setRange(trakcrate::outputToSourceMs(e.plan, a), trakcrate::outputToSourceMs(e.plan, b));
    });
    connect(regionPanel_, &TrakcrateEditPanel::regionsEdited, this, [this](const std::vector<trakcrate::EditRegion>&, const std::vector<trakcrate::EditRegion>& after, int selAfter, const QString& text, bool mergeable) {
        const int lane = canvas_->selectedLane();
        if (lane < 0 || size_t(lane) >= mix_.tracks.size()) return;
        regionSel_ = selAfter;
        regionSelLane_ = lane;
        bool clamped = false;
        edit(text, [this, lane, after, &clamped](trakcrate::Mix& m) {
            m.tracks[size_t(lane)].regions = after;
            // the periods and curve points are in beats of the edited track and are not moved by an edit: they are only kept inside it
            const auto eff = trakcrate::effectiveTracks(m, facts_);
            const double E = eff[size_t(lane)].editedBeats;
            trakcrate::MixTrack& t = m.tracks[size_t(lane)];
            for (trakcrate::Curve* c : {&t.volume, &t.bass, &t.high})
                for (trakcrate::CurvePoint& p : c->points)
                    if (p.beat > E) { p.beat = E; clamped = true; }
            auto fitPair = [&](int pair, bool outgoing) {
                if (pair < 0 || size_t(pair) + 1 >= m.tracks.size()) return;
                const trakcrate::MixPair before = m.tracks[size_t(pair)].next;
                if (outgoing) trakcrate::setPairOutStart(&m, pair, before.outStartBeat, eff);
                else trakcrate::setPairInStart(&m, pair, before.inStartBeat, eff);
                trakcrate::setPairLength(&m, pair, m.tracks[size_t(pair)].next.lenBeats, eff);
                const trakcrate::MixPair a = m.tracks[size_t(pair)].next;
                if (a.outStartBeat != before.outStartBeat || a.inStartBeat != before.inStartBeat || a.lenBeats != before.lenBeats) clamped = true;
            };
            fitPair(lane, true);
            fitPair(lane - 1, false);
        }, mergeable ? QStringLiteral("regions%1").arg(lane) : QString());
        if (clamped) statusBar()->showMessage(QStringLiteral("The edit shortened the track: periods and curve points were kept inside it"), 8000);
    });

    // the mixes of the library
    tree_ = new MixTreeDock(provider_);
    auto* treeDock = new QDockWidget(QStringLiteral("Mixes"), this);
    treeDock->setObjectName(QStringLiteral("mixTreeDock"));
    treeDock->setWidget(tree_);
    addDockWidget(Qt::LeftDockWidgetArea, treeDock);
    resizeDocks({treeDock}, {170}, Qt::Horizontal);
    connect(tree_, &MixTreeDock::openRequested, this, [this](const QString& p) { openMixFile(p); });
    connect(tree_, &MixTreeDock::renamed, this, [this](const QString& from, const QString& to) { if (currentFile_ == from) { currentFile_ = to; } });
    connect(tree_, &MixTreeDock::deleted, this, [this](const QString& p) { if (currentFile_ == p) currentFile_.clear(); });
    connect(tree_, &MixTreeDock::status, this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });
    connect(undo_, &QUndoStack::cleanChanged, this, [this] { updateTitle(); });
    connect(undo_, &QUndoStack::indexChanged, this, [this] { updateTitle(); });

    // default answers: dialogs
    prompts.saveDetails = [this](QString* name, QString* author, QString* image) {
        QDialog d(this);
        d.setWindowTitle(QStringLiteral("Save studio mix"));
        auto* form = new QFormLayout(&d);
        auto* n = new QLineEdit(*name);
        n->setToolTip(QStringLiteral("A folder may be given: Folder/Name"));
        auto* a = new QLineEdit(*author);
        auto* im = new QLineEdit(*image);
        auto* browse = new QPushButton(QStringLiteral("..."));
        auto* imRow = new QHBoxLayout;
        imRow->addWidget(im, 1);
        imRow->addWidget(browse);
        form->addRow(QStringLiteral("Name"), n);
        form->addRow(QStringLiteral("Author"), a);
        form->addRow(QStringLiteral("Cover image"), imRow);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(box);
        connect(box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        connect(browse, &QPushButton::clicked, &d, [&] {
            const QString f = QFileDialog::getOpenFileName(&d, QStringLiteral("Cover image"), QString(), QStringLiteral("Images (*.jpg *.jpeg *.png)"));
            if (!f.isEmpty()) im->setText(f);
        });
        if (d.exec() != QDialog::Accepted) return false;
        *name = n->text().trimmed();
        *author = a->text();
        *image = im->text();
        return true;
    };
    prompts.askUnsaved = [this](const QString& text) {
        const auto r = QMessageBox::question(this, windowTitle(), text, QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        return r == QMessageBox::Save ? 0 : r == QMessageBox::Discard ? 1 : 2;
    };
    prompts.chooseOpenFile = [this]() {
        const trakcrate::Library* lib = provider_ ? provider_() : nullptr;
        const QString start = lib ? QString::fromStdString(lib->mixesDir().string()) : QString();
        return QFileDialog::getOpenFileName(this, QStringLiteral("Open a studio mix"), start, QStringLiteral("Studio mixes (*.mix)"));
    };
    prompts.notify = [this](const QString& text) { QMessageBox::information(this, windowTitle(), text); };
    auto* rkey = new QAction(this);
    rkey->setShortcut(Qt::Key_R);
    connect(rkey, &QAction::triggered, rangeButton_, &QPushButton::toggle);
    addAction(rkey);

    qApp->installEventFilter(this);                      // Space plays the mix preview here, not the sequencer of the main window

    cache_ = std::make_unique<trakcrate::StretchCache>(trakcrate::defaultStretchCacheDir());
    queue_ = new StretchJobQueue(cache_.get(), this);
    connect(queue_, &StretchJobQueue::laneStretching, canvas_, &MixCanvas::setStretching);
    stretchTimer_ = new QTimer(this);
    stretchTimer_->setSingleShot(true);
    stretchTimer_->setInterval(600);
    connect(stretchTimer_, &QTimer::timeout, this, [this] {
        queue_->request(mix_, facts_);
    });

    // playlists for the combo, refreshed when the window is shown
    auto fillCombo = [this] {
        playlistCombo_->blockSignals(true);
        playlistCombo_->clear();
        playlistCombo_->addItem(QStringLiteral("New mix from playlist..."));
        if (const trakcrate::Library* lib = provider_ ? provider_() : nullptr)
            for (const std::string& n : lib->listPlaylists()) playlistCombo_->addItem(QString::fromStdString(n));
        playlistCombo_->blockSignals(false);
    };
    fillCombo();
    connect(playlistCombo_, QOverload<int>::of(&QComboBox::activated), this, [this, fillCombo](int i) {
        if (i > 0) newFromPlaylist(playlistCombo_->itemText(i));
        fillCombo();
    });

    previewer_ = new MixPreviewer(cache_.get(), this);
    connect(previewer_, &MixPreviewer::positionChanged, this, [this](double ms) {
        canvas_->setPlayPosition(ms);
        canvas_->ensureVisible(ms);
    });
    connect(previewer_, &MixPreviewer::status, this, [this](const QString& t) { statusBar()->showMessage(t, 6000); });
    connect(previewer_, &MixPreviewer::stateChanged, this, [this](int) { updateStatus(); });
    auto* homeKey = new QAction(this);
    homeKey->setShortcut(Qt::Key_Home);
    connect(homeKey, &QAction::triggered, this, [this] {
        const bool was = previewer_->isActive();
        previewer_->stop();
        canvas_->setPlayPosition(0);
        canvas_->ensureVisible(0);
        if (was) togglePreview();
    });
    addAction(homeKey);
    connect(canvas_, &MixCanvas::playPositionChanged, this, [this](double ms) {
        if (previewer_->isActive()) previewer_->start(ms);            // a click while playing jumps there
        updateStatus();
    });
    connect(canvas_, &MixCanvas::hovered, this, [this](int lane, double ms) {
        hoverLabel_->setText(lane >= 0 ? QStringLiteral("  track %1 at %2").arg(lane + 1).arg(MixCanvas::formatTime(ms)) : QString());
    });
    connect(canvas_, &MixCanvas::viewChanged, this, [this] {
        const double lo = std::min(MixCanvas::kMinSpanMs, std::max(MixCanvas::kMinSpanMs, canvas_->mixLengthMs())), hi = std::max(lo, std::max(MixCanvas::kMinSpanMs, canvas_->mixLengthMs()));
        QSignalBlocker b(zoom_);
        zoom_->setValue(hi > lo ? int(1000.0 * std::log(canvas_->spanMs() / lo) / std::log(hi / lo)) : 1000);
    });
    connect(zoom_, &QSlider::valueChanged, this, [this](int v) {
        const double hi = std::max(MixCanvas::kMinSpanMs, canvas_->mixLengthMs()), lo = std::min(MixCanvas::kMinSpanMs, hi);
        const double span = lo * std::pow(hi / lo, v / 1000.0);
        canvas_->zoomAt(canvas_->viewStartMs() + canvas_->spanMs() / 2, span / canvas_->spanMs());
    });

    if (config_) {
        const QByteArray g = QByteArray::fromBase64(config_->loadValue(kSection, QStringLiteral("geometry")).toLatin1());
        if (!g.isEmpty()) restoreGeometry(g);
        const int h = config_->loadValue(kSection, QStringLiteral("laneHeight"), QStringLiteral("100")).toInt();
        setLaneHeightChoice(h == 50 || h == 200 ? h : 100);
    } else {
        setLaneHeightChoice(100);
    }
}

StudioMixWindow::~StudioMixWindow()
{
    qApp->removeEventFilter(this);
    if (previewer_) previewer_->stop();
    if (queue_) queue_->cancel();                      // the worker uses the cache, which goes first
}

void StudioMixWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(QStringLiteral("&New from playlist..."), this, [this] { newFromPlaylist(); });
    auto later = [this](const QString& what) {
        statusBar()->showMessage(what + QStringLiteral(" comes in a later phase"), 4000);
    };
    file->addAction(QStringLiteral("&Open..."), this, [this] {
        const QString f = prompts.chooseOpenFile ? prompts.chooseOpenFile() : QString();
        if (!f.isEmpty()) openMixFile(f);
    });
    QAction* saveAct = file->addAction(QStringLiteral("&Save"), this, [this] { this->save(); });
    saveAct->setShortcut(QKeySequence::Save);
    file->addAction(QStringLiteral("Save &As..."), this, [this] { saveAs(); });
    file->addAction(QStringLiteral("&Render MP3..."), this, [this] { renderMp3(); });
    file->addSeparator();
    file->addAction(QStringLiteral("&Close"), this, &QWidget::close);

    QMenu* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    QAction* undo = undo_->createUndoAction(this, QStringLiteral("&Undo"));
    undo->setShortcut(QKeySequence::Undo);
    QAction* redo = undo_->createRedoAction(this, QStringLiteral("&Redo"));
    redo->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    edit->addAction(QStringLiteral("&EQ and master filters..."), this, [this] { filtersDialog()->show(); });

    QMenu* view = menuBar()->addMenu(QStringLiteral("&View"));
    QMenu* heights = view->addMenu(QStringLiteral("Track &height"));
    auto* group = new QActionGroup(this);
    const int px[3] = {50, 100, 200};
    const char* names[3] = {"Small (50 px)", "Medium (100 px)", "Large (200 px)"};
    for (int i = 0; i < 3; ++i) {
        heightActions_[i] = heights->addAction(QString::fromLatin1(names[i]));
        heightActions_[i]->setCheckable(true);
        group->addAction(heightActions_[i]);
        connect(heightActions_[i], &QAction::triggered, this, [this, i, px] { setLaneHeightChoice(px[i]); });
    }
    view->addSeparator();
    QAction* in = view->addAction(QStringLiteral("Zoom &in"), this, [this] { canvas_->zoomAt(canvas_->viewStartMs() + canvas_->spanMs() / 2, 0.7); });
    in->setShortcut(QKeySequence::ZoomIn);
    QAction* out = view->addAction(QStringLiteral("Zoom &out"), this, [this] { canvas_->zoomAt(canvas_->viewStartMs() + canvas_->spanMs() / 2, 1.0 / 0.7); });
    out->setShortcut(QKeySequence::ZoomOut);
    view->addAction(QStringLiteral("&Fit whole mix"), this, [this] { canvas_->zoomFit(); });
    QAction* fitPeriod = view->addAction(QStringLiteral("Fit mix &period"), this, [this] { canvas_->fitPeriod(); });
    fitPeriod->setShortcut(Qt::Key_F);
    fitPeriod->setToolTip(QStringLiteral("Centre the nearest mix period with 8 bars on each side"));
    QAction* prevPeriod = view->addAction(QStringLiteral("Pre&vious mix period"), this, [this] { canvas_->fitNextPeriod(-1); });
    prevPeriod->setShortcut(Qt::Key_BracketLeft);
    QAction* nextPeriod = view->addAction(QStringLiteral("&Next mix period"), this, [this] { canvas_->fitNextPeriod(1); });
    nextPeriod->setShortcut(Qt::Key_BracketRight);
}

void StudioMixWindow::setLaneHeightChoice(int px)
{
    canvas_->setLaneHeight(px);
    const int idx = px == 50 ? 0 : px == 200 ? 2 : 1;
    if (heightActions_[idx]) heightActions_[idx]->setChecked(true);
    if (config_) config_->saveValue(kSection, QStringLiteral("laneHeight"), QString::number(px));
}

void StudioMixWindow::updateStatus()
{
    posLabel_->setText(MixCanvas::formatTime(canvas_->playPosition()));
}

void StudioMixWindow::setMix(const trakcrate::Mix& mix, const std::vector<trakcrate::TrackFacts>& facts, const std::vector<MixLaneInfo>& lanes)
{
    previewer_->stop();
    mix_ = mix;
    facts_ = facts;
    undo_->clear();
    canvas_->setMix(&mix_, facts_, lanes);
    canvas_->setPlayPosition(0);
    updateTitle();
    updateStatus();
    refreshDocks();
    stretchSignature_.clear();
    scheduleStretch();
}

void StudioMixWindow::updateTitle()
{
    setWindowTitle(QStringLiteral("Studio mix - %1%2").arg(QString::fromStdString(mix_.name), undo_->isClean() ? QString() : QStringLiteral(" *")));
}

bool StudioMixWindow::isModified() const { return !undo_->isClean(); }

void StudioMixWindow::updateRegionPanel()
{
    const int lane = canvas_->selectedLane();
    trakcrate::BeatGrid g;
    double len = 0;
    std::vector<trakcrate::EditRegion> regions;
    int sel = -1;
    if (lane >= 0 && size_t(lane) < mix_.tracks.size()) {
        const trakcrate::EffectiveTrack e = trakcrate::effectiveTracks(mix_, facts_)[size_t(lane)];
        g = e.stretchedGrid;
        len = double(e.stretchedFrames) * 1000.0 / trakcrate::kEditRate;
        regions = mix_.tracks[size_t(lane)].regions;
        sel = regionSelLane_ == lane ? regionSel_ : -1;
    }
    auto same = [](const std::vector<trakcrate::EditRegion>& a, const std::vector<trakcrate::EditRegion>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].kind != b[i].kind || a[i].startBeat != b[i].startBeat || a[i].lenBeats != b[i].lenBeats || a[i].algo != b[i].algo || a[i].algoParam != b[i].algoParam ||
                a[i].loopTimes != b[i].loopTimes || a[i].earlyShift != b[i].earlyShift || a[i].earlyAfter != b[i].earlyAfter || a[i].earlyJoinAlgo != b[i].earlyJoinAlgo ||
                a[i].earlyJoinParam != b[i].earlyJoinParam) return false;
        return true;
    };
    if (lane == panelLane_ && g.bpm == panelGrid_.bpm && g.firstBeatUs == panelGrid_.firstBeatUs && len == panelLen_ && sel == panelSel_ && same(regions, panelRegions_)) return;
    if (lane != panelLane_) canvas_->clearRange();
    panelLane_ = lane;
    panelGrid_ = g;
    panelLen_ = len;
    panelRegions_ = regions;
    panelSel_ = sel;
    regionPanel_->setTrack(g, len);
    regionPanel_->setRegions(regions, sel);
}

void StudioMixWindow::refreshDocks()
{
    filling_ = true;
    bpmSpin_->setValue(mix_.masterBpm);
    const int lane = canvas_->selectedLane();
    const auto eff = trakcrate::effectiveTracks(mix_, facts_);
    if (lane >= 0 && size_t(lane) < mix_.tracks.size()) {
        const trakcrate::MixTrack& t = mix_.tracks[size_t(lane)];
        trackLabel_->setText(QStringLiteral("Track %1").arg(lane + 1));
        trimSpin_->setValue(t.trim);
        nudgeLabel_->setText(QStringLiteral("%1%2 ms").arg(t.gridNudgeMs >= 0 ? "+" : "").arg(t.gridNudgeMs, 0, 'f', 1));
        trimSpin_->setEnabled(true);
    } else {
        trackLabel_->setText(QStringLiteral("Click a track"));
        trimSpin_->setEnabled(false);
    }
    stretchDock_->setMix(mix_, eff, lane);
    filling_ = false;
    updateRegionPanel();
}

void StudioMixWindow::scheduleStretch()
{
    std::string sig = std::to_string(mix_.masterBpm) + "|" + trakcrate::choiceKey(mix_.stretch);
    for (const trakcrate::MixTrack& t : mix_.tracks) sig += "|" + std::string(t.stretchOff ? "off" : "") + trakcrate::choiceKey(t.stretch);
    if (sig == stretchSignature_) return;
    stretchSignature_ = sig;
    queue_->cancel();
    for (int i = 0; i < canvas_->laneCount(); ++i) canvas_->setStretching(i, false);
    stretchTimer_->start();
}

void StudioMixWindow::applyMix(const trakcrate::Mix& mix)
{
    if (previewer_) previewer_->stop();                  // the audio no longer matches the lanes
    const trakcrate::Mix old = mix_;
    mix_ = mix;
    realignTracks(old);
    canvas_->refresh();
    refreshDocks();
    scheduleStretch();
    emit mixChanged();
}

void StudioMixWindow::edit(const QString& text, std::function<void(trakcrate::Mix&)> change, const QString& mergeKey)
{
    trakcrate::Mix after = mix_;
    change(after);
    if (trakcrate::mixToXml(after, trakcrate::fs::path()) == trakcrate::mixToXml(mix_, trakcrate::fs::path())) return;       // nothing changed
    undo_->push(new MixSnapshotCommand(this, mix_, after, text, mergeKey));
}

void StudioMixWindow::setMasterBpm(double bpm)
{
    bpm = std::max(40.0, std::min(300.0, bpm));
    edit(QStringLiteral("Master tempo"), [bpm](trakcrate::Mix& m) { m.masterBpm = bpm; }, QStringLiteral("master-bpm"));
}

void StudioMixWindow::setTrim(int lane, double percent)
{
    percent = std::max(0.0, std::min(200.0, percent));
    edit(QStringLiteral("Track volume"), [lane, percent](trakcrate::Mix& m) { if (lane >= 0 && size_t(lane) < m.tracks.size()) m.tracks[size_t(lane)].trim = percent; },
         QStringLiteral("trim%1").arg(lane));
}

void StudioMixWindow::nudgeGrid(int lane, double deltaMs)
{
    edit(QStringLiteral("Grid nudge"), [lane, deltaMs](trakcrate::Mix& m) {
        if (lane < 0 || size_t(lane) >= m.tracks.size()) return;
        double& n = m.tracks[size_t(lane)].gridNudgeMs;
        n = deltaMs == 0 ? 0 : std::round((n + deltaMs) * 10) / 10;
    }, QStringLiteral("nudge%1").arg(lane));
}

// Facts and lanes follow the tracks of the mix by file, so a removed track can be brought back by undo.
void StudioMixWindow::realignTracks(const trakcrate::Mix& old)
{
    bool same = old.tracks.size() == mix_.tracks.size();
    for (size_t i = 0; same && i < old.tracks.size(); ++i) same = old.tracks[i].file == mix_.tracks[i].file;
    if (same) return;
    const std::vector<MixLaneInfo> infos = canvas_->laneInfos();
    std::vector<bool> used(old.tracks.size(), false);
    std::vector<trakcrate::TrackFacts> nf;
    std::vector<MixLaneInfo> ni;
    for (const trakcrate::MixTrack& t : mix_.tracks) {
        const std::string key = t.file.string();
        bool found = false;
        for (size_t j = 0; j < old.tracks.size() && !found; ++j) {
            if (used[j] || old.tracks[j].file != t.file || j >= facts_.size()) continue;
            used[j] = found = true;
            nf.push_back(facts_[j]);
            ni.push_back(j < infos.size() ? infos[j] : MixLaneInfo());
        }
        if (found) continue;
        const auto it = parked_.find(key);
        nf.push_back(it != parked_.end() ? it->second.facts : trakcrate::TrackFacts());
        ni.push_back(it != parked_.end() ? it->second.info : MixLaneInfo());
    }
    for (size_t j = 0; j < old.tracks.size(); ++j)
        if (!used[j] && j < facts_.size()) parked_[old.tracks[j].file.string()] = Parked{facts_[j], j < infos.size() ? infos[j] : MixLaneInfo()};
    facts_ = nf;
    canvas_->setTracks(facts_, ni);
    regionSel_ = regionSelLane_ = -1;
    panelLane_ = -2;
}

void StudioMixWindow::removeTrack(int lane)
{
    if (lane < 0 || size_t(lane) >= mix_.tracks.size()) return;
    if (mix_.tracks.size() < 2) {
        statusBar()->showMessage(QStringLiteral("A mix needs at least one track"), 4000);
        return;
    }
    const std::vector<trakcrate::TrackFacts> facts = facts_;
    edit(QStringLiteral("Remove track %1 from the mix").arg(lane + 1), [lane, facts](trakcrate::Mix& m) { trakcrate::removeTrack(&m, lane, facts); });
}

void StudioMixWindow::setMixType(int pair, trakcrate::MixType type)
{
    edit(QStringLiteral("Mix type %1").arg(QString::fromLatin1(trakcrate::mixTypeName(type))), [this, pair, type](trakcrate::Mix& m) {
        trakcrate::applyMixType(&m, pair, type, trakcrate::effectiveTracks(m, facts_));
    });
}

MixFiltersDialog* StudioMixWindow::filtersDialog()
{
    if (!filters_) filters_ = new MixFiltersDialog(this);
    return filters_;
}

void StudioMixWindow::audition()
{
    const int lane = canvas_->selectedLane();
    if (lane < 0 || size_t(lane) >= mix_.tracks.size()) return;
    const trakcrate::MixTrack& t = mix_.tracks[size_t(lane)];
    const auto eff = trakcrate::effectiveTracks(mix_, facts_);
    // the excerpt: the mix period of the track (as outgoing, else as incoming) plus 4 bars either side, at most 40 s, from the ORIGINAL audio
    double a = 0, b = 16;
    const trakcrate::BeatGrid& g = facts_[size_t(lane)].grid;
    if (lane + 1 < int(mix_.tracks.size())) { a = t.next.outStartBeat; b = a + t.next.lenBeats; }
    else if (lane > 0) { a = mix_.tracks[size_t(lane) - 1].next.inStartBeat; b = a + mix_.tracks[size_t(lane) - 1].next.lenBeats; }
    // edited beats of the stretched track -> beats of the original (same count: the stretch keeps beats)
    a = std::max(0.0, a - 16);
    b = b + 16;
    const double maxBeats = 40.0 * g.bpm / 60.0;
    if (b - a > maxBeats) b = a + maxBeats;
    trakcrate::DecodedPcm full;
    std::string err;
    if (!trakcrate::decodeAudio(t.file, &full, &err)) {
        statusBar()->showMessage(QString::fromStdString(err), 6000);
        return;
    }
    const long long f0 = std::max(0LL, trakcrate::beatFrame(g, a)), f1 = std::min((long long)full.frames(), trakcrate::beatFrame(g, b));
    if (f1 <= f0) return;
    trakcrate::DecodedPcm ex;
    ex.stereo.assign(full.stereo.begin() + f0 * 2, full.stereo.begin() + f1 * 2);
    MixAuditionDialog dlg(ex, eff[size_t(lane)].ratio, this);
    dlg.exec();
}
void StudioMixWindow::showPlaylistMix(const QString& playlist, const std::vector<trakcrate::MixTrackInput>& tracks)
{
    trakcrate::Mix mix = trakcrate::defaultMix(playlist.toStdString(), tracks);
    mix.playlist = playlist.toStdString();
    currentFile_.clear();
    loadMixAndFacts(mix, false);
}

// Shows a mix: facts and lanes come from the track folders; a track that cannot be read stays in the mix as a red "missing" lane.
void StudioMixWindow::loadMixAndFacts(const trakcrate::Mix& mixIn, bool)
{
    trakcrate::Mix mix = mixIn;
    std::vector<trakcrate::TrackFacts> facts;
    std::vector<MixLaneInfo> lanes;
    for (trakcrate::MixTrack& t : mix.tracks) {
        trakcrate::TrackFacts f;
        MixLaneInfo li;
        std::string err;
        const trakcrate::fs::path dir = t.file.parent_path();
        if (t.missing || !trakcrate::readTrackFacts(t.file, &f, &err)) {
            t.missing = true;
        } else {
            trakcrate::Track tr;
            trakcrate::loadTrack(dir, &tr);
            li.title = trackTitle(tr, dir);
            li.color = trakcrateTrackAnalysis(QString::fromStdString(dir.string())).color;
        }
        if (li.title.isEmpty()) li.title = QString::fromStdString(dir.filename().string());
        facts.push_back(f);
        lanes.push_back(li);
    }
    regionSel_ = regionSelLane_ = -1;
    panelLane_ = -2;
    setMix(mix, facts, lanes);
}

bool StudioMixWindow::maybeSave()
{
    if (undo_->isClean()) return true;
    const int r = prompts.askUnsaved ? prompts.askUnsaved(QStringLiteral("Save the changes to \"%1\"?").arg(QString::fromStdString(mix_.name))) : 1;
    if (r == 0) return save();
    return r == 1;
}

bool StudioMixWindow::writeFile(const QString& path)
{
    const trakcrate::Library* lib = provider_ ? provider_() : nullptr;
    std::string err;
    const trakcrate::fs::path root = lib ? lib->root() : trakcrate::fs::path();
    if (!trakcrate::atomicWrite(trakcrate::fs::path(path.toStdString()), trakcrate::mixToXml(mix_, root), &err)) {
        if (prompts.notify) prompts.notify(QStringLiteral("Cannot save: ") + QString::fromStdString(err));
        return false;
    }
    undo_->setClean();
    updateTitle();
    tree_->refresh();
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(path), 5000);
    return true;
}

bool StudioMixWindow::save()
{
    if (currentFile_.isEmpty()) return saveAs();
    return writeFile(currentFile_);
}

bool StudioMixWindow::saveAs()
{
    QString name = currentFile_.isEmpty() ? QString::fromStdString(mix_.name.empty() ? mix_.playlist : mix_.name) : QString();
    if (!currentFile_.isEmpty()) {
        const trakcrate::Library* lib = provider_ ? provider_() : nullptr;
        if (lib) {
            const auto rel = trakcrate::fs::path(currentFile_.toStdString()).lexically_relative(lib->mixesDir());
            if (!rel.empty() && *rel.begin() != "..") name = QString::fromStdString(trakcrate::fs::path(rel).replace_extension("").generic_string());
        }
    }
    QString author = QString::fromStdString(mix_.author), image = QString::fromStdString(mix_.image);
    if (!prompts.saveDetails || !prompts.saveDetails(&name, &author, &image)) return false;
    return saveToName(name, author, image);
}

bool StudioMixWindow::saveToName(const QString& relName, const QString& author, const QString& image)
{
    const trakcrate::Library* lib = provider_ ? provider_() : nullptr;
    if (!lib || !lib->isOpen()) {
        if (prompts.notify) prompts.notify(QStringLiteral("Open a trakcrate library first."));
        return false;
    }
    if (!trakcrate::isSafeMixName(relName.toStdString())) {
        if (prompts.notify) prompts.notify(QStringLiteral("That name cannot be used (letters, digits, spaces and one level of folders: Folder/Name)."));
        return false;
    }
    const std::string leaf = trakcrate::fs::path(relName.toStdString()).filename().string();
    const std::string a = author.toStdString(), im = image.toStdString();
    edit(QStringLiteral("Mix details"), [leaf, a, im](trakcrate::Mix& m) { m.name = leaf; m.author = a; m.image = im; });
    std::string err;
    if (!trakcrate::saveMix(*lib, relName.toStdString(), mix_, &err)) {
        if (prompts.notify) prompts.notify(QStringLiteral("Cannot save: ") + QString::fromStdString(err));
        return false;
    }
    currentFile_ = QString::fromStdString((lib->mixesDir() / (relName.toStdString() + ".mix")).string());
    undo_->setClean();
    updateTitle();
    tree_->refresh();
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(currentFile_), 5000);
    return true;
}

bool StudioMixWindow::openMixFile(const QString& path)
{
    const trakcrate::Library* lib = provider_ ? provider_() : nullptr;
    if (!lib || !lib->isOpen()) {
        if (prompts.notify) prompts.notify(QStringLiteral("Open a trakcrate library first."));
        return false;
    }
    if (!maybeSave()) return false;
    trakcrate::Mix mix;
    std::vector<std::string> warnings;
    std::string err;
    if (!trakcrate::loadMix(trakcrate::fs::path(path.toStdString()), lib->root(), &mix, &warnings, &err)) {
        if (prompts.notify) prompts.notify(QStringLiteral("Cannot open the mix: ") + QString::fromStdString(err));
        return false;
    }
    loadMixAndFacts(mix, true);
    currentFile_ = path;
    undo_->setClean();
    updateTitle();
    if (!warnings.empty() && prompts.notify) {
        QString text = QStringLiteral("Some tracks of this mix are missing (they are shown in red and the mix cannot be rendered until they are fixed or removed):\n");
        for (const std::string& w : warnings) text += QStringLiteral("\n") + QString::fromStdString(w);
        prompts.notify(text);
    }
    return true;
}

void StudioMixWindow::newFromPlaylist(const QString& preselect)
{
    const trakcrate::Library* lib = provider_ ? provider_() : nullptr;
    if (!lib || !lib->isOpen()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Open a trakcrate library first."));
        return;
    }
    if (!maybeSave()) return;
    NewMixDialog dlg(lib, this);
    if (!preselect.isEmpty()) dlg.selectPlaylist(preselect);
    if (dlg.exec() != QDialog::Accepted) return;
    const auto tracks = dlg.tracks();
    if (tracks.empty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Studio mixes need analysed tracks."));
        return;
    }
    showPlaylistMix(dlg.playlist(), tracks);
}

bool StudioMixWindow::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type t = event->type();
    if ((t != QEvent::ShortcutOverride && t != QEvent::KeyPress) || !watched->isWidgetType() || !isActiveWindow()) return QMainWindow::eventFilter(watched, event);
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() != Qt::Key_Space || (ke->modifiers() & ~Qt::KeypadModifier) || ke->isAutoRepeat()) return QMainWindow::eventFilter(watched, event);
    QWidget* f = QApplication::focusWidget();
    if (f && (qobject_cast<QLineEdit*>(f) || qobject_cast<QAbstractSpinBox*>(f) || qobject_cast<QTextEdit*>(f))) return QMainWindow::eventFilter(watched, event);   // typing
    if (auto* cb = qobject_cast<QComboBox*>(f)) if (cb->isEditable()) return QMainWindow::eventFilter(watched, event);
    if (t == QEvent::ShortcutOverride) {
        event->accept();                                 // keeps the application shortcut (the main window's play / pause) from taking the key
        return false;
    }
    togglePreview();
    return true;
}

void StudioMixWindow::renderMp3()
{
    const trakcrate::Library* lib = provider_ ? provider_() : nullptr;
    if (!lib || mix_.tracks.empty()) {
        statusBar()->showMessage(QStringLiteral("There is no mix to render"), 4000);
        return;
    }
    previewer_->stop();
    MixRenderDialog dlg(mix_, lib->root(), config_, cache_.get(), this);
    if (renderHook) renderHook(&dlg);
    else dlg.exec();
    const trakcrate::Mix r = dlg.resultMix();
    if (r.name != mix_.name || r.author != mix_.author || r.image != mix_.image)
        edit(QStringLiteral("Mix details"), [r](trakcrate::Mix& m) { m.name = r.name; m.author = r.author; m.image = r.image; });
}

void StudioMixWindow::togglePreview()
{
    if (previewer_->isActive()) {
        previewer_->stop();
        return;
    }
    previewer_->setMix(mix_, facts_);
    double from = canvas_->playPosition();
    if (from >= canvas_->mixLengthMs() - 100) from = 0;
    previewer_->start(from);
}

void StudioMixWindow::closeEvent(QCloseEvent* e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    if (config_) config_->saveValue(kSection, QStringLiteral("geometry"), QString::fromLatin1(saveGeometry().toBase64()));
    e->accept();
}

} // namespace vdj
