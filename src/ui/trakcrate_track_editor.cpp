#include "trakcrate_track_editor.h"

#include <QComboBox>
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
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include "artwork_view.h"
#include "star_rating_widget.h"
#include "trakcrate/cuemark_edit.h"
#include "trakcrate/grid_edit.h"
#include "trakcrate/track.h"
#include "trakcrate_analysis.h"
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
    comment_->setPlaceholderText(QStringLiteral("Comment"));
    wave_ = new WaveformWidget;
    wave_->setInteractive(true);
    wave_->setMinimumHeight(90);
    editMarks_ = new QPushButton(QStringLiteral("Edit cuemarks"));
    editMarks_->setCheckable(true);
    editMarks_->setToolTip(QStringLiteral("Double click adds a cuemark (Alt: free position), drag moves it, right click for the menu, M adds at the play position, Delete removes"));
    snapBox_ = new QComboBox;
    snapBox_->addItem(QStringLiteral("Snap to beat"), int(WaveformWidget::Snap::Beat));
    snapBox_->addItem(QStringLiteral("Snap to bar"), int(WaveformWidget::Snap::Bar));
    snapBox_->addItem(QStringLiteral("Free"), int(WaveformWidget::Snap::None));
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
    col->addWidget(comment_);
    col->addWidget(wave_, 1);
    auto* markRow = new QHBoxLayout;
    markRow->addWidget(editMarks_);
    markRow->addWidget(snapBox_);
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
    connect(gridEdit_, &QPushButton::toggled, this, [this](bool on) {
        if (on) editMarks_->setChecked(false);
        wave_->setGridEditing(on);
    });
    connect(editMarks_, &QPushButton::toggled, this, [this](bool on) { if (on) gridEdit_->setChecked(false); });
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

void TrakcrateTrackEditor::setConfig(ConfigManager* config) { art_->setConfig(config); }

int TrakcrateTrackEditor::rating() const { return stars_->rating(); }
QString TrakcrateTrackEditor::comment() const { return comment_->text(); }

void TrakcrateTrackEditor::setTrack(const QString& trackDir)
{
    const bool newTrack = trackDir != dir_;
    if (newTrack) undo_->clear();                        // history belongs to one track
    dir_ = trackDir;
    const bool on = !dir_.isEmpty();
    stars_->setEditable(on);
    stars_->setEnabled(on);
    editMarks_->setEnabled(on);
    gridEdit_->setEnabled(on);
    if (!on) { editMarks_->setChecked(false); gridEdit_->setChecked(false); }
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
            marks.append(w);
            const qint64 ms = qint64(m.ms + 0.5);
            markList_->addItem(QStringLiteral("%1  %2:%3.%4  %5%6").arg(++n).arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
                                   .arg(ms % 1000, 3, 10, QLatin1Char('0')).arg(w.label, m.hot ? QStringLiteral("  [%1]").arg(QChar(m.hot)) : QString()));
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
    if (trakcrate::addCuemark(&f, trakcrate::beatGridFrom(t.grid), ms, label.toStdString()) < 0) return false;
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
