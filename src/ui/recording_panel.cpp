#include "recording_panel.h"
#include "../audio/audio_recorder.h"
#include "level_meter_widget.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QCheckBox>
#include <QRegularExpression>
#include <QPushButton>
#include <QVBoxLayout>

namespace vdj {

RecordingPanel::RecordingPanel(AudioRecorder* recorder, QWidget* parent)
    : QWidget(parent)
    , recorder_(recorder)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Device row
    auto* devRow = new QHBoxLayout;
    devRow->addWidget(new QLabel(QStringLiteral("Capture:"), this));
    deviceCombo_ = new QComboBox(this);
    const auto devices = AudioRecorder::availableDevices();
    for (const auto& d : devices)
        deviceCombo_->addItem(d);
    if (deviceCombo_->count() == 0)
        deviceCombo_->addItem(QStringLiteral("default"));
    devRow->addWidget(deviceCombo_, 1);
    layout->addLayout(devRow);

    // Input meter: shows which input is receiving audio and helps balance levels. It watches the chosen capture device even when
    // not recording (it opens the device, so untick it if another program needs the input).
    meter_ = new LevelMeterWidget(this);
    layout->addWidget(meter_);
    adviceLabel_ = new QLabel(this);
    adviceLabel_->setStyleSheet(QStringLiteral("font-size:8pt;"));
    layout->addWidget(adviceLabel_);
    monitorBox_ = new QCheckBox(QStringLiteral("Show input level when not recording"), this);
    monitorBox_->setChecked(true);
    monitorBox_->setToolTip(QStringLiteral("Opens the selected capture device to measure it. Choose each input in turn to see which one is receiving audio."));
    layout->addWidget(monitorBox_);

    // File label
    fileLabel_ = new QLabel(QStringLiteral("—"), this);
    fileLabel_->setStyleSheet(QStringLiteral("color:#888; font-size:8pt;"));
    fileLabel_->setWordWrap(true);
    layout->addWidget(fileLabel_);

    // Button row
    auto* btnRow = new QHBoxLayout;
    recordBtn_ = new QPushButton(QStringLiteral("Record"), this);
    splitBtn_  = new QPushButton(QStringLiteral("Split (DJ change)"), this);
    splitBtn_->setEnabled(false);
    btnRow->addWidget(recordBtn_);
    btnRow->addWidget(splitBtn_);
    layout->addLayout(btnRow);

    // Hint
    auto* hint = new QLabel(
        QStringLiteral("Tip: PulseAudio monitor source captures mixer output without a cable."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#666; font-size:8pt;"));
    layout->addWidget(hint);

    connect(recordBtn_, &QPushButton::clicked, this, &RecordingPanel::onRecordStop);
    connect(splitBtn_,  &QPushButton::clicked, this, &RecordingPanel::onSplit);
    connect(recorder_, &AudioRecorder::levels,            this, &RecordingPanel::onLevels);
    connect(recorder_, &AudioRecorder::monitorFailed,     this, &RecordingPanel::onMonitorFailed);
    connect(deviceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { meter_->reset(); restartMonitor(); });
    connect(monitorBox_, &QCheckBox::toggled, this, [this](bool) { meter_->reset(); restartMonitor(); });
    connect(recorder_, &AudioRecorder::recordingStarted,  this, &RecordingPanel::onRecordingStarted);
    connect(recorder_, &AudioRecorder::recordingStopped,  this, &RecordingPanel::onRecordingStopped);
    connect(recorder_, &AudioRecorder::errorOccurred,     this, &RecordingPanel::onError);
}

QString RecordingPanel::makeFilename(const QString& djName)
{
    QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmm"));
    QString safe = djName.isEmpty() ? QStringLiteral("session") : djName;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    QString dir = QDir::homePath() + QStringLiteral("/Music/");
    QDir().mkpath(dir);
    return dir + QStringLiteral("session_%1_%2.mp3").arg(ts, safe);
}

void RecordingPanel::onRecordStop()
{
    if (recorder_->isRecording()) {
        recorder_->stop();
    } else {
        QString path = makeFilename();
        recorder_->start(deviceCombo_->currentText(), path);
    }
}

void RecordingPanel::onSplit()
{
    recorder_->split(makeFilename());
}

void RecordingPanel::onLevels(float peakL, float peakR, float rmsL, float rmsR)
{
    meter_->setLevels(peakL, peakR, rmsL, rmsR);
    const QString z = meter_->zone();
    if (!meter_->hasSignal()) adviceLabel_->setText(QStringLiteral("<span style='color:#888'>No signal on this input</span>"));
    else if (z == QLatin1String("clip")) adviceLabel_->setText(QStringLiteral("<span style='color:#ff6a5a'>Clipping: turn this input down (click the C light to clear)</span>"));
    else if (z == QLatin1String("hot")) adviceLabel_->setText(QStringLiteral("<span style='color:#f0c828'>Hot: peaks above -6 dB, leave a little headroom</span>"));
    else if (z == QLatin1String("good")) adviceLabel_->setText(QStringLiteral("<span style='color:#46d26e'>Good level: peaks between -18 and -6 dB</span>"));
    else adviceLabel_->setText(QStringLiteral("<span style='color:#9aa'>Low: peaks under -18 dB, turn this input up</span>"));
}

void RecordingPanel::restartMonitor()
{
    if (recorder_->isRecording()) return;
    recorder_->stopMonitor();
    if (monitorBox_->isChecked() && isVisible()) recorder_->startMonitor(deviceCombo_->currentText());
    else adviceLabel_->clear();
}

void RecordingPanel::onMonitorFailed(const QString& msg)
{
    meter_->reset();
    adviceLabel_->setText(QStringLiteral("<span style='color:#888'>Cannot measure this input: %1</span>").arg(msg.toHtmlEscaped()));
}

void RecordingPanel::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    restartMonitor();
}

void RecordingPanel::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    if (!recorder_->isRecording()) recorder_->stopMonitor();             // let go of the sound card while nobody is looking
}

void RecordingPanel::onRecordingStarted(const QString& path)
{
    recordBtn_->setText(QStringLiteral("Stop"));
    splitBtn_->setEnabled(true);
    fileLabel_->setText(path);
}

void RecordingPanel::onRecordingStopped(const QString& path)
{
    Q_UNUSED(path)
    recordBtn_->setText(QStringLiteral("Record"));
    splitBtn_->setEnabled(false);
    meter_->reset();
    if (!recorder_->isRecording()) {
        fileLabel_->setText(QStringLiteral("—"));
        restartMonitor();
    }
}

void RecordingPanel::onError(const QString& msg)
{
    QMessageBox::warning(this, QStringLiteral("Recording Error"), msg);
    recordBtn_->setText(QStringLiteral("Record"));
    splitBtn_->setEnabled(false);
}

} // namespace vdj
