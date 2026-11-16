#ifndef RECORDING_BAR_H
#define RECORDING_BAR_H

#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QTimer;
class QToolButton;

namespace vdj {

class AudioRecorder;
class ConfigManager;
class LevelMeterWidget;

// The recording UI as one line across the bottom of the window:
//   Recording ⚫ | [Capture v] | L,R meters ----- | Monitor [x] | Compressor [x] | [folder v] | ⏺ Record | ⏭ Split | ⏹ Stop
// The dot is grey when idle and red while recording. Settings (device, folder, monitor, compressor) are kept in the config section "recording".
class RecordingBar : public QWidget {
    Q_OBJECT
public:
    RecordingBar(AudioRecorder* recorder, ConfigManager* config, QWidget* parent = nullptr);

    // <dir>/session_YYYYMMDD_HHmm[_name].mp3 ; when that exists already _2, _3 ... is added. The folder is created.
    static QString makeFilename(const QString& dir, const QString& djName = QString());
    static QString defaultDir();                           // ~/Music

    QString folder() const { return dir_; }
    void setFolder(const QString& dir);
    bool isRecording() const;
    // Test access
    QLabel* dot() const { return dot_; }
    QPushButton* recordButton() const { return recordBtn_; }
    QPushButton* splitButton() const { return splitBtn_; }
    QPushButton* stopButton() const { return stopBtn_; }
    QCheckBox* compressorBox() const { return compressorBox_; }
    QCheckBox* monitorBox() const { return monitorBox_; }
    QComboBox* deviceCombo() const { return deviceCombo_; }
    LevelMeterWidget* meter() const { return meter_; }

protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private slots:
    void onRecord();
    void onSplit();
    void onStop();
    void onLevels(float peakL, float peakR, float rmsL, float rmsR);
    void restartMonitor();
    void onMonitorFailed(const QString& msg);
    void onRecordingStarted(const QString& path);
    void onRecordingStopped(const QString& path);
    void onError(const QString& msg);

private:
    void refreshDevices();
    void rebuildFolderMenu();
    void chooseFolder();
    void save(const QString& key, const QString& value);
    QString load(const QString& key) const;
    void setRecordingUi(bool on);

    AudioRecorder* recorder_;
    ConfigManager* config_;
    QLabel* dot_;
    QComboBox* deviceCombo_;
    LevelMeterWidget* meter_;
    QCheckBox* monitorBox_;
    QCheckBox* compressorBox_;
    QToolButton* dirBtn_;
    QMenu* dirMenu_;
    QPushButton* recordBtn_;
    QPushButton* splitBtn_;
    QPushButton* stopBtn_;
    QTimer* monitorTimer_;
    QString dir_;
};

} // namespace vdj
#endif
