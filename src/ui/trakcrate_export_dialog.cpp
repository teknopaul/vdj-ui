#include "trakcrate_export_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStorageInfo>

#include "trakcrate/usb_speed.h"
#include <QTimer>
#include <QVBoxLayout>

#include "export/pendrive_registry.h"

namespace vdj {

TrakcrateExportDialog::TrakcrateExportDialog(const trakcrate::Library& lib, QWidget* parent) : QDialog(parent), lib_(lib)
{
    setWindowTitle(QStringLiteral("Export to a rekordbox drive"));
    playlists_ = new QListWidget;
    playlists_->setMaximumHeight(130);
    for (const std::string& p : lib_.listPlaylists()) {
        auto* it = new QListWidgetItem(QString::fromStdString(p), playlists_);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Unchecked);
    }
    dest_ = new QLineEdit;
    dest_->setPlaceholderText(QStringLiteral("an empty folder, or a mounted stick"));
    auto* browse = new QPushButton(QStringLiteral("Browse..."));
    browse_ = browse;
    drives_ = new QComboBox;
    drives_->addItem(QStringLiteral("Mounted drives..."));
    for (const PhysicalPendrive& d : PendriveRegistry::scanPhysical()) {
            const trakcrate::UsbLink usb = trakcrate::usbLinkOfPath(d.mountPoint.toStdString());
            drives_->addItem(QStringLiteral("%1  (%2 free%3)").arg(d.name).arg(QLocale().formattedDataSize(d.freeBytes))
                                 .arg(!usb.found ? QString() : QStringLiteral(", ") + QString::fromStdString(usb.label) + (usb.downgraded() ? QStringLiteral(" - supports USB 3") : QString())), d.mountPoint);
        }
    auto* destRow = new QHBoxLayout;
    destRow->addWidget(dest_, 1);
    destRow->addWidget(browse);

    summary_ = new QLabel;
    summary_->setWordWrap(true);
    summary_->setTextFormat(Qt::RichText);

    advanced_ = new QGroupBox(QStringLiteral("Advanced / compatibility testing"));
    advanced_->setCheckable(true);
    advanced_->setChecked(false);
    settings_ = new QCheckBox(QStringLiteral("DEVSETTING.DAT and MYSETTING.DAT"));
    artwork_ = new QCheckBox(QStringLiteral("Artwork"));
    pvbr_ = new QCheckBox(QStringLiteral("PVBR tag (variable bitrate index)"));
    pwv4_ = new QCheckBox(QStringLiteral("PWV4 tag (colour preview waveform, approximated)"));
    pwv5_ = new QCheckBox(QStringLiteral("PWV5 tag (colour detail waveform)"));
    manifest_ = new QCheckBox(QStringLiteral("SHA256SUMS and trakcrate-manifest.txt in the root"));
    verify_ = new QCheckBox(QStringLiteral("Read the drive back and compare checksums"));
    unanalysed_ = new QCheckBox(QStringLiteral("Allow tracks that are not analysed (flat waveform, no beat grid)"));
    for (QCheckBox* c : {settings_, artwork_, pvbr_, pwv4_, pwv5_, manifest_, verify_}) c->setChecked(true);
    realDrive_ = new QLineEdit;
    realDrive_->setPlaceholderText(QStringLiteral("folder holding PIONEER/ of a real rekordbox drive (other tracks' data!)"));
    exportExt_ = new QCheckBox(QStringLiteral("exportExt.pdb"));
    rbfltr_ = new QCheckBox(QStringLiteral("RBFLTR.DAT"));
    usbmng_ = new QCheckBox(QStringLiteral("USBMNG.DAT"));
    djprofile_ = new QCheckBox(QStringLiteral("djprofile.nxs (contains a user name)"));
    auto* adv = new QVBoxLayout(advanced_);
    for (QCheckBox* c : {settings_, artwork_, pvbr_, pwv4_, pwv5_, manifest_, verify_, unanalysed_}) adv->addWidget(c);
    adv->addWidget(new QLabel(QStringLiteral("Copy from a real drive, only to find out which files a player needs:")));
    adv->addWidget(realDrive_);
    auto* copies = new QHBoxLayout;
    for (QCheckBox* c : {exportExt_, rbfltr_, usbmng_, djprofile_}) copies->addWidget(c);
    adv->addLayout(copies);

    register_ = new QCheckBox(QStringLiteral("Register the folder as a virtual pendrive afterwards (to serve it to the decks)"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(QStringLiteral("Export"));

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Playlists"), playlists_);
    form->addRow(QStringLiteral("Destination"), destRow);
    form->addRow(QString(), drives_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(summary_);
    layout->addWidget(register_);
    layout->addWidget(advanced_);
    layout->addWidget(buttons);
    resize(560, sizeHint().height());

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(250);
    connect(refreshTimer_, &QTimer::timeout, this, &TrakcrateExportDialog::refreshSummary);
    connect(browse, &QPushButton::clicked, this, &TrakcrateExportDialog::browse);
    connect(drives_, QOverload<int>::of(&QComboBox::activated), this, &TrakcrateExportDialog::detectedChosen);
    connect(dest_, &QLineEdit::textChanged, refreshTimer_, QOverload<>::of(&QTimer::start));
    connect(playlists_, &QListWidget::itemChanged, refreshTimer_, QOverload<>::of(&QTimer::start));
    connect(unanalysed_, &QCheckBox::toggled, refreshTimer_, QOverload<>::of(&QTimer::start));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refreshSummary();
}

void TrakcrateExportDialog::setPendrive(const QString& drive, bool physical)
{
    pendrive_ = drive;
    setWindowTitle(physical ? QStringLiteral("Write pendrive \"%1\" to a physical device").arg(drive)
                            : QStringLiteral("Write pendrive \"%1\" as a rekordbox folder").arg(drive));
    playlists_->clear();
    const auto entries = lib_.driveEntries(drive.toStdString());
    for (size_t i = 0; i < entries.size(); ++i) {
        auto* it = new QListWidgetItem(QStringLiteral("%1  (%2 tracks)").arg(QString::fromStdString(entries[i].name)).arg(entries[i].tracks), playlists_);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
        it->setData(Qt::UserRole, int(i));
    }
    if (physical) {
        dest_->setReadOnly(true);
        dest_->setPlaceholderText(QStringLiteral("choose a mounted stick from the list below"));
        if (browse_) browse_->setVisible(false);
    }
    refreshSummary();
}

void TrakcrateExportDialog::setDestination(const QString& dir) { dest_->setText(dir); refreshSummary(); }

void TrakcrateExportDialog::selectPlaylist(const QString& name, bool on)
{
    for (int i = 0; i < playlists_->count(); ++i)
        if (playlists_->item(i)->text() == name) playlists_->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    refreshSummary();
}

void TrakcrateExportDialog::browse()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Destination folder"), dest_->text().isEmpty() ? QDir::homePath() : dest_->text());
    if (!dir.isEmpty()) setDestination(dir);
}

void TrakcrateExportDialog::detectedChosen(int index)
{
    if (index > 0) setDestination(drives_->itemData(index).toString());
    drives_->setCurrentIndex(0);
}

trakcrate::ExportOptions TrakcrateExportDialog::options() const
{
    trakcrate::ExportOptions o;
    for (int i = 0; i < playlists_->count(); ++i) {
        if (playlists_->item(i)->checkState() != Qt::Checked) continue;
        if (pendrive_.isEmpty()) {
            o.playlists.push_back(playlists_->item(i)->text().toStdString());
        } else {                                                   // an entry of the pendrive: its tracks, in the order set on the pendrive
            const int entry = playlists_->item(i)->data(Qt::UserRole).toInt();
            const auto entries = lib_.driveEntries(pendrive_.toStdString());
            if (entry >= 0 && size_t(entry) < entries.size())
                o.lists.push_back({entries[size_t(entry)].name, lib_.driveLibrary(pendrive_.toStdString()).playlistTracks(entries[size_t(entry)].dir)});
        }
    }
    o.dest = dest_->text().trimmed().toStdString();
    o.allowUnanalysed = unanalysed_->isChecked();
    // the advanced switches only count when the box is ticked; otherwise a normal full export
    const bool adv = advanced_->isChecked();
    o.includeSettings = !adv || settings_->isChecked();
    o.includeArtwork = !adv || artwork_->isChecked();
    o.includePvbr = !adv || pvbr_->isChecked();
    o.includePwv4 = !adv || pwv4_->isChecked();
    o.includePwv5 = !adv || pwv5_->isChecked();
    o.writeManifest = !adv || manifest_->isChecked();
    o.verify = !adv || verify_->isChecked();
    if (adv && !realDrive_->text().trimmed().isEmpty()) {
        o.optionalFilesFrom = realDrive_->text().trimmed().toStdString();
        o.copyExportExt = exportExt_->isChecked();
        o.copyRbfltr = rbfltr_->isChecked();
        o.copyUsbmng = usbmng_->isChecked();
        o.copyDjprofile = djprofile_->isChecked();
    }
    return o;
}

bool TrakcrateExportDialog::registerAsVirtualPendrive() const { return register_->isChecked(); }

QString TrakcrateExportDialog::virtualPendriveName() const
{
    const trakcrate::ExportOptions o = options();
    if (o.playlists.size() == 1) return QString::fromStdString(o.playlists[0]);
    return QFileInfo(QString::fromStdString(o.dest.string())).fileName();
}

void TrakcrateExportDialog::refreshSummary()
{
    const trakcrate::ExportOptions o = options();
    QString html;
    const bool haveLists = !o.playlists.empty() || !o.lists.empty();
    bool good = haveLists && !o.dest.empty();
    if (!haveLists) html += QStringLiteral("Tick the playlists to put on the drive.<br>");
    if (haveLists) {
        const trakcrate::ExportSummary s = trakcrate::summariseExport(lib_, o);
        html += QStringLiteral("%1 track(s), %2 of audio.<br>").arg(s.tracks).arg(QLocale().formattedDataSize(qint64(s.audioBytes)));
        for (const std::string& p : s.problems) { good = false; html += QStringLiteral("<span style='color:#c00'>%1</span><br>").arg(QString::fromStdString(p).toHtmlEscaped()); }
        if (!o.dest.empty()) {
            QString path = QString::fromStdString(o.dest.string());
            QFileInfo fi(path);
            while (!fi.exists() && !fi.isRoot() && fi.absoluteDir().exists() == false) fi = QFileInfo(fi.absolutePath());
            QDir existing(fi.exists() ? (fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath()) : fi.absolutePath());
            if (QFileInfo(path).exists() && !QFileInfo(path).isDir()) { good = false; html += QStringLiteral("<span style='color:#c00'>The destination is not a folder.</span><br>"); }
            else if (QDir(path).exists() && !QDir(path).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
                good = false;
                html += QStringLiteral("<span style='color:#c00'>The destination folder is not empty.</span><br>");
            }
            const trakcrate::UsbLink usb = trakcrate::usbLinkOfPath(existing.exists() ? existing.absolutePath().toStdString() : std::string());
            if (usb.found) html += QStringLiteral("Connected as %1.<br>").arg(QString::fromStdString(usb.label).toHtmlEscaped());
            if (usb.downgraded())
                html += QStringLiteral("<span style='color:#b71c1c'><b>&#9888; This drive supports USB 3 but is connected as %1.</b> The socket, the cable or the "
                                       "drive's USB 3 contacts are not working: unplug it and plug it in again, or try another socket, before exporting.</span><br>")
                            .arg(QString::fromStdString(usb.label).toHtmlEscaped());
            QStorageInfo st(existing.exists() ? existing.absolutePath() : QDir::homePath());
            const qint64 need = qint64(s.audioBytes + s.metadataBytes);
            html += QStringLiteral("Free on %1: %2 (needs about %3)<br>").arg(st.displayName().toHtmlEscaped(), QLocale().formattedDataSize(st.bytesAvailable()), QLocale().formattedDataSize(need));
            if (st.bytesAvailable() < need) { good = false; html += QStringLiteral("<span style='color:#c00'>Not enough space.</span><br>"); }
        }
    }
    summary_->setText(html);
    ok_->setEnabled(good);
}

} // namespace vdj
