#include "trakcrate_edit_panel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "trakcrate/edit_output.h"

using namespace trakcrate;

namespace vdj {

// Replaces the whole region list; undo puts the previous list and selection back. Edits of one parameter of one region in a row merge.
class EditRegionsCommand : public QUndoCommand {
public:
    EditRegionsCommand(TrakcrateEditPanel* p, std::vector<EditRegion> before, int selBefore, std::vector<EditRegion> after, int selAfter, const QString& text, int mergeKey)
        : QUndoCommand(text), p_(p), before_(std::move(before)), after_(std::move(after)), selBefore_(selBefore), selAfter_(selAfter), key_(mergeKey) {}
    void redo() override { p_->applyRegions(after_, selAfter_); }
    void undo() override { p_->applyRegions(before_, selBefore_); }
    int id() const override { return key_ >= 0 ? 0x45444900 + key_ : -1; }
    bool mergeWith(const QUndoCommand* other) override
    {
        auto* o = dynamic_cast<const EditRegionsCommand*>(other);
        if (!o || o->p_ != p_ || key_ < 0 || o->key_ != key_) return false;
        after_ = o->after_;
        selAfter_ = o->selAfter_;
        return true;
    }

private:
    TrakcrateEditPanel* p_;
    std::vector<EditRegion> before_, after_;
    int selBefore_, selAfter_, key_;
};

namespace {

QString mmss(double sec)
{
    const int s = int(std::lround(std::max(0.0, sec)));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

struct Choice { QString text; double value; };

QList<Choice> choicesFor(EditAlgo a)
{
    QList<Choice> out;
    switch (a) {
    case EditAlgo::Cut:
        for (int ms : kCutFadeMs) out.append({QStringLiteral("%1 ms").arg(ms), double(ms)});
        break;
    case EditAlgo::Cross:
        for (double b : kCrossBeats) out.append({b < 1 ? QStringLiteral("1/2 beat") : QStringLiteral("%1 beat%2").arg(b).arg(b > 1 ? "s" : ""), b});
        break;
    case EditAlgo::Fadeout:
        for (double b : kFadeoutBeats) out.append({b < 1 ? QStringLiteral("1/2 beat") : QStringLiteral("%1 beat%2").arg(b).arg(b > 1 ? "s" : ""), b});
        break;
    case EditAlgo::TailOverlap:
        for (double b : kTailBeats) out.append({QStringLiteral("%1 beat%2").arg(b).arg(b == 1 ? "" : "s"), b});
        break;
    default:
        break;
    }
    return out;
}

double defaultParam(EditAlgo a)
{
    switch (a) {
    case EditAlgo::Cut: return 10;
    case EditAlgo::Cross: return 1;
    case EditAlgo::Fadeout: return 1;
    case EditAlgo::TailOverlap: return 1;
    default: return 0;
    }
}

void fill(QComboBox* box, EditAlgo a, double current)
{
    box->clear();
    int sel = 0;
    for (const Choice& c : choicesFor(a)) {
        if (std::fabs(c.value - current) < 1e-9) sel = box->count();
        box->addItem(c.text, c.value);
    }
    box->setCurrentIndex(sel);
}

QString algoName(EditAlgo a)
{
    switch (a) {
    case EditAlgo::Cut: return QStringLiteral("cut");
    case EditAlgo::Cross: return QStringLiteral("cross");
    case EditAlgo::Fadeout: return QStringLiteral("fadeout");
    case EditAlgo::TailOverlap: return QStringLiteral("tailoverlap");
    default: return QStringLiteral("earlycut");
    }
}

QString shortParam(const EditRegion& r)
{
    const EditAlgo a = r.algo == EditAlgo::EarlyCut ? r.earlyJoinAlgo : r.algo;
    const double p = r.algo == EditAlgo::EarlyCut ? r.earlyJoinParam : r.algoParam;
    if (a == EditAlgo::Cut) return QStringLiteral("%1ms").arg(p);
    return QStringLiteral("%1b").arg(p);
}

} // namespace

TrakcrateEditPanel::TrakcrateEditPanel(QUndoStack* undo, QWidget* parent) : QWidget(parent), undo_(undo)
{
    rangeLabel_ = new QLabel(QStringLiteral("Drag on the waveform to select a range"));
    cutButton_ = new QPushButton(QStringLiteral("Cut"));
    cutButton_->setToolTip(QStringLiteral("Remove the selected range (rounded to whole bars from a bar line)"));
    loopButton_ = new QPushButton(QStringLiteral("Loop"));
    loopButton_->setToolTip(QStringLiteral("Repeat the selected range (1, 2, 4, 8 or 16 bars from the nearest beat)"));
    removeButton_ = new QPushButton(QStringLiteral("Remove"));
    removeButton_->setToolTip(QStringLiteral("Delete the selected region (Delete)"));
    list_ = new QListWidget;
    list_->setMaximumHeight(110);
    readout_ = new QLabel;
    readout_->setWordWrap(true);
    previewButton_ = new QPushButton(QStringLiteral("Preview..."));
    previewButton_->setToolTip(QStringLiteral("Play the edit before saving anything, from the blue play marker if you clicked one on the waveform (Space on the waveform does the same; press again to stop)"));
    previewRegionButton_ = new QPushButton(QStringLiteral("Preview region"));
    previewRegionButton_->setFocusPolicy(Qt::NoFocus);          // Space belongs to the waveform: it toggles the preview
    previewButton_->setFocusPolicy(Qt::NoFocus);
    previewRegionButton_->setToolTip(QStringLiteral("Play just the selected cut or loop with 2 bars before and after it"));
    loopExportButton_ = new QPushButton(QStringLiteral("Export loop..."));
    loopExportButton_->setToolTip(QStringLiteral("Save the selected loop (or the selected range: 4 beats) as a sample for Acid"));
    renderButton_ = new QPushButton(QStringLiteral("Render..."));
    renderButton_->setToolTip(QStringLiteral("Save the edit as a new track (the original is never changed)"));

    algoBox_ = new QComboBox;
    paramBox_ = new QComboBox;
    barsLabel_ = new QLabel(QStringLiteral("Bars"));
    barsBox_ = new QComboBox;
    for (int b : kLoopBars) barsBox_->addItem(QString::number(b), b);
    timesLabel_ = new QLabel(QStringLiteral("Times"));
    timesSpin_ = new QSpinBox;
    timesSpin_->setRange(kLoopTimesMin, kLoopTimesMax);
    timesSpin_->setToolTip(QStringLiteral("How often the section is played in total"));
    shiftBox_ = new QComboBox;
    for (int s : kEarlyShift) shiftBox_->addItem(QStringLiteral("%1 beat%2").arg(s).arg(s > 1 ? "s" : ""), s);
    dirBox_ = new QComboBox;
    dirBox_->addItem(QStringLiteral("before the bar"), 0);
    dirBox_->addItem(QStringLiteral("after the bar"), 1);
    joinBox_ = new QComboBox;
    joinBox_->addItem(QStringLiteral("join: cut"), int(EditAlgo::Cut));
    joinBox_->addItem(QStringLiteral("join: cross"), int(EditAlgo::Cross));
    joinParamBox_ = new QComboBox;

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    auto* row = new QHBoxLayout;
    row->addWidget(cutButton_);
    row->addWidget(loopButton_);
    row->addWidget(rangeLabel_, 1);
    row->addWidget(removeButton_);
    col->addLayout(row);
    col->addWidget(list_);
    params_ = new QWidget;
    auto* pl = new QHBoxLayout(params_);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(new QLabel(QStringLiteral("Click avoidance")));
    pl->addWidget(algoBox_);
    pl->addWidget(paramBox_);
    pl->addWidget(barsLabel_);
    pl->addWidget(barsBox_);
    pl->addWidget(timesLabel_);
    pl->addWidget(timesSpin_);
    earlyRow_ = new QWidget;
    auto* el = new QHBoxLayout(earlyRow_);
    el->setContentsMargins(0, 0, 0, 0);
    el->addWidget(shiftBox_);
    el->addWidget(dirBox_);
    el->addWidget(joinBox_);
    el->addWidget(joinParamBox_);
    pl->addWidget(earlyRow_);
    pl->addStretch(1);
    col->addWidget(params_);
    auto* outRow = new QHBoxLayout;
    outRow->addWidget(readout_, 1);
    outRow->addWidget(loopExportButton_);
    outRow->addWidget(previewRegionButton_);
    outRow->addWidget(previewButton_);
    outRow->addWidget(renderButton_);
    col->addLayout(outRow);

    connect(cutButton_, &QPushButton::clicked, this, [this] { addCutFromRange(); });
    connect(loopButton_, &QPushButton::clicked, this, [this] { addLoopFromRange(); });
    connect(loopExportButton_, &QPushButton::clicked, this, [this] {
        int a = 0, n = 0;
        if (loopSection(&a, &n)) emit loopExportRequested(a, n);
    });
    connect(previewButton_, &QPushButton::clicked, this, [this] { emit previewRequested(previewStartMs_, -1); });
    connect(previewRegionButton_, &QPushButton::clicked, this, [this] {
        double a, b;
        if (regionPreviewWindow(&a, &b)) emit previewRequested(a, b);
    });
    connect(renderButton_, &QPushButton::clicked, this, &TrakcrateEditPanel::renderRequested);
    connect(removeButton_, &QPushButton::clicked, this, [this] { removeRegion(selected_); });
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) { if (!updating_) selectRegion(row); });
    auto* del = new QShortcut(Qt::Key_Delete, list_);
    del->setContext(Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, [this] { removeRegion(selected_); });
    connect(algoBox_, QOverload<int>::of(&QComboBox::activated), this, [this](int) { paramsFromWidgets(); });
    for (QComboBox* b : {paramBox_, barsBox_, shiftBox_, dirBox_, joinBox_, joinParamBox_})
        connect(b, QOverload<int>::of(&QComboBox::activated), this, [this](int) { paramsFromWidgets(); });
    connect(timesSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { if (!updating_) paramsFromWidgets(); });
    refresh();
}

long long TrakcrateEditPanel::totalFrames() const { return std::llround(lengthMs_ * kEditRate / 1000.0); }

double TrakcrateEditPanel::beatMs(double beat) const { return double(grid_.firstBeatUs) / 1000.0 + beat * 60000.0 / grid_.bpm; }

double TrakcrateEditPanel::beatOfMs(double ms) const { return (ms - double(grid_.firstBeatUs) / 1000.0) * grid_.bpm / 60000.0; }

void TrakcrateEditPanel::setTrack(const BeatGrid& grid, double lengthMs)
{
    grid_ = grid;
    lengthMs_ = lengthMs;
    refresh();
    emit regionsChanged();
}

void TrakcrateEditPanel::setPreviewStart(double ms)
{
    previewStartMs_ = ms < 0 ? -1 : std::min(ms, lengthMs_);
}

bool TrakcrateEditPanel::regionPreviewWindow(double* startMs, double* stopMs) const
{
    if (grid_.empty() || selected_ < 0 || size_t(selected_) >= regions_.size()) return false;
    const EditRegion at = resolveRegions({regions_[size_t(selected_)]}).front();
    *startMs = std::max(0.0, beatMs(at.startBeat - 8));
    *stopMs = std::min(lengthMs_, beatMs(at.startBeat + at.lenBeats + 8));
    return true;
}

void TrakcrateEditPanel::resetRegions()
{
    previewStartMs_ = -1;
    regions_.clear();
    selected_ = -1;
    rangeA_ = rangeB_ = 0;
    refresh();
    emit regionsChanged();
}

bool TrakcrateEditPanel::loopSection(int* startBeat, int* lenBeats) const
{
    if (grid_.empty()) return false;
    if (selected_ >= 0 && size_t(selected_) < regions_.size() && regions_[size_t(selected_)].kind == EditKind::Loop) {
        *startBeat = regions_[size_t(selected_)].startBeat;
        *lenBeats = regions_[size_t(selected_)].lenBeats;
        return true;
    }
    if (!hasRange()) return false;
    *startBeat = std::max(0, int(std::lround(beatOfMs(rangeA_))));
    *lenBeats = 4;
    return true;
}

QList<WaveEditRegion> TrakcrateEditPanel::waveRegions() const
{
    QList<WaveEditRegion> out;
    if (grid_.empty()) return out;
    for (size_t i = 0; i < regions_.size(); ++i) {
        const EditRegion& r = regions_[i];
        const EditRegion at = resolveRegions({r}).front();
        WaveEditRegion w;
        w.startMs = beatMs(at.startBeat);
        w.endMs = beatMs(at.startBeat + at.lenBeats);
        w.loop = r.kind == EditKind::Loop;
        w.number = int(i) + 1;
        w.text = (w.loop ? QStringLiteral("x%1 ").arg(r.loopTimes) : QString()) + algoName(r.algo);
        out.append(w);
    }
    return out;
}

QString TrakcrateEditPanel::problem() const
{
    return QString::fromStdString(validateRegions(grid_, regions_, totalFrames()));
}

QString TrakcrateEditPanel::readout() const
{
    if (regions_.empty()) return QStringLiteral("No edits yet");
    const QString bad = problem();
    if (!bad.isEmpty()) return bad;
    const EditPlan plan = planEdit(grid_, regions_, totalFrames());
    const double out = double(planOutputFrames(plan)) / kEditRate;
    return QStringLiteral("Edit: %1 -> %2").arg(mmss(lengthMs_ / 1000.0), mmss(out));
}

void TrakcrateEditPanel::refresh()
{
    updating_ = true;
    const bool hasGrid = !grid_.empty();
    setEnabled(hasGrid);
    list_->clear();
    for (size_t i = 0; i < regions_.size(); ++i) list_->addItem(QString::fromStdString(describeRegion(int(i) + 1, regions_[i])));
    list_->setCurrentRow(selected_);
    cutButton_->setEnabled(hasGrid && hasRange());
    loopButton_->setEnabled(hasGrid && hasRange());
    removeButton_->setEnabled(selected_ >= 0);
    if (hasRange()) {
        rangeLabel_->setText(QStringLiteral("Selected %1 s (%2 beats)").arg((rangeB_ - rangeA_) / 1000.0, 0, 'f', 1).arg(beatOfMs(rangeB_) - beatOfMs(rangeA_), 0, 'f', 0));
    } else {
        rangeLabel_->setText(hasGrid ? QStringLiteral("Drag on the waveform to select a range") : QStringLiteral("A beat grid is needed to make an edit"));
    }

    const bool sel = selected_ >= 0 && size_t(selected_) < regions_.size();
    params_->setEnabled(sel);
    algoBox_->clear();
    if (sel) {
        const EditRegion& r = regions_[size_t(selected_)];
        const bool loop = r.kind == EditKind::Loop;
        for (EditAlgo a : {EditAlgo::Cut, EditAlgo::Cross, EditAlgo::Fadeout}) algoBox_->addItem(algoName(a), int(a));
        if (!loop) {
            algoBox_->addItem(algoName(EditAlgo::TailOverlap), int(EditAlgo::TailOverlap));
            algoBox_->addItem(algoName(EditAlgo::EarlyCut), int(EditAlgo::EarlyCut));
        }
        algoBox_->setCurrentIndex(std::max(0, algoBox_->findData(int(r.algo))));
        const bool early = r.algo == EditAlgo::EarlyCut;
        paramBox_->setVisible(!early);
        earlyRow_->setVisible(early);
        barsLabel_->setVisible(loop);
        barsBox_->setVisible(loop);
        timesLabel_->setVisible(loop);
        timesSpin_->setVisible(loop);
        if (!early) fill(paramBox_, r.algo, r.algoParam);
        if (loop) {
            barsBox_->setCurrentIndex(std::max(0, barsBox_->findData(r.lenBeats / 4)));
            timesSpin_->setValue(r.loopTimes);
        }
        if (early) {
            shiftBox_->setCurrentIndex(std::max(0, shiftBox_->findData(r.earlyShift)));
            dirBox_->setCurrentIndex(r.earlyAfter ? 1 : 0);
            joinBox_->setCurrentIndex(std::max(0, joinBox_->findData(int(r.earlyJoinAlgo))));
            fill(joinParamBox_, r.earlyJoinAlgo, r.earlyJoinParam);
        }
    }
    {
        int a, n;
        loopExportButton_->setEnabled(hasGrid && loopSection(&a, &n));
    }
    const QString text = readout();
    readout_->setText(text);
    readout_->setStyleSheet(problem().isEmpty() ? QString() : QStringLiteral("color: #d33"));
    const bool renderable = !regions_.empty() && problem().isEmpty();
    for (QPushButton* b : {previewButton_, renderButton_}) {
        b->setEnabled(renderable);
        if (!renderable) b->setToolTip(regions_.empty() ? QStringLiteral("Add a cut or a loop first") : problem());
    }
    {
        double a, b;
        previewRegionButton_->setEnabled(renderable && regionPreviewWindow(&a, &b));
    }
    if (renderable) {
        previewButton_->setToolTip(QStringLiteral("Play the edit before saving anything, from the blue play marker if you clicked one on the waveform"));
        renderButton_->setToolTip(QStringLiteral("Save the edit as a new track (the original is never changed)"));
    }
    updating_ = false;
}

void TrakcrateEditPanel::applyRegions(const std::vector<EditRegion>& regions, int selected)
{
    regions_ = regions;
    selected_ = (selected >= 0 && size_t(selected) < regions_.size()) ? selected : -1;
    refresh();
    emit regionsChanged();
}

bool TrakcrateEditPanel::pushRegions(std::vector<EditRegion> after, int selectedAfter, const QString& text, bool mergeable)
{
    const int key = mergeable ? std::max(0, selected_) : -1;
    undo_->push(new EditRegionsCommand(this, regions_, selected_, std::move(after), selectedAfter, text, key));
    return true;
}

bool TrakcrateEditPanel::setRange(double startMs, double endMs)
{
    if (grid_.empty()) { emit status(QStringLiteral("A beat grid is needed to make an edit")); return false; }
    rangeA_ = std::max(0.0, std::min(startMs, endMs));
    rangeB_ = std::min(lengthMs_, std::max(startMs, endMs));
    refresh();
    return hasRange();
}

bool TrakcrateEditPanel::addRegion(EditRegion r)
{
    std::vector<EditRegion> next = regions_;
    next.push_back(r);
    const std::string bad = validateRegions(grid_, next, totalFrames());
    if (!bad.empty()) {
        emit status(QString::fromStdString(bad));
        return false;
    }
    std::stable_sort(next.begin(), next.end(), [](const EditRegion& a, const EditRegion& b) { return a.startBeat < b.startBeat; });
    int at = 0;
    for (size_t i = 0; i < next.size(); ++i)
        if (next[i].startBeat == r.startBeat) at = int(i);
    pushRegions(next, at, r.kind == EditKind::Cut ? QStringLiteral("Add cut") : QStringLiteral("Add loop"));
    rangeA_ = rangeB_ = 0;
    refresh();
    emit rangeConsumed();
    return true;
}

bool TrakcrateEditPanel::addCutFromRange()
{
    if (!hasRange() || grid_.empty()) return false;
    EditRegion r;
    r.kind = EditKind::Cut;
    const int a = std::max(0, 4 * int(std::lround(beatOfMs(rangeA_) / 4)));
    int b = 4 * int(std::lround(beatOfMs(rangeB_) / 4));
    if (b < a + 4) b = a + 4;
    r.startBeat = a;
    r.lenBeats = b - a;
    r.algo = EditAlgo::Cut;
    r.algoParam = defaultParam(EditAlgo::Cut);
    return addRegion(r);
}

bool TrakcrateEditPanel::addLoopFromRange()
{
    if (!hasRange() || grid_.empty()) return false;
    EditRegion r;
    r.kind = EditKind::Loop;
    r.startBeat = std::max(0, int(std::lround(beatOfMs(rangeA_))));
    const double beats = std::max(1.0, beatOfMs(rangeB_) - beatOfMs(rangeA_));
    int best = kLoopBars[0];
    for (int bars : kLoopBars)                                              // nearest allowed length, compared by ratio (3 bars -> 4)
        if (std::fabs(std::log(bars * 4.0 / beats)) < std::fabs(std::log(best * 4.0 / beats)) - 1e-9) best = bars;
    r.lenBeats = barsToBeats(best);
    r.algo = EditAlgo::Cut;
    r.algoParam = defaultParam(EditAlgo::Cut);
    r.loopTimes = 4;
    return addRegion(r);
}

bool TrakcrateEditPanel::removeRegion(int index)
{
    if (index < 0 || size_t(index) >= regions_.size()) return false;
    std::vector<EditRegion> next = regions_;
    const bool loop = next[size_t(index)].kind == EditKind::Loop;
    next.erase(next.begin() + index);
    pushRegions(next, std::min(index, int(next.size()) - 1), loop ? QStringLiteral("Remove loop") : QStringLiteral("Remove cut"));
    return true;
}

bool TrakcrateEditPanel::selectRegion(int index)
{
    if (index < 0 || size_t(index) >= regions_.size()) index = -1;
    if (index == selected_) return false;
    selected_ = index;                                                      // the selection is not an edit: it is not undoable
    refresh();
    emit regionsChanged();
    return true;
}

bool TrakcrateEditPanel::changeRegion(int index, const EditRegion& after)
{
    if (index < 0 || size_t(index) >= regions_.size()) return false;
    std::vector<EditRegion> next = regions_;
    next[size_t(index)] = after;
    const std::string bad = validateRegions(grid_, next, totalFrames());
    if (!bad.empty()) {
        emit status(QString::fromStdString(bad));
        refresh();                                                          // the widgets go back to the legal values
        return false;
    }
    std::stable_sort(next.begin(), next.end(), [](const EditRegion& a, const EditRegion& b) { return a.startBeat < b.startBeat; });
    int at = 0;
    for (size_t i = 0; i < next.size(); ++i)
        if (next[i].startBeat == after.startBeat) at = int(i);
    pushRegions(next, at, QStringLiteral("Change region"), true);
    return true;
}

bool TrakcrateEditPanel::setAlgo(EditAlgo algo)
{
    if (selected_ < 0 || size_t(selected_) >= regions_.size()) return false;
    EditRegion r = regions_[size_t(selected_)];
    if (algo == r.algo) return false;
    if (r.kind == EditKind::Loop && (algo == EditAlgo::TailOverlap || algo == EditAlgo::EarlyCut)) return false;
    r.algo = algo;
    r.algoParam = defaultParam(algo);
    if (algo == EditAlgo::EarlyCut) {
        r.earlyShift = 1;
        r.earlyAfter = false;
        r.earlyJoinAlgo = EditAlgo::Cut;
        r.earlyJoinParam = defaultParam(EditAlgo::Cut);
    }
    return changeRegion(selected_, r);
}

bool TrakcrateEditPanel::setParam(double value)
{
    if (selected_ < 0 || size_t(selected_) >= regions_.size()) return false;
    EditRegion r = regions_[size_t(selected_)];
    if (r.algo == EditAlgo::EarlyCut) r.earlyJoinParam = value;
    else r.algoParam = value;
    return changeRegion(selected_, r);
}

// A widget changed: build the region it describes, resetting the parameter when the algorithm changed.
void TrakcrateEditPanel::paramsFromWidgets()
{
    if (updating_ || selected_ < 0 || size_t(selected_) >= regions_.size()) return;
    EditRegion r = regions_[size_t(selected_)];
    const EditAlgo algo = EditAlgo(algoBox_->currentData().toInt());
    if (algo != r.algo) {
        setAlgo(algo);
        return;
    }
    if (r.kind == EditKind::Loop) {
        r.lenBeats = barsToBeats(barsBox_->currentData().toInt());
        r.loopTimes = timesSpin_->value();
    }
    if (r.algo == EditAlgo::EarlyCut) {
        r.earlyShift = shiftBox_->currentData().toInt();
        r.earlyAfter = dirBox_->currentData().toInt() == 1;
        const EditAlgo ja = EditAlgo(joinBox_->currentData().toInt());
        if (ja != r.earlyJoinAlgo) {
            r.earlyJoinAlgo = ja;
            r.earlyJoinParam = defaultParam(ja);
        } else {
            r.earlyJoinParam = joinParamBox_->currentData().toDouble();
        }
    } else if (paramBox_->count() > 0) {
        r.algoParam = paramBox_->currentData().toDouble();
    }
    changeRegion(selected_, r);
}

} // namespace vdj
