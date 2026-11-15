#ifndef MIX_STRETCH_DOCK_H
#define MIX_STRETCH_DOCK_H

#include "trakcrate/mix_model.h"
#include "trakcrate/stretch_cache.h"

#include <QDialog>
#include <QObject>
#include <QWidget>

#include <atomic>

class QCheckBox;
class QFormLayout;
class QHBoxLayout;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QThread;
class QTimer;

namespace vdj {

class AudioPlayer;

// Fills the StretchCache in the background after the master tempo or an engine changed: one worker, lane by lane, cancellable (a newer request cancels
// the older one). Cached stretches are free.
class StretchJobQueue : public QObject {
    Q_OBJECT

public:
    explicit StretchJobQueue(trakcrate::StretchCache* cache, QObject* parent = nullptr);
    ~StretchJobQueue() override;

    void request(const trakcrate::Mix& mix, const std::vector<trakcrate::TrackFacts>& facts);
    void cancel();                       // also waits for the worker to stop
    bool busy() const { return worker_ != nullptr; }
    void waitIdle();                     // for tests

signals:
    void laneStretching(int lane, bool on);
    void laneProgress(int lane, double fraction);
    void allDone();

private:
    trakcrate::StretchCache* cache_;
    QThread* worker_ = nullptr;
    std::atomic<bool> cancel_{false};
    int generation_ = 0;
};

// The widgets of a stretch engine's options, built from its schema.
class StretchOptionsForm : public QWidget {
    Q_OBJECT

public:
    explicit StretchOptionsForm(QWidget* parent = nullptr);
    void setEngine(const QString& engineId, const trakcrate::StretchOptions& values);
    trakcrate::StretchOptions values() const;

signals:
    void changed();

private:
    QString engine_;
    QFormLayout* layout_;
    std::vector<std::pair<std::string, QWidget*>> fields_;
};

// The "Stretch" dock: the mix default engine and its options, the override of the selected track, its ratio and warning, Re-stretch and Audition.
class MixStretchDock : public QWidget {
    Q_OBJECT

public:
    explicit MixStretchDock(QWidget* parent = nullptr);

    // Shows the settings of the mix; `lane` is the selected track (-1 none).
    void setMix(const trakcrate::Mix& mix, const std::vector<trakcrate::EffectiveTrack>& eff, int lane);
    QComboBox* defaultCombo() const { return default_; }
    QComboBox* trackCombo() const { return track_; }

signals:
    void defaultChosen(const trakcrate::StretchChoice& choice);
    void trackChosen(int lane, bool stretchOff, const trakcrate::StretchChoice& choice);      // an empty engine = the mix default
    void restretchRequested();
    void auditionRequested();

private:
    void emitDefault();
    void emitTrack();

    QComboBox* default_;
    StretchOptionsForm* defaultForm_;
    QComboBox* track_;
    StretchOptionsForm* trackForm_;
    QLabel* ratio_;
    QPushButton* restretch_;
    QPushButton* audition_;
    int lane_ = -1;
    bool filling_ = false;
};

// Audition: the excerpt around a mix period stretched by the engines the user ticks, played one after the other or A/B, optionally blind.
class MixAuditionDialog : public QDialog {
    Q_OBJECT

public:
    // excerpt: the original audio (44.1 kHz) of the part to listen to; ratio: the stretch of the track in the mix.
    MixAuditionDialog(const trakcrate::DecodedPcm& excerpt, double ratio, QWidget* parent = nullptr);
    ~MixAuditionDialog() override;

private:
    void renderTicked();
    void playIndex(int i);
    void stop();

    trakcrate::DecodedPcm excerpt_;
    double ratio_;
    QListWidget* engines_;
    QCheckBox* blind_;
    QLabel* status_;
    QWidget* buttonsHost_ = nullptr;
    QHBoxLayout* buttons_ = nullptr;
    QPushButton* reveal_;
    AudioPlayer* player_;
    struct Item {
        std::string id, name, path;
        QString label;
    };
    std::vector<Item> items_;
    QString dir_;
    QTimer* poll_ = nullptr;
    int seq_ = -1;                    // "play all in turn": the excerpt being played, -1 when not running
};

} // namespace vdj

#endif
