#include "hud_window.h"
#include "network/network_manager.h"
#include "config/config_manager.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMoveEvent>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <cstdlib>

namespace vdj {

/* ---- SpectrumWidget ---- */

class SpectrumWidget : public QWidget {
public:
    explicit SpectrumWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(80);
        std::fill(bars_, bars_ + kBars, 0.0f);
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, [this]() {
            bool any = false;
            for (int i = 0; i < kBars; i++) {
                bars_[i] *= 0.82f;
                if (bars_[i] > 0.01f) any = true;
            }
            update();
            if (!any) timer_->stop();
        });
    }

    void pulse(double /*bpm*/, int barPos) {
        float base = (barPos == 0) ? 1.0f : 0.55f - barPos * 0.1f;
        for (int i = 0; i < kBars; i++) {
            float scale = 1.0f - (float)i / kBars * 0.5f;
            float rand01 = (float)(std::rand() % 1000) / 1000.0f;
            bars_[i] = base * scale * (0.6f + 0.4f * rand01);
        }
        timer_->start(33); // ~30fps
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x0a, 0x0a, 0x0a));
        int W = width(), H = height();
        int barW = W / kBars - 1;
        if (barW < 1) barW = 1;
        for (int i = 0; i < kBars; i++) {
            int h = (int)(bars_[i] * H);
            if (h < 1) continue;
            int x = i * (barW + 1);
            // Gradient: green → yellow → red
            float v = bars_[i];
            int r = (int)(v > 0.5f ? 255 : v * 2 * 255);
            int g = (int)(v < 0.5f ? 255 : (1.0f - v) * 2 * 255);
            p.fillRect(x, H - h, barW, h, QColor(r, g, 0));
        }
    }

private:
    static const int kBars = 32;
    float  bars_[kBars];
    QTimer* timer_;
};

/* ---- HudWindow ---- */

static QLabel* bigLabel(const QString& text, int ptSize, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    QFont f = l->font();
    f.setPointSize(ptSize);
    f.setBold(true);
    l->setFont(f);
    l->setAlignment(Qt::AlignCenter);
    l->setStyleSheet(QStringLiteral("color: #e8e8e8;"));
    return l;
}

static void styleSyncDot(QLabel* dot, int masterState, int active)
{
    if (!active) {
        dot->setStyleSheet(QStringLiteral("background:#333; border-radius:8px;"));
        dot->setToolTip(QStringLiteral("Inactive"));
        return;
    }
    switch (masterState) {
    case 2:
        dot->setStyleSheet(QStringLiteral("background:#00cc44; border-radius:8px;"));
        dot->setToolTip(QStringLiteral("Master"));
        break;
    case 1:
        dot->setStyleSheet(QStringLiteral("background:#cccc00; border-radius:8px;"));
        dot->setToolTip(QStringLiteral("Synced"));
        break;
    default:
        dot->setStyleSheet(QStringLiteral("background:#555; border-radius:8px;"));
        dot->setToolTip(QStringLiteral("Free"));
        break;
    }
}

HudWindow::HudWindow(NetworkManager* network, ConfigManager* config, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , network_(network)
    , config_(config)
{
    setWindowTitle(QStringLiteral("VDJ HUD"));
    setStyleSheet(QStringLiteral("background: #0e0e0e;"));
    resize(900, 300);

    setupUi();
    restorePosition();

    connect(network_, &NetworkManager::statusUpdated,
            this,     &HudWindow::onStatusUpdated);
    connect(network_, &NetworkManager::beatReceived,
            this,     &HudWindow::onBeatReceived);
}

HudWindow::~HudWindow() = default;

void HudWindow::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(4);

    auto* topRow = new QHBoxLayout;

    // Helper: build one deck panel
    auto buildDeck = [&](DeckPanel& dp) -> QWidget* {
        auto* w = new QWidget(this);
        w->setStyleSheet(QStringLiteral("background:#161616; border-radius:4px;"));
        auto* vl = new QVBoxLayout(w);
        vl->setContentsMargins(8, 6, 8, 6);
        vl->setSpacing(2);

        dp.syncDot = new QLabel(w);
        dp.syncDot->setFixedSize(16, 16);
        styleSyncDot(dp.syncDot, 0, 0);

        dp.artLabel = new QLabel(w);
        dp.artLabel->setFixedSize(80, 80);
        dp.artLabel->setStyleSheet(QStringLiteral("background:#222;"));
        dp.artLabel->setAlignment(Qt::AlignCenter);

        dp.titleLabel  = bigLabel(QStringLiteral("—"), 14, w);
        dp.titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        dp.artistLabel = bigLabel(QStringLiteral("—"), 11, w);
        dp.artistLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        dp.bpmLabel    = bigLabel(QStringLiteral("— BPM"), 10, w);
        dp.bpmLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        auto* topLine = new QHBoxLayout;
        topLine->addWidget(dp.syncDot);
        topLine->addWidget(dp.artLabel);
        topLine->addStretch();
        vl->addLayout(topLine);
        vl->addWidget(dp.titleLabel);
        vl->addWidget(dp.artistLabel);
        vl->addWidget(dp.bpmLabel);
        return w;
    };

    topRow->addWidget(buildDeck(decks_[0]), 1);

    // Center BPM panel
    auto* centerW = new QWidget(this);
    auto* centerV = new QVBoxLayout(centerW);
    masterBpmLabel_ = bigLabel(QStringLiteral("—"), 48, centerW);
    auto* bpmCaption = new QLabel(QStringLiteral("BPM"), centerW);
    bpmCaption->setAlignment(Qt::AlignCenter);
    bpmCaption->setStyleSheet(QStringLiteral("color:#888; font-size:11pt;"));
    centerV->addStretch();
    centerV->addWidget(masterBpmLabel_);
    centerV->addWidget(bpmCaption);
    centerV->addStretch();
    topRow->addWidget(centerW, 0);

    topRow->addWidget(buildDeck(decks_[1]), 1);

    spectrum_ = new SpectrumWidget(this);

    mainLayout->addLayout(topRow, 1);
    mainLayout->addWidget(spectrum_);

    auto* note = new QLabel(QStringLiteral("Spectrum: synthetic beat-phase display, not audio-derived"), this);
    note->setStyleSheet(QStringLiteral("color:#444; font-size:8pt;"));
    note->setAlignment(Qt::AlignRight);
    mainLayout->addWidget(note);
}

void HudWindow::restorePosition()
{
    QString geom = config_->loadValue(QStringLiteral("hud"), QStringLiteral("geometry"));
    if (!geom.isEmpty()) {
        QStringList parts = geom.split(QLatin1Char(','));
        if (parts.size() == 4) {
            move(parts[0].toInt(), parts[1].toInt());
            resize(parts[2].toInt(), parts[3].toInt());
        }
    }
}

void HudWindow::savePosition()
{
    config_->saveValue(QStringLiteral("hud"), QStringLiteral("geometry"),
        QStringLiteral("%1,%2,%3,%4")
        .arg(x()).arg(y()).arg(width()).arg(height()));
}

void HudWindow::closeEvent(QCloseEvent* event)
{
    savePosition();
    event->accept();
}

void HudWindow::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    savePosition();
}

void HudWindow::setTrackInfo(int pid, const QString& title, const QString& artist)
{
    auto it = pidToSlot_.find(pid);
    if (it == pidToSlot_.end()) return;
    DeckPanel& dp = decks_[it.value()];
    dp.titleLabel->setText(title.isEmpty() ? QStringLiteral("—") : title);
    dp.artistLabel->setText(artist);
}

void HudWindow::setArtwork(int pid, const QPixmap& pixmap)
{
    auto it = pidToSlot_.find(pid);
    if (it == pidToSlot_.end()) return;
    DeckPanel& dp = decks_[it.value()];
    if (pixmap.isNull())
        dp.artLabel->clear();
    else
        dp.artLabel->setPixmap(pixmap.scaled(80, 80, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation));
}

void HudWindow::onStatusUpdated(int pid, double bpm, int /*pitch*/,
                                int /*flags*/, int masterState, int active)
{
    // Assign new PIDs to slots
    if (!pidToSlot_.contains(pid) && nextSlot_ < 2) {
        pidToSlot_.insert(pid, nextSlot_);
        decks_[nextSlot_].pid = pid;
        ++nextSlot_;
    }

    auto it = pidToSlot_.find(pid);
    if (it == pidToSlot_.end()) return;
    DeckPanel& dp = decks_[it.value()];

    styleSyncDot(dp.syncDot, masterState, active);
    dp.bpmLabel->setText(bpm > 0 ? QStringLiteral("%1 BPM").arg(bpm, 0, 'f', 1)
                                 : QStringLiteral("— BPM"));

    if (masterState == 2) {
        masterPid_ = pid;
        masterBpmLabel_->setText(bpm > 0 ? QString::number(bpm, 'f', 1)
                                         : QStringLiteral("—"));
    }
}

void HudWindow::onBeatReceived(int /*pid*/, double bpm, int barPos)
{
    spectrum_->pulse(bpm, barPos);
}

} // namespace vdj
