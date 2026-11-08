#include "trakcrate_track_editor.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QTextEdit>
#include <QDockWidget>
#include <QGuiApplication>
#include <QScreen>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QMessageBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QUndoCommand>
#include <QUndoStack>
#include <QScrollBar>
#include <algorithm>
#include <cmath>
#include <QVBoxLayout>

#include "artwork_view.h"
#include "star_rating_widget.h"
#include "trakcrate/cuemark_edit.h"
#include "trakcrate/grid_edit.h"
#include "trakcrate/track.h"
#include "trakcrate_analysis.h"
#include "trakcrate_edit_panel.h"
#include "trakcrate_edit_loop_dialog.h"
#include "trakcrate_edit_previewer.h"
#include "trakcrate_edit_render_dialog.h"
#include "trakcrate_scripts.h"
#include "waveform_widget.h"

namespace vdj {

// Sets one meta.txt key; undo puts the old value back. Redo/undo go through the editor so the file is always written.
class MetaEditCommand : public QUndoCommand {
public:
    MetaEditCommand(TrakcrateTrackEditor* ed, const QString& dir, const QString& key, const QString& oldValue, const QString& newValue,
                    const QString& text)
        : QUndoCommand(text), ed_(ed), dir_(dir), key_(key), old_(oldValue), new_(newValue) {}
    void redo() override { apply(new_); }
    void undo() override { apply(old_); }

private:
    void apply(const QString& value)
    {
        ed_->writeMeta(dir_, key_, value);                // always the track the edit was made on, even if another is loaded now
        if (ed_->dir_ == dir_) ed_->reload();
    }
    TrakcrateTrackEditor* ed_;
    QString dir_, key_, old_, new_;
};

// Replaces the whole cuemarks file content; undo puts the previous content back.
class CuemarkEditCommand : public QUndoCommand {
public:
    CuemarkEditCommand(TrakcrateTrackEditor* ed, const QString& dir, const trakcrate::CuemarkFile& before, const trakcrate::CuemarkFile& after, const QString& text)
        : QUndoCommand(text), ed_(ed), dir_(dir), before_(before), after_(after) {}
    void redo() override { apply(after_); }
    void undo() override { apply(before_); }

private:
    void apply(const trakcrate::CuemarkFile& f)
    {
        ed_->applyMarks(dir_, f);
        if (ed_->dir_ == dir_) ed_->showMarks();
    }
    TrakcrateTrackEditor* ed_;
    QString dir_;
    trakcrate::CuemarkFile before_, after_;
};

// Sets the beat grid (and the origin it is marked with); consecutive nudges of one track merge.
class GridEditCommand : public QUndoCommand {
public:
    GridEditCommand(TrakcrateTrackEditor* ed, const QString& dir, const trakcrate::BeatGrid& before, const std::string& beforeOrigin,
                    const trakcrate::BeatGrid& after, const QString& text, bool merge)
        : QUndoCommand(text), ed_(ed), dir_(dir), before_(before), beforeOrigin_(beforeOrigin), after_(after), merge_(merge) {}
    int id() const override { return merge_ ? 0x6772 : -1; }
    bool mergeWith(const QUndoCommand* other) override
    {
        const auto* o = dynamic_cast<const GridEditCommand*>(other);
        if (!merge_ || !o || !o->merge_ || o->dir_ != dir_) return false;
        after_ = o->after_;
        return true;
    }
    void redo() override { apply(after_, "edited"); }
    void undo() override { apply(before_, beforeOrigin_); }

private:
    void apply(const trakcrate::BeatGrid& g, const std::string& origin)
    {
        ed_->applyGrid(dir_, g, origin);
        if (ed_->dir_ == dir_) { ed_->refreshWave(); ed_->showGrid(); ed_->showMarks(); }
    }
    TrakcrateTrackEditor* ed_;
    QString dir_;
    trakcrate::BeatGrid before_;
    std::string beforeOrigin_;
    trakcrate::BeatGrid after_;
    bool merge_;
};

TrakcrateTrackEditor::TrakcrateTrackEditor(QWidget* parent) : QWidget(parent)
{
    undo_ = new QUndoStack(this);
    title_ = new QLabel(QStringLiteral("No trakcrate track loaded"));
    title_->setWordWrap(true);
    QFont f = title_->font();
    f.setBold(true);
    title_->setFont(f);
    art_ = new ArtworkView;
    facts_ = new QLabel;
    facts_->setWordWrap(true);
    facts_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    stars_ = new StarRatingWidget;
    comment_ = new QLineEdit;
    comment_->setPlaceholderText(QStringLiteral("shown on the decks when the track is selected"));
    wave_ = new WaveformWidget;
    wave_->setInteractive(true);
    wave_->setMinimumHeight(90);
    editMarks_ = new QPushButton(QStringLiteral("Edit cuemarks"));
    editMarks_->setCheckable(true);
    editMarks_->setToolTip(QStringLiteral("Double click adds a cuemark (Alt: free position), drag moves it, right click for the menu (loops: End loop here, Double / Half loop, Remove loop; they need a beat grid), M adds at the play position, Delete removes"));
    snapBox_ = new QComboBox;
    snapBox_->addItem(QStringLiteral("Snap to beat"), int(WaveformWidget::Snap::Beat));
    snapBox_->addItem(QStringLiteral("Snap to bar"), int(WaveformWidget::Snap::Bar));
    snapBox_->addItem(QStringLiteral("Free"), int(WaveformWidget::Snap::None));
    editRegions_ = new QPushButton(QStringLiteral("Edit track"));
    editRegions_->setCheckable(true);
    editRegions_->setToolTip(QStringLiteral("Make a DJ edit: drag on the waveform to select bars (they snap to the beat grid), then Cut or Loop them; nothing is written until you render"));
    editPanel_ = new TrakcrateEditPanel(undo_);
    editPanel_->setVisible(false);
    gridEdit_ = new QPushButton(QStringLiteral("Edit grid"));
    gridEdit_->setCheckable(true);
    gridEdit_->setToolTip(QStringLiteral("Drag the waveform to move the beat grid; Left/Right nudge 1 ms (Shift 10 ms, Ctrl 0.1 ms); B or the right click menu sets beat 1 at the pointer"));
    gridReadout_ = new QLabel;
    markList_ = new QListWidget;
    markList_->setMaximumHeight(90);
    markList_->setToolTip(QStringLiteral("Double click to name a cuemark"));
    undoButton_ = new QPushButton(QStringLiteral("Undo"));
    redoButton_ = new QPushButton(QStringLiteral("Redo"));

    auto* top = new QHBoxLayout;
    top->addWidget(art_);
    auto* right = new QVBoxLayout;
    right->addWidget(title_);
    right->addWidget(facts_, 1);
    top->addLayout(right, 1);
    auto* ratingRow = new QHBoxLayout;
    ratingRow_ = ratingRow;
    ratingRow->addWidget(new QLabel(QStringLiteral("Rating")));
    ratingRow->addWidget(stars_);
    ratingRow->addStretch(1);
    ratingRow->addWidget(undoButton_);
    ratingRow->addWidget(redoButton_);
    auto* col = new QVBoxLayout(this);
    col->addLayout(top);
    col->addLayout(ratingRow);
    {
        auto* commentRow = new QHBoxLayout;
        commentRow->addWidget(new QLabel(QStringLiteral("Comment")));
        commentRow->addWidget(comment_, 1);
        col->addLayout(commentRow);
    }
    col->addWidget(wave_, 1);
    waveScroll_ = new QScrollBar(Qt::Horizontal);
    waveScroll_->setToolTip(QStringLiteral("Scroll the zoomed waveform; drag the handle to the far left for the start of the track"));
    col->addWidget(waveScroll_);
    connect(wave_, &WaveformWidget::viewChanged, this, &TrakcrateTrackEditor::syncWaveScroll);
    connect(waveScroll_, &QScrollBar::valueChanged, this, [this](int v) {
        if (syncingScroll_) return;
        const double len = wave_->analysis().lengthMs();
        wave_->setView(v, wave_->viewSpan() > 0 ? wave_->viewSpan() : len);
    });
    auto* markRow = new QHBoxLayout;
    markRow->addWidget(editMarks_);
    markRow->addWidget(snapBox_);
    markRow->addWidget(editRegions_);
    markRow->addStretch(1);
    col->addLayout(markRow);
    col->addWidget(markList_);
    auto* gridRow = new QHBoxLayout;
    gridRow->addWidget(gridEdit_);
    struct Step { const char* text; double ms; };
    for (const Step& st : {Step{"-10", -10.0}, Step{"-1", -1.0}, Step{"-0.1", -0.1}, Step{"+0.1", 0.1}, Step{"+1", 1.0}, Step{"+10", 10.0}}) {
        auto* b = new QPushButton(QLatin1String(st.text));
        b->setMaximumWidth(48);
        b->setToolTip(QStringLiteral("Move the beat grid by %1 ms").arg(QLatin1String(st.text)));
        const double ms = st.ms;
        connect(b, &QPushButton::clicked, this, [this, ms] { nudgeGrid(ms); });
        gridRow->addWidget(b);
    }
    auto* revert = new QPushButton(QStringLiteral("Revert"));
    revert->setToolTip(QStringLiteral("Put back the grid the track had when it was loaded here"));
    connect(revert, &QPushButton::clicked, this, [this] { revertGrid(); });
    gridRow->addWidget(revert);
    gridRow->addWidget(gridReadout_);
    gridRow->addStretch(1);
    col->addLayout(gridRow);
    col->addWidget(editPanel_);
    connect(editRegions_, &QPushButton::toggled, this, [this](bool on) {
        if (on && editPanel_->isEnabled() == false) {
            emit status(QStringLiteral("A beat grid is needed to make an edit"));
            editRegions_->setChecked(false);
            return;
        }
        if (on) { editMarks_->setChecked(false); gridEdit_->setChecked(false); }
        wave_->setRegionEditing(on);
        editPanel_->setVisible(on);
        if (!on) previewer_->stop();
        popOut(on);
    });
    connect(wave_, &WaveformWidget::editRangeSelected, editPanel_, &TrakcrateEditPanel::setRange);
    connect(wave_, &WaveformWidget::editRegionClicked, editPanel_, &TrakcrateEditPanel::selectRegion);
    connect(editPanel_, &TrakcrateEditPanel::rangeConsumed, wave_, &WaveformWidget::clearEditRange);
    previewer_ = new EditPreviewer(&editCache_, this);
    qApp->installEventFilter(this);
    connect(editPanel_, &TrakcrateEditPanel::previewRequested, this, [this](double a, double b) { previewEdit(a, b); });
    connect(wave_, &WaveformWidget::previewToggleRequested, this, [this] { previewEdit(editPanel_->previewStart(), -1); });
    connect(previewer_, &EditPreviewer::status, this, &TrakcrateTrackEditor::status);
    connect(previewer_, &EditPreviewer::stateChanged, this, [this](bool playing) { if (!playing) wave_->setPlayPosition(-1); });
    connect(previewer_, &EditPreviewer::positionChanged, this, [this](double ms) {
        wave_->setPlayPosition(ms);                                    // the time line, on the original's waveform
        const double span = wave_->viewSpan();
        if (span > 0 && (ms < wave_->viewStart() || ms > wave_->viewStart() + span)) wave_->setView(ms - span * 0.1, span);
    });
    connect(wave_, &WaveformWidget::previewPointSet, editPanel_, &TrakcrateEditPanel::setPreviewStart);
    connect(editPanel_, &TrakcrateEditPanel::loopExportRequested, this, [this](int a, int n) { openLoopExport(a, n); });
    connect(editPanel_, &TrakcrateEditPanel::renderRequested, this, [this] { openEditRender(); });
    connect(editPanel_, &TrakcrateEditPanel::status, this, &TrakcrateTrackEditor::status);
    connect(editPanel_, &TrakcrateEditPanel::regionsChanged, this, [this] {
        wave_->setEditRegions(editPanel_->waveRegions());
        wave_->setEditRegionSelected(editPanel_->selected());
        wave_->setPreviewPoint(editPanel_->previewStart());
        if (previewer_->isPlaying() || previewer_->isBusy()) {
            if (trakcrate::requestKey(editRequest()) != lastEditKey_) previewer_->stop();      // the edit changed under the preview
        }
    });
    connect(gridEdit_, &QPushButton::toggled, this, [this](bool on) {
        if (on) { editMarks_->setChecked(false); editRegions_->setChecked(false); }
        wave_->setGridEditing(on);
    });
    connect(editMarks_, &QPushButton::toggled, this, [this](bool on) { if (on) { gridEdit_->setChecked(false); editRegions_->setChecked(false); } });
    connect(wave_, &WaveformWidget::gridNudged, this, [this](double ms) { nudgeGrid(ms); });
    connect(wave_, &WaveformWidget::gridBeatOneRequested, this, [this](double ms) { setBeatOneAt(ms); });

    connect(art_, &ArtworkView::artworkChanged, this, [this](const QString& d) { showFacts(); emit trackChanged(d); });
    connect(art_, &ArtworkView::status, this, &TrakcrateTrackEditor::status);
    connect(editMarks_, &QPushButton::toggled, wave_, &WaveformWidget::setCuemarkEditing);
    connect(snapBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { wave_->setSnap(WaveformWidget::Snap(snapBox_->currentData().toInt())); });
    connect(wave_, &WaveformWidget::cuemarkAdded, this, [this](double ms) { addCuemark(ms); });
    connect(wave_, &WaveformWidget::cuemarkMoved, this, [this](int i, double ms) { moveCuemark(i, ms); });
    connect(wave_, &WaveformWidget::cuemarkRemoved, this, [this](int i) { removeCuemark(i); });
    connect(wave_, &WaveformWidget::cuemarkHotChanged, this, [this](int i, int hot) { setCuemarkHot(i, char(hot)); });
    connect(wave_, &WaveformWidget::cuemarkLoopEnded, this, [this](int i, double ms) { setLoopEnd(i, ms); });
    connect(wave_, &WaveformWidget::cuemarkLoopScaled, this, [this](int i, bool d) { scaleLoop(i, d); });
    connect(wave_, &WaveformWidget::cuemarkLoopRemoved, this, [this](int i) { removeLoop(i); });
    wave_->setLoopMenuProvider([this](double ms, int hit) {
        WaveLoopMenu m;
        trakcrate::Track t;
        if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return m;
        const trakcrate::LoopMenuState s = trakcrate::loopMenuState(t.cuemarks, trakcrate::beatGridFrom(t.grid), ms, hit, double(t.meta.getInt("duration_ms", 0)));
        m.endLoopFor = s.endLoopFor; m.insideLoop = s.insideLoop; m.canDouble = s.canDouble; m.canHalf = s.canHalf;
        m.removeLoopFor = s.removeLoopFor; m.needsGrid = s.needsGrid;
        return m;
    });
    connect(wave_, &WaveformWidget::cuemarkSelected, this, [this](int i) { markList_->setCurrentRow(i); });
    connect(markList_, &QListWidget::currentRowChanged, wave_, &WaveformWidget::setSelectedCuemark);
    connect(markList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        const int i = markList_->currentRow();
        trakcrate::Track t;
        if (i < 0 || !trakcrate::loadTrack(dir_.toStdString(), &t) || size_t(i) >= t.cuemarks.marks.size()) return;
        bool ok = false;
        const QString text = QInputDialog::getText(this, QStringLiteral("Cuemark"), QStringLiteral("Name:"), QLineEdit::Normal,
                                                   QString::fromStdString(t.cuemarks.marks[size_t(i)].label), &ok);
        if (ok) setCuemarkLabel(i, text);
    });

    connect(stars_, &StarRatingWidget::ratingChanged, this, [this](int n) { setRating(n); });
    connect(comment_, &QLineEdit::editingFinished, this, &TrakcrateTrackEditor::commentCommitted);
    // Ctrl+Z / Ctrl+Shift+Z (Ctrl+Y) anywhere in the editor; a text field that has focus keeps its own undo
    for (const QKeySequence& k : {QKeySequence(Qt::CTRL | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)}) {
        auto* sc = new QShortcut(k, this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        const bool undo = k == QKeySequence(Qt::CTRL | Qt::Key_Z);
        connect(sc, &QShortcut::activated, undo_, [this, undo]() { if (undo) undo_->undo(); else undo_->redo(); });
    }
    undoButton_->setToolTip(QStringLiteral("Undo the last edit of this track (Ctrl+Z)"));
    redoButton_->setToolTip(QStringLiteral("Redo (Ctrl+Shift+Z or Ctrl+Y)"));
    connect(undoButton_, &QPushButton::clicked, undo_, &QUndoStack::undo);
    connect(redoButton_, &QPushButton::clicked, undo_, &QUndoStack::redo);
    connect(undo_, &QUndoStack::canUndoChanged, undoButton_, &QPushButton::setEnabled);
    connect(undo_, &QUndoStack::canRedoChanged, redoButton_, &QPushButton::setEnabled);
    setTrack(QString());
}

void TrakcrateTrackEditor::setScripts(ScriptManager* scripts)
{
    scripts_ = scripts;
    auto* button = new QToolButton;
    button->setText(QStringLiteral("Scripts"));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolTip(QStringLiteral("Scripts for this track (scripts/track-script-*). They only run when you start them."));
    auto* menu = new QMenu(button);
    button->setMenu(menu);
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        populateScriptsMenu(menu, scripts_, trakcrate::ScriptKind::Track, this, [this] { return dir_; });
    });
    connect(scripts_, &ScriptManager::ran, this, [this] {
        if (dir_.isEmpty()) return;
        const QString d = dir_;
        setTrack(d);                                          // the files may have changed: read them again
        emit trackChanged(d);
        emit gridChanged(d);
    });
    // next to Undo / Redo in the rating row
    ratingRow_->insertWidget(ratingRow_->indexOf(undoButton_), button);
}

void TrakcrateTrackEditor::setConfig(ConfigManager* config)
{
    config_ = config;
    art_->setConfig(config);
}

trakcrate::EditRequest TrakcrateTrackEditor::editRequest() const
{
    trakcrate::EditRequest r;
    r.trackDir = dir_.toStdString();
    trakcrate::Track t;
    if (!dir_.isEmpty() && trakcrate::loadTrack(r.trackDir, &t)) r.grid = trakcrate::beatGridFrom(t.grid);
    r.regions = editPanel_->regions();
    return r;
}

bool TrakcrateTrackEditor::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type t = event->type();
    if ((t != QEvent::ShortcutOverride && t != QEvent::KeyPress) || !editRegions_->isChecked()) return QWidget::eventFilter(watched, event);
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() != Qt::Key_Space || (ke->modifiers() & ~Qt::KeypadModifier) || ke->isAutoRepeat()) return QWidget::eventFilter(watched, event);
    QWidget* f = QApplication::focusWidget();
    if (!f || (f != this && !isAncestorOf(f))) return QWidget::eventFilter(watched, event);
    if (qobject_cast<QLineEdit*>(f) || qobject_cast<QAbstractSpinBox*>(f) || qobject_cast<QTextEdit*>(f)) return QWidget::eventFilter(watched, event);   // typing a space
    if (auto* cb = qobject_cast<QComboBox*>(f)) if (cb->isEditable()) return QWidget::eventFilter(watched, event);
    if (t == QEvent::ShortcutOverride) {
        event->accept();                                              // keeps the application shortcut (the sequencer's play / pause) from taking the key
        return false;
    }
    previewEdit(editPanel_->previewStart(), -1);
    return true;
}

void TrakcrateTrackEditor::previewEdit(double startMs, double stopMs)
{
    if (dir_.isEmpty()) return;
    const trakcrate::EditRequest r = editRequest();
    lastEditKey_ = trakcrate::requestKey(r);
    previewer_->toggle(r, startMs, stopMs);
}

void TrakcrateTrackEditor::openLoopExport(int startBeat, int lenBeats)
{
    if (dir_.isEmpty()) return;
    const trakcrate::EditRequest r = editRequest();
    if (r.grid.empty()) return;
    EditLoopDialog dlg(r.trackDir, r.grid, startBeat, lenBeats, &editCache_, config_, this);
    connect(&dlg, &EditLoopDialog::exported, this, [this](const QString& file) { emit status(QStringLiteral("Saved loop %1").arg(file)); });
    dlg.exec();
}

// Editing a track needs room: the dock the editor sits in pops out as a window across the whole width of the screen, and goes back when the mode ends.
void TrakcrateTrackEditor::popOut(bool on)
{
    QDockWidget* dock = nullptr;
    for (QWidget* w = parentWidget(); w && !dock; w = w->parentWidget()) dock = qobject_cast<QDockWidget*>(w);
    if (!dock) return;
    if (on) {
        dockWasFloating_ = dock->isFloating();
        dock->setFloating(true);
        const QScreen* screen = dock->screen() ? dock->screen() : QGuiApplication::primaryScreen();
        const QRect avail = screen->availableGeometry();
        const int h = std::min(avail.height(), std::max(dock->height(), 640));
        dock->setGeometry(avail.x(), avail.y() + (avail.height() - h) / 2, avail.width(), h);
        dock->raise();
    } else if (!dockWasFloating_) {
        dock->setFloating(false);
    }
}

void TrakcrateTrackEditor::openEditRender()
{
    if (dir_.isEmpty() || editPanel_->regions().empty()) return;
    EditRenderDialog dlg(editRequest(), &editCache_, library_ ? library_() : nullptr, config_, this);
    connect(&dlg, &EditRenderDialog::created, this, [this](const QString& trackDir, const QString& message) {
        emit status(message);
        if (!trackDir.isEmpty()) emit trackChanged(trackDir);          // the library views refresh
    });
    dlg.exec();
}

int TrakcrateTrackEditor::rating() const { return stars_->rating(); }
QString TrakcrateTrackEditor::comment() const { return comment_->text(); }

void TrakcrateTrackEditor::setTrack(const QString& trackDir)
{
    const bool newTrack = trackDir != dir_;
    if (newTrack) {
        undo_->clear();                                  // history belongs to one track
        editPanel_->resetRegions();                      // and so do the regions of an edit
        previewer_->stop();
        editCache_ = trakcrate::EditRenderCache();
    }
    dir_ = trackDir;
    const bool on = !dir_.isEmpty();
    stars_->setEditable(on);
    stars_->setEnabled(on);
    editMarks_->setEnabled(on);
    gridEdit_->setEnabled(on);
    editRegions_->setEnabled(on);
    if (!on) { editMarks_->setChecked(false); gridEdit_->setChecked(false); editRegions_->setChecked(false); }
    if (newTrack || !on) sessionKnown_ = false;
    comment_->setEnabled(on);
    undoButton_->setEnabled(undo_->canUndo());
    redoButton_->setEnabled(undo_->canRedo());
    reload();
}

void TrakcrateTrackEditor::reload()
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) {
        title_->setText(QStringLiteral("No trakcrate track loaded"));
        facts_->clear();
        art_->setTrack(QString());
        stars_->setRating(0);
        comment_->clear();
        shownComment_.clear();
        wave_->clear();
        markList_->clear();
        gridReadout_->clear();
        editPanel_->setTrack(trakcrate::BeatGrid(), 0);
        return;
    }
    QString title = QString::fromStdString(t.title());
    if (title.isEmpty()) title = QString::fromStdString(t.dir.filename().string());
    title_->setText(title);
    stars_->setRating(t.rating());
    shownComment_ = QString::fromStdString(t.comment());
    comment_->setText(shownComment_);
    showFacts();
    {
        TrackAnalysis a = trakcrateTrackAnalysis(dir_);
        a.cues.clear();                                    // the cuemarks are drawn by the widget itself, in green
        wave_->setAnalysis(a);
        showMarks();
        editPanel_->setTrack(trakcrate::beatGridFrom(t.grid), a.lengthMs());
        if (!sessionKnown_) {                              // the grid as it is now is what Revert goes back to
            trakcrate::Track g;
            if (trakcrate::loadTrack(dir_.toStdString(), &g)) {
                sessionGrid_ = trakcrate::beatGridFrom(g.grid);
                sessionOrigin_ = trakcrate::origin(g.source, "beatgrid");
                sessionKnown_ = true;
            }
        }
        showGrid();
    }
    if (art_->trackDir() != dir_) art_->setTrack(dir_);        // a meta edit does not reload the picture
}

void TrakcrateTrackEditor::syncWaveScroll()
{
    const double len = wave_->analysis().lengthMs();
    const double span = wave_->viewSpan() > 0 ? wave_->viewSpan() : len;
    const double start = wave_->viewSpan() > 0 ? wave_->viewStart() : 0.0;
    syncingScroll_ = true;
    waveScroll_->setRange(0, int(std::max(0.0, std::floor(len - span))));
    waveScroll_->setPageStep(int(std::max(1.0, span)));
    waveScroll_->setSingleStep(int(std::max(1.0, span / 10)));
    waveScroll_->setValue(int(std::lround(start)));
    waveScroll_->setEnabled(len > span);
    syncingScroll_ = false;
}

void TrakcrateTrackEditor::showFacts()
{
    trakcrate::Track t;
    if (!trakcrate::loadTrack(dir_.toStdString(), &t)) return;
    const trakcrate::BeatGrid g = trakcrate::beatGridFrom(t.grid);
    QStringList lines;
    auto add = [&](const char* label, const QString& value) { if (!value.isEmpty()) lines << QStringLiteral("%1: %2").arg(QLatin1String(label), value); };
    add("Artist", QString::fromStdString(t.meta.get("artist")));
    add("Album", QString::fromStdString(t.meta.get("album")));
    add("BPM", t.hasGrid && !g.empty() ? QString::number(g.bpm, 'f', 2) : QString::fromStdString(t.meta.get("bpm_tag")));
    add("Key", QString::fromStdString(t.meta.get("key")));
    const long long ms = t.meta.getInt("duration_ms", 0);
    if (ms > 0) add("Length", QStringLiteral("%1:%2").arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')));
    if (t.meta.has("peak_dbfs")) add("Peak / RMS", QStringLiteral("%1 / %2 dBFS").arg(QString::fromStdString(t.meta.get("peak_dbfs")), QString::fromStdString(t.meta.get("rms_dbfs"))));
    QStringList origins;
    for (const char* grp : {"meta", "beatgrid", "cues", "waveform", "artwork"})
        origins << QStringLiteral("%1 %2").arg(QLatin1String(grp), QString::fromStdString(trakcrate::origin(t.source, grp)));
    add("Origin", origins.join(QStringLiteral(", ")));
    facts_->setText(lines.join(QLatin1Char('\n')));
}

void TrakcrateTrackEditor::showMarks()
{
    trakcrate::Track t;
    QList<WaveCuemark> marks;
    markList_->clear();
    if (!dir_.isEmpty() && trakcrate::loadTrack(dir_.toStdString(), &t)) {
        int n = 0;
        for (const trakcrate::Cuemark& m : t.cuemarks.marks) {
            WaveCuemark w;
            w.ms = m.ms;
            w.label = QString::fromStdString(m.label);
            w.hot = m.hot;
            double endMs = 0;
            if (trakcrate::cuemarkLoopEnd(m, &endMs)) w.loopEndMs = endMs;
            marks.append(w);
            const qint64 ms = qint64(m.ms + 0.5);
            markList_->addItem(QStringLiteral("%1  %2:%3.%4  %5%6%7").arg(++n).arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
                                   .arg(ms % 1000, 3, 10, QLatin1Char('0')).arg(w.label, m.hot ? QStringLiteral("  [%1]").arg(QChar(m.hot)) : QString(),
                                           w.loopEndMs > 0 ? QStringLiteral("  loop to %1.%2").arg(qint64(w.loopEndMs) / 1000 / 60).arg(qint64(w.loopEndMs) / 1000 % 60, 2, 10, QLatin1Char('0')) : QString()));
        }
    }
    wave_->setCuemarks(marks);
}

// The waveform again from the files (the beat lines moved), at the same zoom.
void TrakcrateTrackEditor::refreshWave()
{
    const double start = wave_->viewStart(), span = wave_->viewSpan();
    TrackAnalysis a = trakcrateTrackAnalysis(dir_);
    a.cues.clear();
    wave_->setAnalysis(a);
    if (span > 0) wave_->setView(start, span);
}

void TrakcrateTrackEditor::showGrid()
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) { gridReadout_->clear(); return; }
    const trakcrate::BeatGrid g = trakcrate::beatGridFrom(t.grid);
    if (g.empty()) { gridReadout_->setText(QStringLiteral("no grid")); return; }
    const double off = sessionKnown_ ? trakcrate::gridOffsetMs(g, sessionGrid_) : 0.0;
    gridReadout_->setText(QStringLiteral("first beat %1 ms (%2%3 ms)").arg(double(g.firstBeatUs) / 1000.0, 0, 'f', 1)
                              .arg(off >= 0 ? "+" : "").arg(off, 0, 'f', 1));
}

double TrakcrateTrackEditor::gridOffsetMs() const
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !sessionKnown_ || !trakcrate::loadTrack(dir_.toStdString(), &t)) return 0.0;
    return trakcrate::gridOffsetMs(trakcrate::beatGridFrom(t.grid), sessionGrid_);
}

bool TrakcrateTrackEditor::confirmed()
{
    trakcrate::Track t;
    if (!trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    if (trakcrate::origin(t.source, "beatgrid") != "rekordbox" || confirmedDir_ == dir_) return true;
    const QString q = QStringLiteral("This beat grid came from rekordbox. Edit it anyway?");
    const bool yes = confirmGrid_ ? confirmGrid_(q)
                                  : QMessageBox::question(this, QStringLiteral("trakcrate"), q) == QMessageBox::Yes;
    if (yes) confirmedDir_ = dir_;
    return yes;
}

bool TrakcrateTrackEditor::applyGrid(const QString& dir, const trakcrate::BeatGrid& grid, const std::string& origin)
{
    trakcrate::Track t;
    std::string err;
    if (dir.isEmpty() || !trakcrate::loadTrack(dir.toStdString(), &t, &err)) return false;
    if (!trakcrate::saveGridEdit(&t, grid, &err, origin)) {
        emit status(QStringLiteral("Could not save: %1").arg(QString::fromStdString(err)));
        return false;
    }
    emit trackChanged(dir);
    emit gridChanged(dir);
    return true;
}

bool TrakcrateTrackEditor::pushGrid(const trakcrate::BeatGrid& after, const QString& text, bool merge)
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    const trakcrate::BeatGrid before = trakcrate::beatGridFrom(t.grid);
    if (before.empty() || after.firstBeatUs == before.firstBeatUs) return false;
    if (!confirmed()) return false;
    undo_->push(new GridEditCommand(this, dir_, before, trakcrate::origin(t.source, "beatgrid"), after, text, merge));
    return true;
}

bool TrakcrateTrackEditor::nudgeGrid(double deltaMs)
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    return pushGrid(trakcrate::nudgedGrid(trakcrate::beatGridFrom(t.grid), deltaMs), QStringLiteral("Nudge beat grid"), true);
}

bool TrakcrateTrackEditor::setBeatOneAt(double ms)
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    return pushGrid(trakcrate::gridWithBeatAt(trakcrate::beatGridFrom(t.grid), ms), QStringLiteral("Set beat 1"), false);
}

bool TrakcrateTrackEditor::revertGrid()
{
    if (!sessionKnown_) return false;
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    return pushGrid(sessionGrid_, QStringLiteral("Revert beat grid"), false);
}

int TrakcrateTrackEditor::cuemarkCount() const { return wave_->cuemarks().size(); }

bool TrakcrateTrackEditor::applyMarks(const QString& dir, const trakcrate::CuemarkFile& marks)
{
    trakcrate::Track t;
    std::string err;
    if (dir.isEmpty() || !trakcrate::loadTrack(dir.toStdString(), &t, &err)) return false;
    if (!trakcrate::saveCuemarkEdit(&t, marks, &err)) {
        emit status(QStringLiteral("Could not save: %1").arg(QString::fromStdString(err)));
        return false;
    }
    emit trackChanged(dir);
    return true;
}

bool TrakcrateTrackEditor::pushMarks(const trakcrate::CuemarkFile& after, const QString& text)
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    undo_->push(new CuemarkEditCommand(this, dir_, t.cuemarks, after, text));
    return true;
}

bool TrakcrateTrackEditor::addCuemark(double ms, const QString& label)
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    trakcrate::CuemarkFile f = t.cuemarks;
    const int at = trakcrate::addCuemark(&f, trakcrate::beatGridFrom(t.grid), ms, label.toStdString());
    if (at < 0) return false;
    if (const char hot = trakcrate::freeHotLetter(f)) f.marks[size_t(at)].hot = hot;       // the first three marks are the hot cues A, B, C (keys a, b, c)
    return pushMarks(f, QStringLiteral("Add cuemark"));
}

bool TrakcrateTrackEditor::moveCuemark(int index, double ms)
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    trakcrate::CuemarkFile f = t.cuemarks;
    if (trakcrate::moveCuemark(&f, trakcrate::beatGridFrom(t.grid), index, ms) < 0) { showMarks(); return false; }
    return pushMarks(f, QStringLiteral("Move cuemark"));
}

bool TrakcrateTrackEditor::removeCuemark(int index)
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    trakcrate::CuemarkFile f = t.cuemarks;
    if (!trakcrate::removeCuemark(&f, index)) return false;
    return pushMarks(f, QStringLiteral("Delete cuemark"));
}

bool TrakcrateTrackEditor::setCuemarkHot(int index, char hot)
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    trakcrate::CuemarkFile f = t.cuemarks;
    if (!trakcrate::setCuemarkHot(&f, index, hot)) return false;
    return pushMarks(f, QStringLiteral("Hot cue"));
}

bool TrakcrateTrackEditor::setLoopEnd(int index, double ms)
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    const trakcrate::BeatGrid g = trakcrate::beatGridFrom(t.grid);
    if (g.empty()) { emit status(QStringLiteral("A loop needs a beat grid")); return false; }
    trakcrate::CuemarkFile f = t.cuemarks;
    if (!trakcrate::endCuemarkLoopAt(&f, g, index, ms, double(t.meta.getInt("duration_ms", 0)))) {
        emit status(QStringLiteral("No loop there: it must be at least a quarter beat long and end inside the track"));
        return false;
    }
    return pushMarks(f, QStringLiteral("End loop"));
}

bool TrakcrateTrackEditor::scaleLoop(int index, bool doubleIt)
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    const trakcrate::BeatGrid g = trakcrate::beatGridFrom(t.grid);
    if (g.empty()) { emit status(QStringLiteral("A loop needs a beat grid")); return false; }
    trakcrate::CuemarkFile f = t.cuemarks;
    if (!trakcrate::scaleCuemarkLoop(&f, g, index, doubleIt, double(t.meta.getInt("duration_ms", 0)))) {
        emit status(QStringLiteral("The loop cannot be made %1 (1/2 to 32 beats, inside the track)").arg(doubleIt ? QStringLiteral("longer") : QStringLiteral("shorter")));
        return false;
    }
    return pushMarks(f, doubleIt ? QStringLiteral("Double loop") : QStringLiteral("Half loop"));
}

bool TrakcrateTrackEditor::removeLoop(int index)
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    trakcrate::CuemarkFile f = t.cuemarks;
    if (!trakcrate::clearCuemarkLoop(&f, index)) return false;
    return pushMarks(f, QStringLiteral("Remove loop"));
}

bool TrakcrateTrackEditor::setCuemarkLabel(int index, const QString& label)
{
    trakcrate::Track t;
    if (dir_.isEmpty() || !trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    trakcrate::CuemarkFile f = t.cuemarks;
    if (!trakcrate::setCuemarkLabel(&f, index, label.toStdString())) return false;
    return pushMarks(f, QStringLiteral("Name cuemark"));
}

bool TrakcrateTrackEditor::writeMeta(const QString& dir, const QString& key, const QString& value)
{
    trakcrate::Track t;
    std::string err;
    if (dir.isEmpty() || !trakcrate::loadTrack(dir.toStdString(), &t, &err)) return false;
    if (key == QLatin1String("rating")) t.meta.setInt("rating", value.toInt());
    else t.meta.set(key.toStdString(), value.toStdString());
    trakcrate::setOrigin(t.source, "meta", "edited");
    if (!trakcrate::saveMeta(t, &err) || !trakcrate::saveSource(t, &err)) {
        emit status(QStringLiteral("Could not save: %1").arg(QString::fromStdString(err)));
        return false;
    }
    emit trackChanged(dir);
    return true;
}

bool TrakcrateTrackEditor::setRating(int stars)
{
    if (dir_.isEmpty()) return false;
    stars = std::max(0, std::min(5, stars));
    trakcrate::Track t;
    if (!trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    const int old = t.rating();
    if (old == stars) { stars_->setRating(stars); return true; }
    undo_->push(new MetaEditCommand(this, dir_, QStringLiteral("rating"), QString::number(old), QString::number(stars), QStringLiteral("Rating")));
    return rating() == stars;                                // push() ran redo(): the widget was reloaded from the file
}

bool TrakcrateTrackEditor::setComment(const QString& text)
{
    if (dir_.isEmpty()) return false;
    trakcrate::Track t;
    if (!trakcrate::loadTrack(dir_.toStdString(), &t)) return false;
    const QString old = QString::fromStdString(t.comment());
    if (old == text) return true;
    undo_->push(new MetaEditCommand(this, dir_, QStringLiteral("comment"), old, text, QStringLiteral("Comment")));
    return true;
}

void TrakcrateTrackEditor::commentCommitted()
{
    if (comment_->text() == shownComment_) return;           // focus left without a change
    setComment(comment_->text());
}

} // namespace vdj
