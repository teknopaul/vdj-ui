#include "recording_bar.h"

#include "../audio/audio_recorder.h"
#include "../config/config_manager.h"
#include "level_meter_widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QToolButton>

namespace vdj {

namespace {
const QString kSection = QStringLiteral("recording");
const QString kGrey = QStringLiteral("color:#8a8a8a; font-size:14pt;");
const QString kRed = QStringLiteral("color:#e53935; font-size:14pt;");
}

QString RecordingBar::defaultDir() { return QDir::homePath() + QStringLiteral("/Music"); }

QString RecordingBar::makeFilename(const QString& dir, const QString& djName)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmm"));
    QString safe = djName;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    const QString base = dir.isEmpty() ? defaultDir() : dir;
    QDir().mkpath(base);
    const QString stem = QStringLiteral("%1/session_%2%3").arg(base, ts, safe.isEmpty() ? QString() : QLatin1Char('_') + safe);
    QString path = stem + QStringLiteral(".mp3");
    for (int n = 2; QFileInfo::exists(path) && n < 1000; ++n) path = QStringLiteral("%1_%2.mp3").arg(stem).arg(n);
    return path;
}

RecordingBar::RecordingBar(AudioRecorder* recorder, ConfigManager* config, QWidget* parent)
    : QWidget(parent), recorder_(recorder), config_(config)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(6, 2, 6, 2);
    lay->setSpacing(8);

    lay->addWidget(new QLabel(QStringLiteral("Recording"), this));
    dot_ = new QLabel(QStringLiteral("⚫"), this);
    dot_->setStyleSheet(kGrey);
    dot_->setToolTip(QStringLiteral("Grey: idle. Red: recording."));
    lay->addWidget(dot_);

    deviceCombo_ = new QComboBox(this);
    deviceCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    deviceCombo_->setMinimumContentsLength(14);
    deviceCombo_->setMaximumWidth(240);
    deviceCombo_->setToolTip(QStringLiteral("Capture device. `default` and `pipewire` follow the system; choose the mixer's card to record it directly."));
    lay->addWidget(deviceCombo_);

    meter_ = new LevelMeterWidget(this);
    meter_->setCompact(true);
    meter_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lay->addWidget(meter_, 1);

    monitorBox_ = new QCheckBox(QStringLiteral("Monitor"), this);
    monitorBox_->setToolTip(QStringLiteral("Show the input level when not recording. Opens the capture device, so untick it if another program needs the input."));
    lay->addWidget(monitorBox_);

    compressorBox_ = new QCheckBox(QStringLiteral("Compressor"), this);
    if (AudioRecorder::compressorAvailable()) {
        compressorBox_->setToolTip(QStringLiteral("Compress the dynamics with a LADSPA compressor before encoding (applies to the next recording or split)."));
    } else {
        compressorBox_->setEnabled(false);
        compressorBox_->setToolTip(QStringLiteral("No LADSPA compressor found: install the 'caps' package (sudo apt install caps)."));
    }
    lay->addWidget(compressorBox_);

    dirBtn_ = new QToolButton(this);
    dirBtn_->setPopupMode(QToolButton::InstantPopup);
    dirBtn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    dirMenu_ = new QMenu(dirBtn_);
    dirBtn_->setMenu(dirMenu_);
    lay->addWidget(dirBtn_);

    recordBtn_ = new QPushButton(QStringLiteral("⏺ Record"), this);
    splitBtn_ = new QPushButton(QStringLiteral("⏭ Split"), this);
    stopBtn_ = new QPushButton(QStringLiteral("⏹ Stop"), this);
    splitBtn_->setToolTip(QStringLiteral("Close this file and start the next one without a gap (DJ change)."));
    for (QPushButton* b : {recordBtn_, splitBtn_, stopBtn_}) lay->addWidget(b);

    // settings
    refreshDevices();
    const QString dev = load(QStringLiteral("device"));
    if (!dev.isEmpty()) { const int i = deviceCombo_->findText(dev); if (i >= 0) deviceCombo_->setCurrentIndex(i); }
    monitorBox_->setChecked(load(QStringLiteral("monitor")) != QLatin1String("0"));
    compressorBox_->setChecked(compressorBox_->isEnabled() && load(QStringLiteral("compressor")) == QLatin1String("1"));
    recorder_->setCompressor(compressorBox_->isChecked());
    setFolder(load(QStringLiteral("dir")).isEmpty() ? defaultDir() : load(QStringLiteral("dir")));
    setRecordingUi(false);

    // a device is opened only after the user stops scrolling through the list
    monitorTimer_ = new QTimer(this);
    monitorTimer_->setSingleShot(true);
    monitorTimer_->setInterval(150);
    connect(monitorTimer_, &QTimer::timeout, this, &RecordingBar::restartMonitor);

    connect(recordBtn_, &QPushButton::clicked, this, &RecordingBar::onRecord);
    connect(splitBtn_, &QPushButton::clicked, this, &RecordingBar::onSplit);
    connect(stopBtn_, &QPushButton::clicked, this, &RecordingBar::onStop);
    connect(deviceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        save(QStringLiteral("device"), deviceCombo_->currentText());
        meter_->reset();
        recorder_->stopMonitor();
        monitorTimer_->start();
    });
    connect(monitorBox_, &QCheckBox::toggled, this, [this](bool on) {
        save(QStringLiteral("monitor"), on ? QStringLiteral("1") : QStringLiteral("0"));
        meter_->reset();
        monitorTimer_->start();
    });
    connect(compressorBox_, &QCheckBox::toggled, this, [this](bool on) {
        save(QStringLiteral("compressor"), on ? QStringLiteral("1") : QStringLiteral("0"));
        recorder_->setCompressor(on);
    });
    connect(recorder_, &AudioRecorder::levels, this, &RecordingBar::onLevels);
    connect(recorder_, &AudioRecorder::monitorFailed, this, &RecordingBar::onMonitorFailed);
    connect(recorder_, &AudioRecorder::recordingStarted, this, &RecordingBar::onRecordingStarted);
    connect(recorder_, &AudioRecorder::recordingStopped, this, &RecordingBar::onRecordingStopped);
    connect(recorder_, &AudioRecorder::errorOccurred, this, &RecordingBar::onError);
}

QString RecordingBar::load(const QString& key) const { return config_ ? config_->loadValue(kSection, key) : QString(); }
void RecordingBar::save(const QString& key, const QString& value) { if (config_) config_->saveValue(kSection, key, value); }
bool RecordingBar::isRecording() const { return recorder_->isRecording(); }

void RecordingBar::refreshDevices()
{
    const QString keep = deviceCombo_->currentText();
    const QSignalBlocker b(deviceCombo_);
    deviceCombo_->clear();
    for (const QString& d : AudioRecorder::availableDevices()) deviceCombo_->addItem(d);
    if (deviceCombo_->count() == 0) deviceCombo_->addItem(QStringLiteral("default"));
    const int i = deviceCombo_->findText(keep);
    if (i >= 0) deviceCombo_->setCurrentIndex(i);
}

void RecordingBar::setFolder(const QString& dir)
{
    dir_ = QDir::cleanPath(dir);
    save(QStringLiteral("dir"), dir_);
    QStringList recent = load(QStringLiteral("recent_dirs")).split(QLatin1Char('|'), Qt::SkipEmptyParts);
    recent.removeAll(dir_);
    recent.prepend(dir_);
    while (recent.size() > 5) recent.removeLast();
    save(QStringLiteral("recent_dirs"), recent.join(QLatin1Char('|')));
    const QString name = QFileInfo(dir_).fileName().isEmpty() ? dir_ : QFileInfo(dir_).fileName();
    dirBtn_->setText(QStringLiteral("📁 %1 ▾").arg(QFontMetrics(font()).elidedText(name, Qt::ElideMiddle, 140)));
    dirBtn_->setToolTip(QStringLiteral("Recordings are saved in %1").arg(dir_));
    rebuildFolderMenu();
}

void RecordingBar::rebuildFolderMenu()
{
    dirMenu_->clear();
    dirMenu_->addAction(dir_)->setEnabled(false);
    dirMenu_->addSeparator();
    connect(dirMenu_->addAction(QStringLiteral("Select Dir...")), &QAction::triggered, this, &RecordingBar::chooseFolder);
    const QStringList recent = load(QStringLiteral("recent_dirs")).split(QLatin1Char('|'), Qt::SkipEmptyParts);
    bool first = true;
    for (const QString& r : recent) {
        if (r == dir_) continue;
        if (first) { dirMenu_->addSeparator(); first = false; }
        connect(dirMenu_->addAction(r), &QAction::triggered, this, [this, r]() { setFolder(r); });
    }
}

void RecordingBar::chooseFolder()
{
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Folder for the recordings"), dir_);
    if (!d.isEmpty()) setFolder(d);
}

void RecordingBar::setRecordingUi(bool on)
{
    dot_->setText(on ? QStringLiteral("🔴") : QStringLiteral("⚫"));
    dot_->setStyleSheet(on ? kRed : kGrey);
    recordBtn_->setEnabled(!on);
    splitBtn_->setEnabled(on);
    stopBtn_->setEnabled(on);
    deviceCombo_->setEnabled(!on);
    dirBtn_->setEnabled(!on);
}

void RecordingBar::onRecord()
{
    QDir().mkpath(dir_);
    const QFileInfo fi(dir_);
    if (!fi.isDir() || !fi.isWritable()) {
        QMessageBox::warning(this, QStringLiteral("Recording"), QStringLiteral("Cannot write to %1: choose another folder.").arg(dir_));
        return;
    }
    recorder_->setCompressor(compressorBox_->isChecked());
    recorder_->start(deviceCombo_->currentText(), makeFilename(dir_));
}

void RecordingBar::onSplit()
{
    recorder_->setCompressor(compressorBox_->isChecked());
    recorder_->split(makeFilename(dir_));
}

void RecordingBar::onStop() { recorder_->stop(); }

void RecordingBar::onLevels(float peakL, float peakR, float rmsL, float rmsR)
{
    meter_->setLevels(peakL, peakR, rmsL, rmsR);
    const QString z = meter_->zone();
    QString tip;
    if (!meter_->hasSignal()) tip = QStringLiteral("No signal on this input");
    else if (z == QLatin1String("clip")) tip = QStringLiteral("Clipping: turn this input down (click the meter to clear the light)");
    else if (z == QLatin1String("hot")) tip = QStringLiteral("Hot: peaks above -6 dB, leave a little headroom");
    else if (z == QLatin1String("good")) tip = QStringLiteral("Good level: peaks between -18 and -6 dB");
    else tip = QStringLiteral("Low: peaks under -18 dB, turn this input up");
    meter_->setToolTip(tip);
}

void RecordingBar::restartMonitor()
{
    if (recorder_->isRecording()) return;
    recorder_->stopMonitor();
    if (monitorBox_->isChecked() && isVisible()) recorder_->startMonitor(deviceCombo_->currentText());
}

void RecordingBar::onMonitorFailed(const QString& msg)
{
    meter_->reset();
    meter_->setToolTip(QStringLiteral("Cannot measure this input: %1").arg(msg));
}

void RecordingBar::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    refreshDevices();
    monitorTimer_->start();
}

void RecordingBar::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    monitorTimer_->stop();
    if (!recorder_->isRecording()) recorder_->stopMonitor();             // let go of the sound card while nobody is looking
}

void RecordingBar::onRecordingStarted(const QString& path)
{
    setRecordingUi(true);
    dot_->setToolTip(QStringLiteral("Recording to %1").arg(path));
}

void RecordingBar::onRecordingStopped(const QString&)
{
    if (recorder_->isRecording()) return;                                // a split: the next file is already running
    setRecordingUi(false);
    dot_->setToolTip(QStringLiteral("Grey: idle. Red: recording."));
    meter_->reset();
    monitorTimer_->start();
}

void RecordingBar::onError(const QString& msg)
{
    QMessageBox::warning(this, QStringLiteral("Recording Error"), msg);
    if (!recorder_->isRecording()) setRecordingUi(false);
}

} // namespace vdj
