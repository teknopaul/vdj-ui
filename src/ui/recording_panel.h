#ifndef RECORDING_PANEL_H
#define RECORDING_PANEL_H

#include <QWidget>

class QComboBox;
class QLabel;
class QCheckBox;
class QPushButton;

namespace vdj {

class AudioRecorder;
class LevelMeterWidget;

class RecordingPanel : public QWidget {
    Q_OBJECT
public:
    explicit RecordingPanel(AudioRecorder* recorder, QWidget* parent = nullptr);

    // Return an auto-generated filename: session_YYYYMMDD_HHmm_<djName>.mp3
    static QString makeFilename(const QString& djName = QString());

private slots:
    void onRecordStop();
    void onSplit();
    void onLevels(float peakL, float peakR, float rmsL, float rmsR);
    void restartMonitor();
    void onMonitorFailed(const QString& msg);
    void onRecordingStarted(const QString& path);
    void onRecordingStopped(const QString& path);
    void onError(const QString& msg);

protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    AudioRecorder* recorder_;
    QComboBox*     deviceCombo_;
    QPushButton*   recordBtn_;
    QPushButton*   splitBtn_;
    QLabel*        fileLabel_;
    LevelMeterWidget* meter_;
    QCheckBox*     monitorBox_;
    QLabel*        adviceLabel_;
};

} // namespace vdj
#endif
