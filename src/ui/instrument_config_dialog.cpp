#include "instrument_config_dialog.h"
#include "midi/instrument_manager.h"
#include "midi/instrument_profile.h"
#include "config/config_manager.h"

#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDir>
#include <QMessageBox>

namespace vdj {

InstrumentConfigDialog::InstrumentConfigDialog(InstrumentManager* manager,
                                               ConfigManager* config,
                                               QWidget* parent)
    : QDialog(parent), manager_(manager), config_(config)
{
    setWindowTitle(QStringLiteral("Instrument Configuration"));
    resize(600, 440);

    auto* layout = new QVBoxLayout(this);

    auto* tabs    = new QTabWidget(this);
    auto* midiTab = new QWidget;
    auto* jsTab   = new QWidget;
    buildMidiTab(midiTab);
    buildJoystickTab(jsTab);
    tabs->addTab(midiTab, QStringLiteral("MIDI Devices"));
    tabs->addTab(jsTab,   QStringLiteral("Joystick Profiles"));
    layout->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &InstrumentConfigDialog::saveAndWire);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ── MIDI tab ──────────────────────────────────────────────────────────────────

void InstrumentConfigDialog::buildMidiTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    auto* hint = new QLabel(
        QStringLiteral("Select which connected device to use as MIDI input "
                       "(controller → VDJ) and which as MIDI output "
                       "(VDJ clock/start/stop → drum machine). "
                       "Assignments are saved by USB ID and re-applied on boot."), tab);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    deviceTable_ = new QTableWidget(0, 4, tab);
    deviceTable_->setHorizontalHeaderLabels({
        QStringLiteral("Device"),
        QStringLiteral("Profile"),
        QStringLiteral("In"),
        QStringLiteral("Out")
    });
    deviceTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    deviceTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    deviceTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    deviceTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(deviceTable_, 1);

    auto* form = new QFormLayout;
    inputCombo_  = new QComboBox(tab);
    outputCombo_ = new QComboBox(tab);
    form->addRow(QStringLiteral("MIDI Input (controller → VDJ):"),          inputCombo_);
    form->addRow(QStringLiteral("MIDI Output (VDJ clock → drum machine):"), outputCombo_);
    layout->addLayout(form);

    auto* rescanBtn = new QPushButton(QStringLiteral("Rescan"), tab);
    layout->addWidget(rescanBtn);
    connect(rescanBtn, &QPushButton::clicked, this, &InstrumentConfigDialog::rescan);

    populateMidiTab();
}

void InstrumentConfigDialog::populateMidiTab()
{
    deviceTable_->setRowCount(0);
    inputCombo_->clear();
    outputCombo_->clear();
    comboKeys_.clear();

    // First entry = none
    inputCombo_->addItem(QStringLiteral("(none)"));
    outputCombo_->addItem(QStringLiteral("(none)"));
    comboKeys_.append(QString());

    const QString savedIn  = manager_->midiInputKey();
    const QString savedOut = manager_->midiOutputKey();
    int selIn = 0, selOut = 0;

    const QList<InstrumentManager::DetectedDevice> devs = manager_->detectDevices();
    for (const InstrumentManager::DetectedDevice& d : devs) {
        int row = deviceTable_->rowCount();
        deviceTable_->insertRow(row);

        QString label = d.displayName;
        if (!d.usbId.isEmpty())
            label += QStringLiteral("  [%1]").arg(d.usbId);

        deviceTable_->setItem(row, 0, new QTableWidgetItem(label));
        deviceTable_->setItem(row, 1, new QTableWidgetItem(
            d.profile ? d.profile->name : QString()));
        deviceTable_->setItem(row, 2, new QTableWidgetItem(
            d.inputPorts  > 0 ? QStringLiteral("✓") : QString()));
        deviceTable_->setItem(row, 3, new QTableWidgetItem(
            d.outputPorts > 0 ? QStringLiteral("✓") : QString()));

        int comboIdx = comboKeys_.size();
        comboKeys_.append(d.key);

        if (d.inputPorts > 0) {
            inputCombo_->addItem(d.displayName, d.key);
            if (d.key == savedIn)  selIn  = inputCombo_->count() - 1;
        }
        if (d.outputPorts > 0) {
            outputCombo_->addItem(d.displayName, d.key);
            if (d.key == savedOut) selOut = outputCombo_->count() - 1;
        }
        (void)comboIdx;
    }

    inputCombo_->setCurrentIndex(selIn);
    outputCombo_->setCurrentIndex(selOut);
}

// ── Joystick tab ──────────────────────────────────────────────────────────────

void InstrumentConfigDialog::buildJoystickTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    auto* hint = new QLabel(
        QStringLiteral("Clone controller? Pick your joystick device and a profile with "
                       "matching button layout, then Apply to copy the default mappings. "
                       "Fine-tune in Config → Joystick."), tab);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* form = new QFormLayout;
    jsDeviceCombo_  = new QComboBox(tab);
    jsProfileCombo_ = new QComboBox(tab);
    form->addRow(QStringLiteral("Joystick Device:"), jsDeviceCombo_);
    form->addRow(QStringLiteral("Use Profile:"),     jsProfileCombo_);
    layout->addLayout(form);

    auto* applyBtn = new QPushButton(QStringLiteral("Apply Profile to Device"), tab);
    layout->addWidget(applyBtn);

    layout->addWidget(new QLabel(QStringLiteral("Currently assigned:"), tab));
    jsAssignList_ = new QListWidget(tab);
    layout->addWidget(jsAssignList_, 1);

    connect(applyBtn, &QPushButton::clicked,
            this, &InstrumentConfigDialog::applyJoystickProfile);

    populateJoystickTab();
}

void InstrumentConfigDialog::populateJoystickTab()
{
    jsDeviceCombo_->clear();
    jsProfileCombo_->clear();
    jsAssignList_->clear();

    QDir dev(QStringLiteral("/dev/input"));
    const QStringList jdevs = dev.entryList(
        QStringList() << QStringLiteral("js*"), QDir::System);
    for (const QString& e : jdevs)
        jsDeviceCombo_->addItem(dev.filePath(e));

    for (const InstrumentProfile& p : manager_->profiles())
        jsProfileCombo_->addItem(p.name, p.id);

    const QStringList keys = config_->keysOf(QStringLiteral("joystick_profiles"));
    for (const QString& devicePath : keys) {
        QString profileId = config_->loadValue(
            QStringLiteral("joystick_profiles"), devicePath);
        jsAssignList_->addItem(
            QStringLiteral("%1  →  %2").arg(devicePath, profileId));
    }
}

// ── Actions ───────────────────────────────────────────────────────────────────

void InstrumentConfigDialog::rescan()
{
    populateMidiTab();
}

void InstrumentConfigDialog::saveAndWire()
{
    // Save input selection
    int si = inputCombo_->currentIndex();
    manager_->setMidiInputKey(si > 0 ? inputCombo_->itemData(si).toString() : QString());

    // Save output selection
    int di = outputCombo_->currentIndex();
    manager_->setMidiOutputKey(di > 0 ? outputCombo_->itemData(di).toString() : QString());

    // Create ALSA subscriptions immediately
    manager_->wireDevices();

    accept();
}

void InstrumentConfigDialog::applyJoystickProfile()
{
    QString devicePath = jsDeviceCombo_->currentText();
    QString profileId  = jsProfileCombo_->currentData().toString();

    if (devicePath.isEmpty() || profileId.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Nothing Selected"),
            QStringLiteral("Select a joystick device and a profile first."));
        return;
    }

    manager_->applyProfileToJoystick(devicePath, profileId);
    populateJoystickTab();
    QMessageBox::information(this, QStringLiteral("Profile Applied"),
        QStringLiteral("Default mappings from '%1' applied to %2.\n"
                       "Open Config → Joystick to review or adjust.")
            .arg(profileId, devicePath));
}

} // namespace vdj
