#include "track_details.h"
#include "waveform_widget.h"
#include "waveform_window.h"
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QScrollArea>
#include <QVBoxLayout>

namespace vdj {

static const int kArtSize = 160;

TrackDetails::TrackDetails(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    auto* head = new QHBoxLayout;
    title_ = new QLabel(this);
    title_->setWordWrap(true);
    title_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont f = title_->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.2);
    title_->setFont(f);
    head->addWidget(title_, 1, Qt::AlignTop);

    artwork_ = new QLabel(this);
    artwork_->setFixedSize(kArtSize, kArtSize);
    artwork_->setAlignment(Qt::AlignCenter);
    artwork_->setFrameShape(QFrame::StyledPanel);
    head->addWidget(artwork_, 0, Qt::AlignTop | Qt::AlignRight);
    outer->addLayout(head);

    waveform_ = new WaveformWidget(this);
    waveform_->setToolTip(QStringLiteral("Double click to open in a window you can enlarge, zoom and scroll"));
    outer->addWidget(waveform_);
    connect(waveform_, &WaveformWidget::activated, this, &TrackDetails::openWaveformWindow);
    auto* open = new QPushButton(QStringLiteral("Open waveform in window..."), this);
    open->setToolTip(QStringLiteral("A larger window to zoom and scroll through the beat grid and cue points"));
    connect(open, &QPushButton::clicked, this, &TrackDetails::openWaveformWindow);
    outer->addWidget(open);

    form_ = new QWidget;
    formLayout_ = new QFormLayout(form_);
    formLayout_->setLabelAlignment(Qt::AlignRight);
    formLayout_->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(form_);
    outer->addWidget(scroll, 1);

    clear();
}

void TrackDetails::openWaveformWindow()
{
    if (!window_)
        window_ = new WaveformWindow(this);
    window_->setTrack(info_.title, analysis_);
    window_->show();
    window_->raise();
    window_->activateWindow();
}

void TrackDetails::clear()
{
    setTrack(TrackInfo());
    title_->setText(QStringLiteral("No track selected"));
}

void TrackDetails::setTrack(const TrackInfo& info)
{
    info_ = info;
    title_->setText(info.title);

    QPixmap pm;
    hasArtwork_ = !info.artworkPath.isEmpty() && pm.load(info.artworkPath);
    if (hasArtwork_) {
        artwork_->setPixmap(pm.scaled(kArtSize, kArtSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        artwork_->setPixmap(QPixmap());
        artwork_->setText(QStringLiteral("no artwork"));
    }

    // analysis files (beat grid, cues, waveforms): read for the selected track only
    if (info.ensureAnalysis && !info.analyzePath.isEmpty())
        info.ensureAnalysis(info.analyzePath);   // from the deck, once
    analysis_ = info.analyzePath.isEmpty() ? TrackAnalysis() : loadTrackAnalysis(info.root, info.analyzePath);
    waveform_->setAnalysis(analysis_);
    if (window_ && window_->isVisible())         // an open window follows the selection
        window_->setTrack(info.title, analysis_);

    while (formLayout_->rowCount() > 0)
        formLayout_->removeRow(0);
    for (const auto& kv : info.fields) {
        if (kv.second.isEmpty()) continue;
        auto* value = new QLabel(kv.second);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        formLayout_->addRow(kv.first + ":", value);
    }

    if (analysis_.valid) {
        auto addRow = [this](const QString& label, const QString& text) {
            auto* value = new QLabel(text);
            value->setWordWrap(true);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            formLayout_->addRow(label + ":", value);
        };
        auto mmss = [](quint32 ms) {
            return QStringLiteral("%1:%2.%3").arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QChar('0')).arg(ms % 1000, 3, 10, QChar('0'));
        };
        if (analysis_.beatCount > 0)
            addRow(QStringLiteral("Beat grid"), QStringLiteral("%1 beats, %2 BPM, first beat at %3")
                       .arg(analysis_.beatCount).arg(analysis_.avgBpm, 0, 'f', 2).arg(mmss(analysis_.firstBeatMs)));
        else
            addRow(QStringLiteral("Beat grid"), QStringLiteral("not analysed"));
        QStringList lines;
        for (const AnalysisCue& c : analysis_.cues) {
            QString line = QStringLiteral("%1  %2").arg(c.hotCue ? c.letter() : QStringLiteral("mem"), 3).arg(mmss(c.timeMs));
            if (c.loop)
                line += QStringLiteral("  loop %1 beats to %2").arg(c.loopBeats(), 0, 'g', 4).arg(mmss(c.loopEndMs));
            lines << line;
        }
        addRow(QStringLiteral("Cues"), lines.isEmpty() ? QStringLiteral("none") : lines.join('\n'));
    }
}

} // namespace vdj
