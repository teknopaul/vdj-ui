#include "recording_panel.h"
#include "../audio/audio_recorder.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
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

    // VU meter
    vuMeter_ = new QProgressBar(this);
    vuMeter_->setRange(0, 100);
    vuMeter_->setValue(0);
    vuMeter_->setTextVisible(false);
    vuMeter_->setFixedHeight(10);
    layout->addWidget(vuMeter_);

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
    connect(recorder_, &AudioRecorder::levelChanged,      this, &RecordingPanel::onLevelChanged);
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

void RecordingPanel::onLevelChanged(float rms)
{
    // rms is 0.0–1.0; display on a logarithmic-ish scale
    int val = static_cast<int>(rms * 300.0f);
    vuMeter_->setValue(qMin(val, 100));
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
    vuMeter_->setValue(0);
    if (!recorder_->isRecording())
        fileLabel_->setText(QStringLiteral("—"));
}

void RecordingPanel::onError(const QString& msg)
{
    QMessageBox::warning(this, QStringLiteral("Recording Error"), msg);
    recordBtn_->setText(QStringLiteral("Record"));
    splitBtn_->setEnabled(false);
}

} // namespace vdj
