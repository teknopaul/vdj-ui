#ifndef HUD_WINDOW_H
#define HUD_WINDOW_H

#include <QWidget>

namespace vdj {

class NetworkManager;
class ConfigManager;
class SpectrumWidget;

class HudWindow : public QWidget {
    Q_OBJECT

public:
    explicit HudWindow(NetworkManager* network,
                       ConfigManager*  config,
                       QWidget* parent = nullptr);
    ~HudWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private slots:
    void onBeatReceived(int pid, double bpm, int barPos);

private:
    void setupUi();
    void restorePosition();
    void savePosition();

    NetworkManager* network_;
    ConfigManager*  config_;
    SpectrumWidget* spectrum_;
};

} // namespace vdj

#endif // HUD_WINDOW_H
