#include "hud_window.h"
#include "network/network_manager.h"
#include "config/config_manager.h"

#include <QCloseEvent>
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

    connect(network_, &NetworkManager::beatReceived,
            this,     &HudWindow::onBeatReceived);
}

HudWindow::~HudWindow() = default;

void HudWindow::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(4);

    spectrum_ = new SpectrumWidget(this);
    mainLayout->addWidget(spectrum_, 1);

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

void HudWindow::onBeatReceived(int /*pid*/, double bpm, int barPos)
{
    spectrum_->pulse(bpm, barPos);
}

} // namespace vdj
