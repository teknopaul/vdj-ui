#include "waveform_window.h"
#include "waveform_widget.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>
#include <cmath>

namespace vdj {

WaveformWindow::WaveformWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    resize(1100, 380);

    auto* layout = new QVBoxLayout(this);

    auto* bar = new QHBoxLayout;
    auto* fit = new QPushButton(QStringLiteral("Fit"), this);
    auto* out = new QPushButton(QStringLiteral("−"), this);
    auto* in = new QPushButton(QStringLiteral("+"), this);
    fit->setToolTip(QStringLiteral("Show the whole track (Home, or double click)"));
    out->setToolTip(QStringLiteral("Zoom out (-)"));
    in->setToolTip(QStringLiteral("Zoom in (+)"));
    for (auto* b : { fit, out, in })
        b->setFocusPolicy(Qt::NoFocus);
    view_ = new QLabel(this);
    bar->addWidget(fit);
    bar->addWidget(out);
    bar->addWidget(in);
    bar->addWidget(view_, 1);
    auto* help = new QLabel(QStringLiteral("wheel: zoom   drag: scroll   double click: fit"), this);
    help->setEnabled(false);
    bar->addWidget(help);
    layout->addLayout(bar);

    waveform_ = new WaveformWidget(this);
    waveform_->setInteractive(true);
    waveform_->setMinimumHeight(160);
    layout->addWidget(waveform_, 1);

    scroll_ = new QScrollBar(Qt::Horizontal, this);
    layout->addWidget(scroll_);

    position_ = new QLabel(this);
    position_->setMinimumHeight(position_->fontMetrics().height() + 4);
    layout->addWidget(position_);

    connect(fit, &QPushButton::clicked, waveform_, &WaveformWidget::fit);
    connect(out, &QPushButton::clicked, this, [this]() { waveform_->zoomAt(waveform_->width() / 2, 1.0 / 1.5); });
    connect(in, &QPushButton::clicked, this, [this]() { waveform_->zoomAt(waveform_->width() / 2, 1.5); });
    connect(waveform_, &WaveformWidget::viewChanged, this, &WaveformWindow::syncControls);
    connect(waveform_, &WaveformWidget::hovered, position_, &QLabel::setText);
    connect(scroll_, &QScrollBar::valueChanged, this, [this](int v) {
        if (!syncing_)
            waveform_->setView(v, waveform_->viewSpan() > 0 ? waveform_->viewSpan() : waveform_->analysis().lengthMs());
    });
    syncControls();
}

void WaveformWindow::setTrack(const QString& title, const TrackAnalysis& analysis)
{
    setWindowTitle(title.isEmpty() ? QStringLiteral("Waveform") : QStringLiteral("Waveform - %1").arg(title));
    waveform_->setAnalysis(analysis);
    position_->clear();
}

void WaveformWindow::syncControls()
{
    const double len = waveform_->analysis().lengthMs();
    const double span = waveform_->viewSpan() > 0 ? waveform_->viewSpan() : len;
    const double start = waveform_->viewSpan() > 0 ? waveform_->viewStart() : 0.0;
    syncing_ = true;
    scroll_->setRange(0, int(std::max(0.0, std::floor(len - span))));
    scroll_->setPageStep(int(std::max(1.0, span)));
    scroll_->setSingleStep(int(std::max(1.0, span / 10)));
    scroll_->setValue(int(std::lround(start)));
    scroll_->setEnabled(len > span);
    syncing_ = false;

    if (len <= 0) {
        view_->clear();
        return;
    }
    auto mmss = [](double ms) {
        const qint64 t = qint64(std::llround(ms));
        return QStringLiteral("%1:%2.%3").arg(t / 60000).arg(t / 1000 % 60, 2, 10, QChar('0')).arg(t % 1000, 3, 10, QChar('0'));
    };
    view_->setText(QStringLiteral("%1 – %2   (%3 s, zoom %4x)")
                       .arg(mmss(start)).arg(mmss(start + span)).arg(span / 1000.0, 0, 'f', 3).arg(waveform_->zoom(), 0, 'f', 1));
}

} // namespace vdj
