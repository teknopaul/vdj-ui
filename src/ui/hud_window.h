#ifndef HUD_WINDOW_H
#define HUD_WINDOW_H

#include <QWidget>
#include <QHash>

class QLabel;
class QTimer;

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

    // Called by MainWindow when a track is selected in the browser.
    void setTrackInfo(int pid, const QString& title, const QString& artist);
    void setArtwork(int pid, const QPixmap& pixmap);

protected:
    void closeEvent(QCloseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private slots:
    void onStatusUpdated(int pid, double bpm, int pitch, int flags,
                         int masterState, int active);
    void onBeatReceived(int pid, double bpm, int barPos);

private:
    struct DeckPanel {
        int        pid       = 0;
        QLabel*    titleLabel = nullptr;
        QLabel*    artistLabel = nullptr;
        QLabel*    bpmLabel   = nullptr;
        QLabel*    syncDot    = nullptr;
        QLabel*    artLabel   = nullptr;
    };

    void setupUi();
    void restorePosition();
    void savePosition();
    void updateDeck(const DeckPanel& p, double bpm, int masterState, int active);

    NetworkManager* network_;
    ConfigManager*  config_;
    SpectrumWidget* spectrum_;
    QLabel*         masterBpmLabel_;

    DeckPanel       decks_[2];    // slots for first two known player IDs
    QHash<int, int> pidToSlot_;   // player ID → deck slot (0 or 1)
    int             nextSlot_     = 0;
    int             masterPid_    = -1;
};

} // namespace vdj

#endif // HUD_WINDOW_H
