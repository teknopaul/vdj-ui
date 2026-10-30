#include "trakcrate_rbimport_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QHeaderView>
#include <QPushButton>
#include <QTreeWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "trakcrate/usb_speed.h"

namespace vdj {

static QString usbSuffix(const QString& path)
{
    const trakcrate::UsbLink usb = trakcrate::usbLinkOfPath(path.toStdString());
    if (!usb.found) return QString();
    return usb.downgraded() ? QStringLiteral("  (%1, supports USB 3)").arg(QString::fromStdString(usb.label)) : QStringLiteral("  (%1)").arg(QString::fromStdString(usb.label));
}

TrakcrateRbImportDialog::TrakcrateRbImportDialog(const trakcrate::Library* lib, QWidget* parent) : QDialog(parent), haveLib_(lib != nullptr)
{
    if (lib) lib_ = *lib;
    setWindowTitle(QStringLiteral("Import from a rekordbox drive"));
    QVBoxLayout* top = new QVBoxLayout(this);
    top->addWidget(new QLabel(QStringLiteral("Real analysis data on the drive (beat grids, cue points, waveforms) and its metadata are taken as they are, "
                                             "and marked as such so our own analysis leaves them alone. The drive is only read.")));

    drives_ = new QComboBox;
    drives_->addItem(QStringLiteral("Mounted drives..."), QString());
    const QStringList roots = {QStringLiteral("/media/") + qEnvironmentVariable("USER"), QStringLiteral("/run/media/") + qEnvironmentVariable("USER"),
                               QStringLiteral("/mnt")};
    for (const QString& r : roots) {
        const QFileInfoList kids = QDir(r).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& k : kids)
            if (trakcrate::isRekordboxDrive(k.absoluteFilePath().toStdString()))
                drives_->addItem(k.fileName() + usbSuffix(k.absoluteFilePath()), k.absoluteFilePath());
    }
    folder_ = new QLineEdit;
    QPushButton* browse = new QPushButton(QStringLiteral("Browse..."));
    QHBoxLayout* row = new QHBoxLayout;
    row->addWidget(drives_);
    row->addWidget(folder_, 1);
    row->addWidget(browse);
    top->addLayout(row);
    status_ = new QLabel;
    status_->setTextFormat(Qt::RichText);
    status_->setWordWrap(true);
    top->addWidget(status_);

    // the drive's playlists: each can become a folder of the library (so the audio stays in groups the DJ knows) and/or a playlist
    playlistFolders_ = new QCheckBox(QStringLiteral("Keep the audio in folders named after the drive's playlists"));
    playlistFolders_->setChecked(true);
    playlistFolders_->setToolTip(QStringLiteral("A track that is in several playlists is stored once, in the first of them (in name order) that is ticked as a folder"));
    top->addWidget(playlistFolders_);
    playlistNote_ = new QLabel;
    playlistNote_->setWordWrap(true);
    top->addWidget(playlistNote_);
    lists_ = new QTreeWidget;
    lists_->setRootIsDecorated(false);
    lists_->setColumnCount(4);
    lists_->setHeaderLabels({QStringLiteral("Playlist"), QStringLiteral("Tracks"), QStringLiteral("Import as folder"), QStringLiteral("Import as playlist")});
    lists_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    lists_->header()->setStretchLastSection(false);
    lists_->setMinimumHeight(160);
    top->addWidget(lists_, 1);
    auto* tickRow = new QHBoxLayout;
    QPushButton* allFolders = new QPushButton(QStringLiteral("All as folders"));
    QPushButton* noFolders = new QPushButton(QStringLiteral("No folders"));
    tickRow->addWidget(allFolders);
    tickRow->addWidget(noFolders);
    tickRow->addStretch(1);
    top->addLayout(tickRow);
    loosePaths_ = new QCheckBox(QStringLiteral("Tracks in no folder playlist: use their folders on the drive (otherwise one folder \"rekordbox\")"));
    top->addWidget(loosePaths_);
    auto setColumn = [this](int col, Qt::CheckState s) {
        for (int i = 0; i < lists_->topLevelItemCount(); ++i) lists_->topLevelItem(i)->setCheckState(col, s);
    };
    connect(allFolders, &QPushButton::clicked, this, [setColumn]() { setColumn(2, Qt::Checked); });
    connect(noFolders, &QPushButton::clicked, this, [setColumn]() { setColumn(2, Qt::Unchecked); });
    connect(playlistFolders_, &QCheckBox::toggled, lists_, [this](bool on) { lists_->setColumnHidden(2, !on); });

    auto box = [&](const QString& text, bool on, const QString& tip = QString()) {
        QCheckBox* c = new QCheckBox(text);
        c->setChecked(on);
        c->setToolTip(tip);
        top->addWidget(c);
        return c;
    };
    playlists_ = box(QStringLiteral("Recreate the ticked playlists as trakcrate playlists"), true, QStringLiteral("Folders on the drive are joined: \"Folder - Playlist\". A playlist that already exists is left alone."));
    grid_ = box(QStringLiteral("Beat grids"), true);
    cues_ = box(QStringLiteral("Cue points and loops (as cuemarks)"), true, QStringLiteral("The first three hot cues stay hot cues A-C, the rest become cuemarks"));
    waveform_ = box(QStringLiteral("Waveforms"), true);
    artwork_ = box(QStringLiteral("Artwork"), true, QStringLiteral("rekordbox drives hold 80x80 images; the picture inside the audio file is used when the drive has none"));
    anlz_ = box(QStringLiteral("Keep the original analysis files (anlz/ in each track)"), true, QStringLiteral("Byte for byte copies, for formats we do not read yet"));
    settings_ = box(QStringLiteral("Copy the player settings (DEVSETTING.DAT, MYSETTING.DAT) and use them for exports"), false,
                    QStringLiteral("Checked first. Exports use these instead of the built in defaults"));
    device_ = box(QStringLiteral("Copy device specific files (RBFLTR.DAT, exportExt.pdb)"), false);
    profile_ = box(QStringLiteral("Copy djprofile.nxs (contains your name)"), false);
    link_ = box(QStringLiteral("Link to the audio on the drive instead of copying it"), false, QStringLiteral("Only sensible for a drive that stays connected"));
    dryRun_ = box(QStringLiteral("Dry run: count what would be imported, change nothing"), false);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(QStringLiteral("Import"));
    top->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, &TrakcrateRbImportDialog::browse);
    connect(folder_, &QLineEdit::textChanged, this, &TrakcrateRbImportDialog::folderEdited);
    connect(drives_, &QComboBox::activated, this, [this](int i) { if (i > 0) folder_->setText(drives_->itemData(i).toString()); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (drives_->count() == 2) folder_->setText(drives_->itemData(1).toString());       // exactly one candidate: offered, not imported
    folderEdited();
}

void TrakcrateRbImportDialog::setFolder(const QString& f) { folder_->setText(f); }
QString TrakcrateRbImportDialog::folder() const { return folder_->text().trimmed(); }

void TrakcrateRbImportDialog::browse()
{
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("rekordbox drive"), folder_->text().isEmpty() ? QStringLiteral("/media") : folder_->text());
    if (!d.isEmpty()) folder_->setText(d);
}

void TrakcrateRbImportDialog::folderEdited()
{
    const bool ok = trakcrate::isRekordboxDrive(folder().toStdString());
    QString text = ok ? QStringLiteral("A rekordbox drive.") : QStringLiteral("Not a rekordbox drive (PIONEER/rekordbox/export.pdb is missing).");
    if (ok) {
        const trakcrate::UsbLink usb = trakcrate::usbLinkOfPath(folder().toStdString());
        if (usb.found) {
            text += QStringLiteral("  Connected as %1.").arg(QString::fromStdString(usb.label));
            if (usb.downgraded())
                text = QStringLiteral("<p style='color:#b71c1c'><b>&#9888; This drive supports USB 3 but is connected as %1.</b> The socket, the cable or the "
                                      "drive's USB 3 contacts are not working: unplug it and plug it in again, or try another socket, before importing.</p>%2")
                           .arg(QString::fromStdString(usb.label).toHtmlEscaped(), text.toHtmlEscaped());
        }
    }
    if (!text.startsWith(QLatin1String("<p"))) text = text.toHtmlEscaped();
    status_->setText(text);
    ok_->setEnabled(ok);
    fillPlaylists();
}

void TrakcrateRbImportDialog::fillPlaylists()
{
    lists_->clear();
    std::vector<trakcrate::RbPlaylistInfo> pls;
    if (trakcrate::isRekordboxDrive(folder().toStdString())) pls = trakcrate::listRekordboxPlaylists(folder().toStdString());
    for (const trakcrate::RbPlaylistInfo& p : pls) {
        auto* it = new QTreeWidgetItem(lists_, QStringList{QString::fromStdString(p.name), QString::number(p.tracks), QString(), QString()});
        it->setData(0, Qt::UserRole, p.id);
        const bool havePl = haveLib_ && trakcrate::rekordboxPlaylistKnown(lib_, p);
        const bool haveFolder = haveLib_ && trakcrate::rekordboxFolderKnown(lib_, p);
        it->setCheckState(2, haveFolder || havePl ? Qt::Unchecked : Qt::Checked);          // already in the library: off unless the user asks
        it->setCheckState(3, havePl ? Qt::Unchecked : Qt::Checked);
        if (havePl || haveFolder) {
            it->setText(0, it->text(0) + QStringLiteral("  (already in the library)"));
            it->setToolTip(0, QStringLiteral("Tick to add the tracks of this playlist the library does not have yet; the tracks it has are left alone"));
        }
        it->setTextAlignment(1, Qt::AlignRight);
    }
    const bool none = pls.empty();
    lists_->setVisible(!none);
    playlistFolders_->setEnabled(!none);
    loosePaths_->setEnabled(!none);
    playlistNote_->setText(none ? QStringLiteral("This drive has no playlists: the tracks are imported into folders named after their folders on the drive, without playlists.")
                                : QStringLiteral("Untick \"folder\" for playlists that only repeat tracks of other playlists. A track is stored once, in the first ticked folder "
                                                 "that contains it. Unticking \"playlist\" leaves that playlist out of the library. Playlists already in the library start unticked: tick one to add only its new tracks."));
}

trakcrate::RbImportOptions TrakcrateRbImportDialog::options() const
{
    trakcrate::RbImportOptions o;
    o.playlists = playlists_->isChecked();
    o.playlistFolders = playlistFolders_->isChecked();
    o.looseByPath = loosePaths_->isChecked();
    for (int i = 0; i < lists_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* it = lists_->topLevelItem(i);
        const uint32_t id = it->data(0, Qt::UserRole).toUInt();
        if (it->checkState(2) != Qt::Checked) o.folderOff.insert(id);
        if (it->checkState(3) != Qt::Checked) o.playlistOff.insert(id);
    }
    o.beatgrid = grid_->isChecked();
    o.cues = cues_->isChecked();
    o.waveform = waveform_->isChecked();
    o.artwork = artwork_->isChecked();
    o.keepAnlz = anlz_->isChecked();
    o.settings = settings_->isChecked();
    o.deviceFiles = device_->isChecked();
    o.profile = profile_->isChecked();
    o.mode = link_->isChecked() ? trakcrate::ImportMode::Link : trakcrate::ImportMode::Copy;
    o.dryRun = dryRun_->isChecked();
    return o;
}

} // namespace vdj
