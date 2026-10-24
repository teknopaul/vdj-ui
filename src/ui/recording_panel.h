#ifndef RECORDING_PANEL_H
#define RECORDING_PANEL_H

#include <QWidget>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;

namespace vdj {

class AudioRecorder;

class RecordingPanel : public QWidget {
    Q_OBJECT
public:
    explicit RecordingPanel(AudioRecorder* recorder, QWidget* parent = nullptr);

    // Return an auto-generated filename: session_YYYYMMDD_HHmm_<djName>.mp3
    static QString makeFilename(const QString& djName = QString());

private slots:
    void onRecordStop();
    void onSplit();
    void onLevelChanged(float rms);
    void onRecordingStarted(const QString& path);
    void onRecordingStopped(const QString& path);
    void onError(const QString& msg);

private:
    AudioRecorder* recorder_;
    QComboBox*     deviceCombo_;
    QPushButton*   recordBtn_;
    QPushButton*   splitBtn_;
    QLabel*        fileLabel_;
    QProgressBar*  vuMeter_;
};

} // namespace vdj
#endif
