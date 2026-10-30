#include "trakcrate_export_dialog.h"

#include <QAbstractItemView>
#include <QSizePolicy>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QIcon>
#include <QPixmap>
#include <QToolButton>
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
    playlists_->setMinimumHeight(100);
    playlists_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    for (const std::string& p : lib_.listPlaylists()) {
        auto* it = new QListWidgetItem(QString::fromStdString(p), playlists_);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Unchecked);
    }
    dest_ = new QLineEdit;
    dest_->setPlaceholderText(QStringLiteral("a folder, or a mounted stick (other files on it are left alone)"));
    auto* browse = new QPushButton(QStringLiteral("Browse..."));
    browse_ = browse;
    drives_ = new QComboBox;
    rescanDrives();
    auto* destRow = new QHBoxLayout;
    destRow->addWidget(dest_, 1);
    destRow->addWidget(browse);

    summary_ = new QLabel;
    summary_->setWordWrap(true);
    summary_->setTextFormat(Qt::RichText);
    summary_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    summary_->setMinimumHeight(summary_->fontMetrics().lineSpacing() * 6);     // room for the usual messages: the dialog does not jump when they change

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
    for (QCheckBox* c : {settings_, artwork_, pvbr_, pwv4_, pwv5_}) c->setChecked(true);      // checksums and the read back are slow on a stick: off unless asked for
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

    update_ = new QCheckBox(QStringLiteral("Update the drive (only changed files are written; the old export.pdb is kept as export.pdb.bak, up to 3 backups)"));
    update_->hide();
    register_ = new QCheckBox(QStringLiteral("Register the folder as an HD pendrive copy afterwards (to serve it to the decks)"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(QStringLiteral("Export"));

    // a grid, not a form: when the dialog is made taller all the extra height goes to the playlist list, the destination and pendrive rows keep their size
    auto* form = new QGridLayout;
    auto iconLabel = [](const char* res, const QString& tip) {
        auto* l = new QLabel;
        l->setPixmap(QPixmap(QString::fromLatin1(res)));
        l->setFixedSize(64, 64);
        l->setToolTip(tip);
        return l;
    };
    // fixed-height row: the combo's popup must not make the dialog grow
    auto* drivesRow = new QWidget;
    auto* drivesLay = new QHBoxLayout(drivesRow);
    drivesLay->setContentsMargins(0, 0, 0, 0);
    drivesLay->addWidget(drives_, 1, Qt::AlignVCenter);
    auto* refresh = new QToolButton;
    refresh->setIcon(QIcon(QStringLiteral(":/img/view-refresh-22x22.png")));
    refresh->setToolTip(QStringLiteral("Look for newly plugged in drives"));
    drivesLay->addWidget(refresh, 0, Qt::AlignVCenter);
    connect(refresh, &QToolButton::clicked, this, &TrakcrateExportDialog::rescanDrives);
    drivesRow->setFixedHeight(64);
    drivesRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // a long drive name must not widen the combo (and so the dialog): the width does not depend on the item shown, the popup still shows it whole
    drives_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    drives_->setMinimumContentsLength(20);
    drives_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    drives_->view()->setTextElideMode(Qt::ElideNone);
    form->addWidget(iconLabel(":/img/traklist-64x64.png", QStringLiteral("Playlists")), 0, 0, Qt::AlignTop);
    form->addWidget(playlists_, 0, 1);
    form->addWidget(new QLabel(QStringLiteral("Destination")), 1, 0);
    form->addLayout(destRow, 1, 1);
    form->addWidget(iconLabel(":/img/pendrive-64x64.png", QStringLiteral("Pendrive")), 2, 0, Qt::AlignTop);
    form->addWidget(drivesRow, 2, 1, Qt::AlignTop);
    form->setRowStretch(0, 1);
    form->setRowStretch(1, 0);
    form->setRowStretch(2, 0);
    form->setColumnStretch(1, 1);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form, 1);
    layout->addWidget(summary_);
    layout->addWidget(update_);
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
    // the destination path is shown in bold; typing a path of one's own no longer matches the drive shown in the combo
    connect(dest_, &QLineEdit::textChanged, this, [this](const QString& t) {
        QFont f = dest_->font();
        f.setBold(!t.isEmpty());
        dest_->setFont(f);
    });
    connect(dest_, &QLineEdit::textEdited, this, [this](const QString& t) {
        if (drives_->currentIndex() > 0 && drives_->currentData().toString() != t) drives_->setCurrentIndex(0);
    });
    connect(dest_, &QLineEdit::textChanged, refreshTimer_, QOverload<>::of(&QTimer::start));
    connect(playlists_, &QListWidget::itemChanged, refreshTimer_, QOverload<>::of(&QTimer::start));
    connect(update_, &QCheckBox::toggled, refreshTimer_, QOverload<>::of(&QTimer::start));
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

void TrakcrateExportDialog::rescanDrives()
{
    const QString chosen = drives_->currentIndex() > 0 ? drives_->currentData().toString() : QString();
    drives_->clear();
    drives_->addItem(QStringLiteral("Mounted drives..."));
    for (const PhysicalPendrive& d : PendriveRegistry::scanPhysical()) {
        const trakcrate::UsbLink usb = trakcrate::usbLinkOfPath(d.mountPoint.toStdString());
        drives_->addItem(QStringLiteral("%1  (%2 free%3)").arg(d.name).arg(QLocale().formattedDataSize(d.freeBytes))
                             .arg(!usb.found ? QString() : QStringLiteral(", ") + QString::fromStdString(usb.label) + (usb.downgraded() ? QStringLiteral(" - supports USB 3") : QString())), d.mountPoint);
    }
    const int again = chosen.isEmpty() ? -1 : drives_->findData(chosen);
    if (again > 0) drives_->setCurrentIndex(again);
}

void TrakcrateExportDialog::detectedChosen(int index)
{
    if (index > 0) setDestination(drives_->itemData(index).toString());     // the chosen drive stays shown in the combo
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
    o.update = !update_->isHidden() && update_->isChecked();
    // the advanced switches only count when the box is ticked; otherwise a normal full export
    const bool adv = advanced_->isChecked();
    o.includeSettings = !adv || settings_->isChecked();
    o.includeArtwork = !adv || artwork_->isChecked();
    o.includePvbr = !adv || pvbr_->isChecked();
    o.includePwv4 = !adv || pwv4_->isChecked();
    o.includePwv5 = !adv || pwv5_->isChecked();
    o.writeManifest = adv && manifest_->isChecked();
    o.verify = adv && verify_->isChecked();
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
            else if (QDir(path).exists()) {
                const std::vector<std::string> clash = trakcrate::exportConflicts(o.dest);      // other files on a stick are fine
                update_->setVisible(!clash.empty());
                if (!clash.empty()) {
                    QStringList names;
                    for (const std::string& n : clash) names << QString::fromStdString(n);
                    if (o.update) {
                        html += QStringLiteral("Updating the earlier export on the drive (%1): nothing is deleted.<br>").arg(names.join(QStringLiteral(", ")).toHtmlEscaped());
                    } else {
                        good = false;
                        html += QStringLiteral("<span style='color:#c00'>The destination already has %1 (an earlier export?). Tick Update to refresh it.</span><br>").arg(names.join(QStringLiteral(", ")).toHtmlEscaped());
                    }
                }
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
    if (o.dest.empty() || !QDir(QString::fromStdString(o.dest.string())).exists()) update_->setVisible(false);
    summary_->setText(html);
    ok_->setEnabled(good);
}

} // namespace vdj
