#include "midi_monitor_dialog.h"
#include "midi/midi_map_input.h"
#include <QCheckBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

namespace vdj {

MidiMonitorDialog::MidiMonitorDialog(MidiMapInput* input, QWidget* parent)
    : QDialog(parent), input_(input)
{
    setWindowTitle(QStringLiteral("MIDI Monitor"));
    resize(640, 420);
    auto* layout = new QVBoxLayout(this);

    info_ = new QLabel(this);
    info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    info_->setWordWrap(true);
    layout->addWidget(info_);

    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(1000);
    log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    log_->setFont(mono);
    layout->addWidget(log_, 1);

    auto* row = new QHBoxLayout;
    pause_ = new QCheckBox(QStringLiteral("Pause"), this);
    auto* clear = new QPushButton(QStringLiteral("Clear"), this);
    row->addWidget(pause_);
    row->addWidget(clear);
    row->addStretch(1);
    layout->addLayout(row);

    connect(clear, &QPushButton::clicked, log_, &QPlainTextEdit::clear);
    connect(input_, &MidiMapInput::midiEvent, this, &MidiMonitorDialog::onEvent);

    infoTimer_ = new QTimer(this);
    infoTimer_->setInterval(1000);
    connect(infoTimer_, &QTimer::timeout, this, &MidiMonitorDialog::refreshInfo);
    refreshInfo();
}

void MidiMonitorDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    input_->setMonitoring(true);
    infoTimer_->start();
    refreshInfo();
}

void MidiMonitorDialog::hideEvent(QHideEvent* event)
{
    QDialog::hideEvent(event);
    input_->setMonitoring(false);
    infoTimer_->stop();
}

void MidiMonitorDialog::onEvent(const QString& description, int op)
{
    if (pause_->isChecked())
        return;
    const QString mapped = op > 0 ? QStringLiteral("→ %1").arg(MidiMapInput::opName(op))
                                  : QStringLiteral("not mapped");
    log_->appendPlainText(QStringLiteral("%1  %2  %3")
        .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")), description.leftJustified(40), mapped));
}

QString MidiMonitorDialog::infoText() const { return info_->text(); }

void MidiMonitorDialog::refreshInfo()
{
    QString text;
    if (!input_->isRunning()) {
        text = QStringLiteral("MIDI input is not running: choose a map with Config > MIDI Map...");
    } else {
        const QStringList sources = input_->sourceNames();
        text = QStringLiteral("Map: %1 (%2 operations)\nPort: %3:%4 \"vdj-midi-in:midi-in\"   Connected from: %5")
            .arg(input_->mapPath().isEmpty() ? QStringLiteral("none") : input_->mapPath())
            .arg(input_->mappedOps())
            .arg(input_->clientId()).arg(input_->portId())
            .arg(sources.isEmpty() ? QStringLiteral("nothing — connect the controller (Config > Instruments..., or aconnect)")
                                   : sources.join(QStringLiteral(", ")));
    }
    if (info_->text() != text) info_->setText(text);
}

} // namespace vdj
