#include "mix_canvas.h"

#include "mix_waveform.h"
#include "trakcrate/edit_job.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QToolTip>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vdj {

namespace {
const QColor kBarColor(220, 40, 40), kBeatColor(235, 235, 235), kPlayColor(60, 220, 90);
const QColor kLaneBg(24, 24, 28), kLaneBgSelected(36, 36, 48);
}

MixCanvas::MixCanvas(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(200, 100);
    setFocusPolicy(Qt::StrongFocus);
    vbar_ = new QScrollBar(Qt::Vertical, this);
    hbar_ = new QScrollBar(Qt::Horizontal, this);
    connect(vbar_, &QScrollBar::valueChanged, this, [this] { update(); });
    connect(hbar_, &QScrollBar::valueChanged, this, [this](int v) {
        if (adjusting_) return;
        viewStart_ = v;
        update();
        emit viewChanged();
    });
}

QString MixCanvas::formatTime(double ms)
{
    const long long s = (long long)std::floor(std::max(0.0, ms) / 1000.0);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

double MixCanvas::tickStepMs(double msPerPixel)
{
    static const double steps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200};
    for (double s : steps)
        if (s * 1000.0 / msPerPixel >= 70) return s * 1000.0;
    return 7200 * 1000.0;
}

int MixCanvas::contentWidth() const { return std::max(1, width() - (vbar_->isVisible() ? vbar_->width() : 0)); }
int MixCanvas::contentHeight() const { return std::max(1, height() - (hbar_->isVisible() ? hbar_->height() : 0)); }

void MixCanvas::setMix(const trakcrate::Mix* mix, const std::vector<trakcrate::TrackFacts>& facts, const std::vector<MixLaneInfo>& lanes)
{
    mix_ = mix;
    facts_ = facts;
    info_ = lanes;
    stretching_.assign(info_.size(), false);
    refresh();
    zoomFit();
}

void MixCanvas::refresh()
{
    eff_.clear();
    starts_.clear();
    edited_.clear();
    lengthMs_ = 0;
    if (mix_) {
        eff_ = trakcrate::effectiveTracks(*mix_, facts_);
        starts_ = trakcrate::trackStartMs(*mix_, eff_);
        lengthMs_ = trakcrate::mixLengthMs(starts_, eff_);
        for (size_t i = 0; i < eff_.size(); ++i) {
            const QVector<quint16> empty;
            const QVector<quint16>& c = i < info_.size() ? info_[i].color : empty;
            edited_.push_back(editedColorWaveform(c, eff_[i].plan, eff_[i].ratio, eff_[i].stretchedFrames));
        }
    }
    stretching_.resize(eff_.size(), false);
    layoutBars();
    clampView();
    update();
}

void MixCanvas::setTracks(const std::vector<trakcrate::TrackFacts>& facts, const std::vector<MixLaneInfo>& lanes)
{
    facts_ = facts;
    info_ = lanes;
    selected_ = -1;
    selPoint_ = PointRef();
    drag_ = Drag();
    stretching_.assign(info_.size(), false);
    refresh();
}

void MixCanvas::setStretching(int lane, bool on)
{
    if (lane >= 0 && lane < int(stretching_.size())) stretching_[size_t(lane)] = on;
    update();
}

QRect MixCanvas::laneRect(int lane) const
{
    return QRect(0, kRulerHeight + lane * (laneHeight_ + kLaneGap) - vbar_->value(), contentWidth(), laneHeight_);
}

void MixCanvas::setLaneHeight(int h)
{
    laneHeight_ = std::max(20, h);
    layoutBars();
    update();
}

void MixCanvas::layoutBars()
{
    const int bw = vbar_->sizeHint().width(), bh = hbar_->sizeHint().height();
    const int total = kRulerHeight + int(eff_.size()) * (laneHeight_ + kLaneGap);
    const bool needV = total > height() - bh;
    vbar_->setVisible(needV);
    hbar_->setVisible(lengthMs_ > span_ + 0.5);
    const int cw = contentWidth(), ch = contentHeight();
    vbar_->setGeometry(width() - bw, 0, bw, ch);
    hbar_->setGeometry(0, height() - bh, cw, bh);
    vbar_->setRange(0, std::max(0, total - ch));
    vbar_->setPageStep(ch);
    vbar_->setSingleStep(laneHeight_ / 2 + 1);
    adjusting_ = true;
    hbar_->setRange(0, int(std::max(0.0, lengthMs_ - span_)));
    hbar_->setPageStep(int(span_));
    hbar_->setSingleStep(std::max(1, int(span_ / 10)));          // an arrow click moves a tenth of the view
    hbar_->setValue(int(viewStart_));
    adjusting_ = false;
}

void MixCanvas::clampView()
{
    const double maxSpan = std::max(kMinSpanMs, lengthMs_);
    const double minSpan = std::min(kMinSpanMs, maxSpan);
    span_ = std::max(minSpan, std::min(maxSpan, span_));
    viewStart_ = std::max(0.0, std::min(std::max(0.0, lengthMs_ - span_), viewStart_));
}

void MixCanvas::setView(double startMs, double spanMs)
{
    span_ = spanMs;
    viewStart_ = startMs;
    clampView();
    layoutBars();
    update();
    emit viewChanged();
}

void MixCanvas::zoomAt(double ms, double factor)
{
    const double oldSpan = span_;
    const double frac = oldSpan > 0 ? (ms - viewStart_) / oldSpan : 0;
    span_ = oldSpan * factor;
    clampView();
    viewStart_ = ms - frac * span_;
    clampView();
    layoutBars();
    update();
    emit viewChanged();
}

int MixCanvas::nearestPeriod(double ms) const
{
    int best = -1;
    double bestD = 0;
    for (int i = 0; i + 1 < laneCount() && mix_; ++i) {
        const trakcrate::MixPair& p = mix_->tracks[size_t(i)].next;
        const double a = starts_[size_t(i)] + trakcrate::trackBeatMs(eff_[size_t(i)], p.outStartBeat);
        const double centre = a + p.lenBeats * 60000.0 / std::max(1.0, eff_[size_t(i)].bpm) / 2;
        const double d = std::fabs(ms - centre);
        if (best < 0 || d < bestD) { best = i; bestD = d; }
    }
    return best;
}

void MixCanvas::fitPeriod(int pair)
{
    if (!mix_) return;
    if (pair < 0) pair = nearestPeriod(playMs_ > 0 ? playMs_ : viewStart_ + span_ / 2);
    if (pair < 0 || pair + 1 >= laneCount()) return;
    currentPair_ = pair;
    const trakcrate::MixPair& p = mix_->tracks[size_t(pair)].next;
    const double beatMs = 60000.0 / std::max(1.0, eff_[size_t(pair)].bpm);
    const double a = starts_[size_t(pair)] + trakcrate::trackBeatMs(eff_[size_t(pair)], p.outStartBeat);
    const double len = p.lenBeats * beatMs, margin = 32 * beatMs;                  // 8 bars on each side
    const double span = len + 2 * margin;
    setView(a + len / 2 - span / 2, span);
}

void MixCanvas::fitNextPeriod(int delta)
{
    const int n = laneCount() - 1;
    if (n <= 0) return;
    const int from = currentPair_ >= 0 ? currentPair_ : nearestPeriod(playMs_ > 0 ? playMs_ : viewStart_ + span_ / 2);
    fitPeriod(std::max(0, std::min(n - 1, from + delta)));
}

void MixCanvas::zoomFit() { setView(0, std::max(lengthMs_, kMinSpanMs)); }

void MixCanvas::setPlayPosition(double ms)
{
    playMs_ = std::max(0.0, ms);
    update();
}

void MixCanvas::ensureVisible(double ms)
{
    if (ms >= viewStart_ && ms <= viewStart_ + span_ * 0.92) return;
    setView(std::max(0.0, ms - span_ * 0.08), span_);
}

double MixCanvas::msAt(int x) const { return viewStart_ + double(x) * span_ / contentWidth(); }
int MixCanvas::xAt(double ms) const { return int(std::floor((ms - viewStart_) * contentWidth() / span_)); }

int MixCanvas::trackAt(int y) const
{
    const int rel = y + vbar_->value() - kRulerHeight;
    if (rel < 0) return -1;
    const int lane = rel / (laneHeight_ + kLaneGap);
    if (lane >= int(eff_.size()) || rel % (laneHeight_ + kLaneGap) >= laneHeight_) return -1;
    return lane;
}

void MixCanvas::selectLane(int lane)
{
    selected_ = lane;
    update();
}

void MixCanvas::resizeEvent(QResizeEvent*)
{
    layoutBars();
    clampView();
}

QColor MixCanvas::curveColor(int curve)
{
    return curve == BassCurve ? QColor(240, 70, 70) : curve == HighCurve ? QColor(60, 220, 240) : QColor(250, 215, 40);
}

const trakcrate::Curve& MixCanvas::curveOf(int lane, int curve) const
{
    const trakcrate::MixTrack& t = mix_->tracks[size_t(lane)];
    return curve == BassCurve ? t.bass : curve == HighCurve ? t.high : t.volume;
}

int MixCanvas::yOfValue(int lane, double v) const
{
    const QRect r = laneRect(lane);
    return r.bottom() - int(std::round(v / 100.0 * (r.height() - 1)));
}

double MixCanvas::valueAtY(int lane, int y) const
{
    const QRect r = laneRect(lane);
    return std::max(0.0, std::min(100.0, double(r.bottom() - y) * 100.0 / std::max(1, r.height() - 1)));
}

QPoint MixCanvas::pointPos(int lane, int curve, int index) const
{
    const trakcrate::CurvePoint& p = curveOf(lane, curve).points[size_t(index)];
    return QPoint(xAt(starts_[size_t(lane)] + trakcrate::trackBeatMs(eff_[size_t(lane)], p.beat)), yOfValue(lane, p.value));
}

int MixCanvas::periodStartX(int pair) const
{
    const trakcrate::MixPair& p = mix_->tracks[size_t(pair)].next;
    return xAt(starts_[size_t(pair)] + trakcrate::trackBeatMs(eff_[size_t(pair)], p.outStartBeat));
}

int MixCanvas::periodEndX(int pair) const
{
    const trakcrate::MixPair& p = mix_->tracks[size_t(pair)].next;
    return xAt(starts_[size_t(pair)] + trakcrate::trackBeatMs(eff_[size_t(pair)], p.outStartBeat + p.lenBeats));
}

int MixCanvas::findPoint(int lane, const QPoint& pos) const
{
    const trakcrate::Curve& c = curveOf(lane, curveMode_);
    int best = -1;
    double bestD = kGrabRadius * kGrabRadius + 1;
    for (int i = 0; i < int(c.points.size()); ++i) {
        const QPoint q = pointPos(lane, curveMode_, i);
        const double d = double(q.x() - pos.x()) * (q.x() - pos.x()) + double(q.y() - pos.y()) * (q.y() - pos.y());
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

int MixCanvas::findPeriodLine(int lane, int x, int* pairOut) const
{
    const int n = laneCount();
    for (int pair : {lane, lane - 1}) {                     // lane is the outgoing track of `lane` and the incoming track of `lane - 1`
        if (pair < 0 || pair + 1 >= n) continue;
        const int a = periodStartX(pair), b = periodEndX(pair);
        *pairOut = pair;
        if (std::abs(x - b) <= 5) return 3;
        if (std::abs(x - a) <= 5) return pair == lane ? 1 : 2;
    }
    return 0;
}

int MixCanvas::pairAtX(int lane, int x) const
{
    for (int pair : {lane, lane - 1}) {
        if (pair < 0 || pair + 1 >= laneCount()) continue;
        if (x >= periodStartX(pair) && x <= periodEndX(pair)) return pair;
    }
    return -1;
}

void MixCanvas::setRegionMode(bool on)
{
    regionMode_ = on;
    if (!on) clearRange();
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void MixCanvas::setRange(int lane, double startMs, double endMs)
{
    if (lane < 0 || endMs <= startMs) {
        rangeLane_ = -1;
        rangeA_ = rangeB_ = 0;
    } else {
        rangeLane_ = lane;
        rangeA_ = startMs;
        rangeB_ = endMs;
    }
    update();
}

void MixCanvas::setCurveMode(int c)
{
    curveMode_ = std::max(0, std::min(2, c));
    selPoint_ = PointRef();
    update();
    emit curveModeChanged(curveMode_);
}

void MixCanvas::removeSelectedPoint()
{
    if (!editor_ || !mix_ || selPoint_.lane < 0 || selPoint_.index < 0) return;
    const PointRef ref = selPoint_;
    selPoint_ = PointRef();
    editor_(QStringLiteral("Remove curve point"), [ref](trakcrate::Mix& m, const std::vector<trakcrate::EffectiveTrack>&) {
        trakcrate::MixTrack& t = m.tracks[size_t(ref.lane)];
        trakcrate::removePoint(ref.curve == BassCurve ? &t.bass : ref.curve == HighCurve ? &t.high : &t.volume, ref.index);
    }, QString());
}

void MixCanvas::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    setFocus();                                                    // the number keys and Delete need the focus
    const QPoint pos = e->position().toPoint();
    const double ms = std::max(0.0, msAt(pos.x()));
    const int lane = trackAt(pos.y());
    drag_ = Drag();
    if (regionMode_ && lane >= 0 && mix_) {
        if (lane != selected_) {
            selected_ = lane;
            selPoint_ = PointRef();
            clearRange();
            emit laneClicked(lane, ms);
            update();
            return;
        }
        const double beat = std::round(trakcrate::mixMsToBeat(starts_, eff_, lane, ms));
        drag_ = Drag{Drag::Range, -1, lane, 0, -1, 0, 0, beat, ms, QString()};
        setRange(lane, 0, 0);
        return;
    }
    if (lane >= 0 && editor_ && mix_) {
        const QString key = QStringLiteral("drag%1").arg(++dragCounter_);
        // 1. a dot of the curve being edited
        const int pi = findPoint(lane, pos);
        if (pi >= 0) {
            const trakcrate::CurvePoint& p = curveOf(lane, curveMode_).points[size_t(pi)];
            drag_ = Drag{Drag::Point, -1, lane, curveMode_, pi, 0, p.value, p.beat, ms, key};
            selPoint_ = PointRef{lane, curveMode_, pi};
            selected_ = lane;
            emit laneClicked(lane, ms);
            update();
            return;
        }
        // 2. a line of a mix period
        int pair = -1;
        const int line = findPeriodLine(lane, pos.x(), &pair);
        if (line) {
            const trakcrate::MixPair& mp = mix_->tracks[size_t(pair)].next;
            drag_ = Drag{line == 1 ? Drag::OutStart : line == 2 ? Drag::InStart : Drag::End, pair, lane, 0, -1,
                         line == 2 ? mp.inStartBeat : line == 3 ? mp.lenBeats : mp.outStartBeat, 0, 0, ms, key};      // press = where the dragged thing starts
            selected_ = lane;
            emit laneClicked(lane, ms);
            update();
            return;
        }
        // 3. the line of the curve: a new point
        const trakcrate::Curve& c = curveOf(lane, curveMode_);
        const double beat = trakcrate::mixMsToBeat(starts_, eff_, lane, ms);
        const double gain = trakcrate::curveGain(c, beat, eff_[size_t(lane)].bpm) * 100.0;
        if (std::abs(yOfValue(lane, gain) - pos.y()) <= 5 && beat >= 0 && beat <= eff_[size_t(lane)].editedBeats) {
            const double b = (e->modifiers() & Qt::AltModifier) ? beat : trakcrate::snapBeat(beat, false);
            const double v = valueAtY(lane, pos.y());
            const int curve = curveMode_;
            int index = -1;
            editor_(QStringLiteral("Add curve point"), [=, &index](trakcrate::Mix& m, const std::vector<trakcrate::EffectiveTrack>& eff) {
                trakcrate::MixTrack& t = m.tracks[size_t(lane)];
                trakcrate::Curve* cv = curve == BassCurve ? &t.bass : curve == HighCurve ? &t.high : &t.volume;
                index = trakcrate::setPoint(cv, std::max(0.0, std::min(eff[size_t(lane)].editedBeats, b)), v);
            }, key);
            drag_ = Drag{Drag::Point, -1, lane, curve, index, 0, v, b, ms, key};
            selPoint_ = PointRef{lane, curve, index};
            selected_ = lane;
            emit laneClicked(lane, ms);
            update();
            return;
        }
    }
    if (pos.y() < kRulerHeight || lane >= 0) {
        playMs_ = ms;
        update();
        emit playPositionChanged(ms);
    }
    if (lane >= 0) {
        selected_ = lane;
        selPoint_ = PointRef();
        emit laneClicked(lane, ms);
    }
}

void MixCanvas::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint pos = e->position().toPoint();
    emit hovered(trackAt(pos.y()), msAt(pos.x()));
    if (drag_.kind == Drag::Range) {
        const double beat = std::round(trakcrate::mixMsToBeat(starts_, eff_, drag_.lane, msAt(pos.x())));
        const double a = trakcrate::trackBeatMs(eff_[size_t(drag_.lane)], std::min(beat, drag_.startBeat));
        const double b = trakcrate::trackBeatMs(eff_[size_t(drag_.lane)], std::max(beat, drag_.startBeat));
        setRange(drag_.lane, std::max(0.0, a), b);
        if (b > a) emit rangeDragged(drag_.lane, std::max(0.0, a), b);
        return;
    }
    if (drag_.kind == Drag::None || !editor_) {
        if (mix_ && drag_.kind == Drag::None) {
            const int lane = trackAt(pos.y());
            int pair;
            setCursor(lane >= 0 && findPeriodLine(lane, pos.x(), &pair) ? Qt::SizeHorCursor : Qt::ArrowCursor);
        }
        return;
    }
    const Drag d = drag_;
    const bool bars = !(e->modifiers() & Qt::ShiftModifier);
    const double ms = msAt(pos.x());
    if (d.kind == Drag::Point) {
        double beat = trakcrate::mixMsToBeat(starts_, eff_, d.lane, ms);
        if (e->modifiers() & Qt::ControlModifier) beat = d.startBeat;                       // Ctrl locks the beat
        else if (!(e->modifiers() & Qt::AltModifier)) beat = trakcrate::snapBeat(beat, false);
        double v = valueAtY(d.lane, pos.y());
        if (e->modifiers() & Qt::ShiftModifier) v = d.startValue;                           // Shift locks the value
        const int lane = d.lane, curve = d.curve;
        int index = d.index;
        editor_(QStringLiteral("Move curve point"), [=, &index](trakcrate::Mix& m, const std::vector<trakcrate::EffectiveTrack>& eff) {
            trakcrate::MixTrack& t = m.tracks[size_t(lane)];
            trakcrate::Curve* cv = curve == BassCurve ? &t.bass : curve == HighCurve ? &t.high : &t.volume;
            index = trakcrate::movePoint(cv, index, std::max(0.0, std::min(eff[size_t(lane)].editedBeats, beat)), v);
        }, d.key);
        drag_.index = index;
        selPoint_.index = index;
        QToolTip::showText(e->globalPosition().toPoint(), QStringLiteral("%1 %  beat %2").arg(v, 0, 'f', 0).arg(beat, 0, 'f', 2), this);
        return;
    }
    const int pair = d.pair;
    QString tip;
    if (d.kind == Drag::OutStart) {
        const double b = trakcrate::snapBeat(trakcrate::mixMsToBeat(starts_, eff_, d.lane, ms), bars);
        editor_(QStringLiteral("Move mix period"), [=](trakcrate::Mix& m, const std::vector<trakcrate::EffectiveTrack>& eff) { trakcrate::setPairOutStart(&m, pair, b, eff); }, d.key);
        tip = QStringLiteral("bar %1").arg(int(b / 4) + 1);
    } else if (d.kind == Drag::InStart) {
        // the incoming track slides under the fixed anchor: the distance dragged, in beats of the incoming track
        const double beatsPerMs = eff_[size_t(pair) + 1].bpm / 60000.0;
        const double b = trakcrate::snapBeat(d.press + (ms - d.pressMs) * beatsPerMs, bars);
        editor_(QStringLiteral("Slide incoming track"), [=](trakcrate::Mix& m, const std::vector<trakcrate::EffectiveTrack>& eff) { trakcrate::setPairInStart(&m, pair, b, eff); }, d.key);
        tip = QStringLiteral("bar %1").arg(int(b / 4) + 1);
    } else {
        const trakcrate::MixPair& mp = mix_->tracks[size_t(pair)].next;
        const double startBeat = d.lane == pair ? mp.outStartBeat : mp.inStartBeat;
        const double len = std::max(1.0, trakcrate::snapBeat(trakcrate::mixMsToBeat(starts_, eff_, d.lane, ms) - startBeat, bars));
        editor_(QStringLiteral("Change mix period length"), [=](trakcrate::Mix& m, const std::vector<trakcrate::EffectiveTrack>& eff) { trakcrate::setPairLength(&m, pair, len, eff); }, d.key);
        tip = QStringLiteral("%1 bars").arg(len / 4.0, 0, 'f', len / 4 == std::floor(len / 4) ? 0 : 2);
    }
    QToolTip::showText(e->globalPosition().toPoint(), tip, this);
}

void MixCanvas::mouseReleaseEvent(QMouseEvent*)
{
    drag_ = Drag();
    QToolTip::hideText();
}

void MixCanvas::mouseDoubleClickEvent(QMouseEvent* e)
{
    const QPoint pos = e->position().toPoint();
    const int lane = trackAt(pos.y());
    if (lane < 0 || !editor_ || !mix_) return;
    const int pi = findPoint(lane, pos);
    if (pi < 0) return;
    selPoint_ = PointRef{lane, curveMode_, pi};
    removeSelectedPoint();
}

void MixCanvas::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_1: setCurveMode(VolumeCurve); break;
    case Qt::Key_2: setCurveMode(BassCurve); break;
    case Qt::Key_3: setCurveMode(HighCurve); break;
    case Qt::Key_Delete: removeSelectedPoint(); break;
    case Qt::Key_F: fitPeriod(); break;
    case Qt::Key_BracketRight: fitNextPeriod(1); break;
    case Qt::Key_BracketLeft: fitNextPeriod(-1); break;
    default: QWidget::keyPressEvent(e); return;
    }
    e->accept();
}

void MixCanvas::contextMenuEvent(QContextMenuEvent* e)
{
    const int lane = trackAt(e->pos().y());
    if (lane < 0 || !mix_ || !editor_) return;
    const int pair = pairAtX(lane, e->pos().x());
    QMenu menu(this);
    const int dot = findPoint(lane, e->pos());
    QAction* del = nullptr;
    if (dot >= 0) {
        static const char* const names[] = {"volume", "bass", "high"};
        del = menu.addAction(QStringLiteral("Delete %1 point").arg(QLatin1String(names[curveMode_])));
        menu.addSeparator();
    }
    if (pair >= 0) {
        for (int t = 0; t < trakcrate::kMixTypeCount; ++t) {
            QAction* a = menu.addAction(QString::fromLatin1(trakcrate::mixTypeName(trakcrate::MixType(t))));
            a->setCheckable(true);
            a->setChecked(mix_->tracks[size_t(pair)].next.type == trakcrate::MixType(t));
            a->setData(t);
        }
        menu.addSeparator();
    }
    QAction* remove = menu.addAction(QStringLiteral("Remove track %1 from the mix").arg(lane + 1));
    remove->setToolTip(QStringLiteral("The track stays in the playlist"));
    remove->setEnabled(mix_->tracks.size() > 1);
    remove->setData(-1);
    if (QAction* chosen = menu.exec(e->globalPos())) {
        if (chosen == del) {
            selPoint_ = PointRef{lane, curveMode_, dot};
            removeSelectedPoint();
        } else if (chosen == remove) emit removeTrackRequested(lane);
        else emit mixTypeChosen(pair, chosen->data().toInt());
    }
}

void MixCanvas::wheelEvent(QWheelEvent* e)
{
    const int delta = e->angleDelta().y() ? e->angleDelta().y() : e->angleDelta().x();
    if (e->modifiers() & Qt::ControlModifier) {
        zoomAt(msAt(int(e->position().x())), delta > 0 ? 0.8 : 1.25);
    } else if (e->modifiers() & Qt::ShiftModifier) {
        setView(viewStart_ - span_ * delta / 1200.0, span_);
    } else {
        vbar_->setValue(vbar_->value() - delta / 2);
    }
    e->accept();
}

void MixCanvas::paintRuler(QPainter& p)
{
    const int W = contentWidth();
    p.fillRect(0, 0, W, kRulerHeight, QColor(40, 40, 46));
    p.setPen(QColor(200, 200, 200));
    const double step = tickStepMs(span_ / W);
    for (double t = std::ceil(viewStart_ / step) * step; t <= viewStart_ + span_; t += step) {
        const int x = xAt(t);
        p.drawLine(x, kRulerHeight - 8, x, kRulerHeight - 1);
        p.drawText(x + 3, kRulerHeight - 9, formatTime(t));
    }
}

void MixCanvas::paintPeriods(QPainter& p, int lane)
{
    if (!mix_) return;
    const QRect r = laneRect(lane);
    for (int pair : {lane, lane - 1}) {
        if (pair < 0 || pair + 1 >= laneCount()) continue;
        const bool outgoing = pair == lane;
        const int a = periodStartX(pair), b = periodEndX(pair);
        QColor c = outgoing ? QColor(255, 150, 40) : QColor(60, 200, 255);
        c.setAlpha(51);                                                          // 20 %
        p.fillRect(QRect(a, r.top(), b - a, r.height()), c);
        c.setAlpha(200);
        p.setPen(QPen(c, 1, Qt::DashLine));
        p.drawLine(a, r.top(), a, r.bottom());
        p.drawLine(b, r.top(), b, r.bottom());
        if (!outgoing) {
            const trakcrate::MixPair& mp = mix_->tracks[size_t(pair)].next;
            QString t = QStringLiteral("%1, %2 bars").arg(QString::fromLatin1(trakcrate::mixTypeName(mp.type))).arg(mp.lenBeats / 4.0, 0, 'f', 1);
            if (trakcrate::pairTempoDiffers(eff_, pair)) t += QStringLiteral("  (tempos differ by more than 2%)");
            p.setPen(QColor(235, 235, 235));
            p.drawText(QRect(a + 3, r.bottom() - 18, std::max(0, b - a - 4), 16), Qt::AlignLeft | Qt::AlignVCenter, t);
        }
    }
}

void MixCanvas::paintCurves(QPainter& p, int lane)
{
    if (!mix_) return;
    const trakcrate::EffectiveTrack& e = eff_[size_t(lane)];
    const int W = contentWidth();
    for (int pass = 0; pass < 2; ++pass) {                                       // the faint curves first, the edited one on top
        for (int curve = 0; curve < 3; ++curve) {
            if ((curve == curveMode_) != (pass == 1)) continue;
            const trakcrate::Curve& c = curveOf(lane, curve);
            if (c.points.empty() && curve != curveMode_) continue;
            QColor col = curveColor(curve);
            if (curve != curveMode_) col.setAlpha(90);
            p.setPen(QPen(col, curve == curveMode_ ? 2 : 1));
            const double endMs = starts_[size_t(lane)] + double(e.editedFrames) * 1000.0 / trakcrate::kEditRate;
            int px = std::max(0, xAt(starts_[size_t(lane)])), py = yOfValue(lane, c.points.empty() ? 100 : c.points.front().value);
            for (const trakcrate::CurvePoint& pt : c.points) {
                const int x = xAt(starts_[size_t(lane)] + trakcrate::trackBeatMs(e, pt.beat)), y = yOfValue(lane, pt.value);
                p.drawLine(px, py, x, y);
                px = x;
                py = y;
            }
            p.drawLine(px, py, std::min(W, xAt(endMs)), py);
            if (curve == curveMode_) {
                p.setBrush(col);
                for (int i = 0; i < int(c.points.size()); ++i) {
                    const QPoint q = pointPos(lane, curve, i);
                    const bool sel = selPoint_.lane == lane && selPoint_.curve == curve && selPoint_.index == i;
                    p.setPen(sel ? QPen(Qt::white, 2) : QPen(Qt::black, 1));
                    p.drawEllipse(q, sel ? 5 : 4, sel ? 5 : 4);
                }
                p.setBrush(Qt::NoBrush);
            }
        }
    }
}

void MixCanvas::paintLane(QPainter& p, int lane)
{
    const QRect r = laneRect(lane);
    const int W = contentWidth();
    p.fillRect(r, lane == selected_ ? kLaneBgSelected : kLaneBg);
    const trakcrate::EffectiveTrack& e = eff_[size_t(lane)];
    const double startMs = starts_[size_t(lane)];
    const double endMs = startMs + double(e.editedFrames) * 1000.0 / trakcrate::kEditRate;
    const int xa = std::max(0, xAt(startMs)), xb = std::min(W, xAt(endMs));
    const int mid = r.top() + r.height() / 2;
    const QVector<quint16>& cols = edited_[size_t(lane)];
    if (xb > xa) {
        if (!cols.isEmpty()) {
            for (int x = xa; x < xb; ++x) {
                const double tA = msAt(x) - startMs, tB = msAt(x + 1) - startMs;
                int a = int(tA * 150.0 / 1000.0), b = std::max(a + 1, int(tB * 150.0 / 1000.0));
                if (a < 0 || a >= cols.size()) continue;
                b = std::min(b, int(cols.size()));
                quint16 best = cols[a];
                int bestH = (best >> 2) & 31;
                for (int i = a + 1; i < b; ++i) {
                    const int h = (cols[i] >> 2) & 31;
                    if (h > bestH) { bestH = h; best = cols[i]; }
                }
                if (bestH == 0) continue;
                const int half = bestH * (r.height() / 2 - 2) / 31;
                p.setPen(QColor(((best >> 13) & 7) * 255 / 7, ((best >> 10) & 7) * 255 / 7, ((best >> 7) & 7) * 255 / 7));
                p.drawLine(x, mid - half, x, mid + half);
            }
        } else {
            p.fillRect(QRect(xa, r.top(), xb - xa, r.height()), QColor(70, 70, 80));
        }
        // beat grid of the effective (nudged, stretched, edited) grid: bar lines red and full height, beat lines white on the lower half
        if (e.bpm > 0) {
            const double beatMs = 60000.0 / e.bpm;
            const double pxPerBeat = beatMs * W / span_;
            if (pxPerBeat * 4 >= 3) {
                const long long k0 = (long long)std::ceil(trakcrate::trackMsBeat(e, std::max(viewStart_, startMs) - startMs));
                const long long k1 = (long long)std::floor(trakcrate::trackMsBeat(e, std::min(viewStart_ + span_, endMs) - startMs));
                for (long long k = std::max(0LL, k0); k <= k1; ++k) {
                    const bool bar = k % 4 == 0;
                    if (!bar && pxPerBeat < 3) continue;
                    const int x = xAt(startMs + trakcrate::trackBeatMs(e, double(k)));
                    if (x < xa || x > xb) continue;
                    if (bar) p.fillRect(QRect(x, r.top(), 2, r.height()), kBarColor);
                    else p.fillRect(QRect(x, mid, 1, r.bottom() - mid + 1), kBeatColor);
                }
            }
        }
        p.setPen(QColor(90, 90, 100));
        p.drawRect(QRect(xa, r.top(), std::max(0, xb - xa - 1), r.height() - 1));
    }
    if (rangeLane_ == lane) {
        const int ra = xAt(startMs + rangeA_), rb = xAt(startMs + rangeB_);
        p.fillRect(QRect(ra, r.top(), std::max(1, rb - ra), r.height()), QColor(80, 140, 255, 70));
    }
    // seams of cuts and loops: a small orange tick at the bottom of the lane
    for (double jm : trakcrate::joinOutputMs(e.plan)) {
        const int x = xAt(startMs + jm);
        if (x >= xa && x <= xb) p.fillRect(QRect(x - 1, r.bottom() - 7, 3, 7), QColor(255, 140, 0));
    }
    paintPeriods(p, lane);
    paintCurves(p, lane);
    // label at the left edge of the visible part of the lane
    QString label = QStringLiteral("%1. %2").arg(lane + 1).arg(lane < int(info_.size()) ? info_[size_t(lane)].title : QString());
    QColor labelColor(235, 235, 235);
    if (mix_ && e.bpm > 0 && lane < int(mix_->tracks.size())) {
        const double own = facts_[size_t(lane)].grid.bpm;
        if (mix_->tracks[size_t(lane)].stretchOff) {
            label += QStringLiteral("  %1 bpm (not stretched)").arg(own, 0, 'f', 1);
        } else {
            label += QStringLiteral("  %1 <- %2, %3%4%").arg(mix_->masterBpm, 0, 'f', 1).arg(own, 0, 'f', 1).arg(QString(e.ratio >= 1 ? "+" : "")).arg((e.ratio - 1) * 100, 0, 'f', 1);
            if (trakcrate::stretchImpossible(e.ratio)) labelColor = QColor(255, 80, 80);
            else if (!trakcrate::stretchWarning(e.ratio).empty()) labelColor = QColor(255, 170, 40);
        }
    }
    if (lane < int(stretching_.size()) && stretching_[size_t(lane)]) label += QStringLiteral("  stretching...");
    if (!e.error.empty()) { label += QStringLiteral("  [%1]").arg(QString::fromStdString(e.error)); labelColor = QColor(255, 80, 80); }
    if (mix_ && lane < int(mix_->tracks.size()) && mix_->tracks[size_t(lane)].missing) { label += QStringLiteral("  MISSING"); labelColor = QColor(255, 80, 80); }
    const int lx = std::max(4, xa + 4);
    QFontMetrics fm(p.font());
    const QRect lr(lx - 2, r.top() + 2, std::min(fm.horizontalAdvance(label) + 6, W - lx), fm.height() + 2);
    p.fillRect(lr, QColor(0, 0, 0, 150));
    p.setPen(labelColor);
    p.drawText(lr.adjusted(2, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
}

void MixCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(14, 14, 16));
    p.setClipRect(0, 0, contentWidth(), contentHeight());
    for (int i = 0; i < int(eff_.size()); ++i) {
        const QRect r = laneRect(i);
        if (r.bottom() < kRulerHeight || r.top() > contentHeight()) continue;       // only what is visible
        paintLane(p, i);
    }
    paintRuler(p);
    const int x = xAt(playMs_);
    if (x >= 0 && x < contentWidth()) {
        p.fillRect(QRect(x - 1, 0, 3, contentHeight()), QColor(0, 0, 0, 110));
        p.setPen(QPen(kPlayColor, 1));
        p.drawLine(x, 0, x, contentHeight());
    }
}

} // namespace vdj
