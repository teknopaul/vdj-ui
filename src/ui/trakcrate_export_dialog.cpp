#include "trakcrate_export_dialog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QMetaObject>
#include <functional>
#include <thread>
#include <QSizePolicy>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QLineEdit>
#include <QListWidget>
#include <QIcon>
#include <QPixmap>
#include <QToolButton>
#include <QPushButton>
#include <QStorageInfo>
#include <unistd.h>

#include "trakcrate/settings_sets.h"
#include "trakcrate/usb_speed.h"
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include "export/pendrive_registry.h"

namespace vdj {

// Runs work on a thread of its own; the function it returns runs afterwards in the GUI thread, unless the dialog is gone by then.
static void spawn(const std::shared_ptr<std::atomic<bool>>& alive, std::function<std::function<void()>()> work)
{
    std::thread([alive, work = std::move(work)] {
        std::function<void()> done = work();
        QMetaObject::invokeMethod(qApp, [alive, done = std::move(done)] { if (alive->load()) done(); }, Qt::QueuedConnection);
    }).detach();
}

TrakcrateExportDialog::TrakcrateExportDialog(const trakcrate::Library& lib, QWidget* parent, bool async) : QDialog(parent), async_(async), lib_(lib)
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
    extraDrives_ = new QListWidget;                      // before the first scan, which fills it
    rescanDrives();
    auto* destRow = new QHBoxLayout;
    destRow->addWidget(dest_, 1);
    destRow->addWidget(browse);

    summary_ = new QLabel;
    summary_->setWordWrap(true);
    summary_->setTextFormat(Qt::RichText);
    summary_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    summary_->setMinimumHeight(summary_->fontMetrics().lineSpacing() * 6);     // room for the usual messages: the dialog does not jump when they change

    advanced_ = new QGroupBox(QStringLiteral("Advanced Export Options"));
    advanced_->setCheckable(true);
    advanced_->setChecked(false);
    settings_ = new QCheckBox(QStringLiteral("The five player settings files (players need them: compatibility testing only)"));
    artwork_ = new QCheckBox(QStringLiteral("Artwork"));
    pvbr_ = new QCheckBox(QStringLiteral("PVBR tag (variable bitrate index)"));
    pwv4_ = new QCheckBox(QStringLiteral("PWV4 tag (colour preview waveform, approximated)"));
    pwv5_ = new QCheckBox(QStringLiteral("PWV5 tag (colour detail waveform)"));
    manifest_ = new QCheckBox(QStringLiteral("SHA256SUMS and trakcrate-manifest.txt in the root"));
    verify_ = new QCheckBox(QStringLiteral("Read the drive back and compare checksums"));
    unanalysed_ = new QCheckBox(QStringLiteral("Allow tracks that are not analysed (flat waveform, no beat grid)"));
    for (QCheckBox* c : {settings_, artwork_, pvbr_, pwv4_, pwv5_}) c->setChecked(true);      // checksums and the read back are slow on a stick: off unless asked for
    auto* advOuter = new QVBoxLayout(advanced_);
    auto* extras = new QWidget;                                  // shown only while the box is ticked
    advOuter->addWidget(extras);
    advOuter->setContentsMargins(advOuter->contentsMargins().left(), 0, advOuter->contentsMargins().right(), 0);
    auto* adv = new QVBoxLayout(extras);
    adv->setContentsMargins(0, 0, 0, 0);
    for (QCheckBox* c : {settings_, artwork_, pvbr_, pwv4_, pwv5_, manifest_, verify_, unanalysed_}) adv->addWidget(c);
    extras->setVisible(false);
    connect(advanced_, &QGroupBox::toggled, this, [this, extras](bool on) {
        extras->setVisible(on);
        layout()->activate();
        resize(width(), on ? sizeHint().height() : minimumSizeHint().height());
    });

    raiseLimits_ = new QCheckBox(QStringLiteral("Apply modern deck limits (up to 20000 tracks, 16000 artists and albums; newer decks only, YMMV)"));
    raiseLimits_->setToolTip(QStringLiteral("Older decks run out of memory above 10000 tracks, artists or albums. Tick this only if all your decks are newer models."));
    raiseLimits_->hide();
    update_ = new QCheckBox(QStringLiteral("Update the drive (only changed files are written; the old export.pdb is kept as export.pdb.bak, up to 3 backups)"));
    update_->hide();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(QStringLiteral("Export"));

    // blocks, top to bottom; when the dialog is made taller all the extra height goes to the list of folders, the other blocks keep their size
    auto iconLabel = [](const char* res, const QString& tip) {
        auto* l = new QLabel;
        l->setPixmap(QPixmap(QString::fromLatin1(res)));
        l->setFixedSize(64, 64);
        l->setToolTip(tip);
        return l;
    };
    // the main destination: a little larger than the rest of the text
    auto* destLabel = new QLabel(QStringLiteral("Destination:"));
    QFont big = font();
    big.setPointSizeF(big.pointSizeF() * 1.2);
    destLabel->setFont(big);
    dest_->setFont(big);
    destRow->insertWidget(0, destLabel);

    // fixed-height row: the combo's popup must not make the dialog grow
    physicalBox_ = new QWidget;
    physicalBox_->setObjectName(QStringLiteral("physicalBox"));
    auto* physLay = new QVBoxLayout(physicalBox_);
    physLay->setContentsMargins(0, 0, 0, 0);
    auto* drivesRow = new QWidget;
    auto* drivesLay = new QHBoxLayout(drivesRow);
    drivesLay->setContentsMargins(0, 0, 0, 0);
    drivesLay->addWidget(iconLabel(":/img/pendrive-64x64.png", QStringLiteral("Pendrive")));
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
    physLay->addWidget(drivesRow);
    extraBox_ = new QWidget;
    extraBox_->setObjectName(QStringLiteral("extraBox"));
    {
        auto* el = new QVBoxLayout(extraBox_);
        el->setContentsMargins(0, 0, 0, 0);
        el->addWidget(new QLabel(QStringLiteral("Simultaneous export destination(s)")));
        extraDrives_->setFixedHeight(extraDrives_->fontMetrics().lineSpacing() * 4 + 12);
        el->addWidget(extraDrives_);
    }
    physLay->addWidget(extraBox_);
    physicalBox_->hide();                                    // physical device mode only: otherwise the destination folder is all that is needed

    auto* listRow = new QHBoxLayout;
    listRow->addWidget(iconLabel(":/img/traklist-64x64.png", QStringLiteral("Playlists")), 0, Qt::AlignTop);
    listRow->addWidget(playlists_, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(destRow);
    layout->addWidget(physicalBox_);
    layout->addWidget(update_);
    layout->addWidget(summary_);
    spacesBox_ = new QWidget;
    spacesBox_->setObjectName(QStringLiteral("spacesBox"));
    new QVBoxLayout(spacesBox_);
    spacesBox_->layout()->setContentsMargins(0, 0, 0, 0);
    spacesBox_->hide();
    layout->addWidget(spacesBox_);
    layout->addWidget(raiseLimits_);
    layout->addLayout(listRow, 1);
    {
        auto* box = new QGroupBox;
        auto* bl = new QVBoxLayout(box);
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(QStringLiteral("User Settings: ")));
        settingsSet_ = new QComboBox;
        settingsSet_->setToolTip(QStringLiteral("DEVSETTING.DAT, MYSETTING.DAT, djprofile.nxs, RBFLTR.DAT and exportExt.pdb. Players need all of them, so they are always written."));
        row->addWidget(settingsSet_, 1);
        bl->addLayout(row);
        addMissing_ = new QRadioButton(QStringLiteral("Add missing User Settings"));
        replace_ = new QRadioButton(QStringLiteral("Overwrite All User Settings"));
        addMissing_->setChecked(true);
        bl->addWidget(addMissing_);
        bl->addWidget(replace_);
        const std::string def = trakcrate::defaultSettingsSet(lib_);
        settingsSet_->addItem(def.empty() ? QStringLiteral("Default (none created yet: built in files)") : QStringLiteral("Default (%1)").arg(QString::fromStdString(def)), QString());
        for (const trakcrate::SettingsSet& set : trakcrate::listSettingsSets(lib_)) settingsSet_->addItem(QString::fromStdString(set.name), QString::fromStdString(set.name));
        settingsSet_->addItem(QStringLiteral("Built in files (no settings set)"), builtInSettingsKey());
        layout->addWidget(box);
    }
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
    connect(dest_, &QLineEdit::textChanged, this, &TrakcrateExportDialog::syncExtraDrives);
    connect(dest_, &QLineEdit::textChanged, refreshTimer_, QOverload<>::of(&QTimer::start));
    connect(playlists_, &QListWidget::itemChanged, refreshTimer_, QOverload<>::of(&QTimer::start));
    connect(extraDrives_, &QListWidget::itemChanged, refreshTimer_, QOverload<>::of(&QTimer::start));
    connect(update_, &QCheckBox::toggled, refreshTimer_, QOverload<>::of(&QTimer::start));
    connect(raiseLimits_, &QCheckBox::toggled, refreshTimer_, QOverload<>::of(&QTimer::start));
    connect(unanalysed_, &QCheckBox::toggled, refreshTimer_, QOverload<>::of(&QTimer::start));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refreshSummary();
}

TrakcrateExportDialog::~TrakcrateExportDialog() { alive_->store(false); }

void TrakcrateExportDialog::setPendrive(const QString& drive, bool physical)
{
    pendrive_ = drive;
    setWindowTitle(physical ? QStringLiteral("Write pendrive \"%1\" to a physical device").arg(drive)
                            : QStringLiteral("Write pendrive \"%1\" as a rekordbox folder").arg(drive));
    setSettingsSet(QString::fromStdString(trakcrate::effectiveDriveSettingsSet(lib_, drive.toStdString())));      // the set the pendrive is linked to
    entries_.clear();
    playlists_->clear();
    physical_ = physical;
    physicalBox_->setVisible(physical);
    if (physical) {
        dest_->setReadOnly(true);
        dest_->setPlaceholderText(QStringLiteral("choose a mounted stick from the list below"));
        if (browse_) browse_->setVisible(false);
    }
    if (!async_) {
        entries_ = lib_.driveEntries(drive.toStdString());
        fillEntries();
        return;
    }
    auto* wait = new QListWidgetItem(QStringLiteral("Loading..."), playlists_);      // reading every entry's links can take a while on a big pendrive
    wait->setFlags(Qt::NoItemFlags);
    loadingEntries_ = true;
    refreshSummary();
    const trakcrate::Library lib = lib_;
    const std::string name = drive.toStdString();
    spawn(alive_, [this, lib, name] {
        auto es = lib.driveEntries(name);
        return std::function<void()>([this, es = std::move(es)]() mutable {
            entries_ = std::move(es);
            loadingEntries_ = false;
            fillEntries();
        });
    });
}

// The pendrive's entries in the list, all ticked; then the summary.
void TrakcrateExportDialog::fillEntries()
{
    {
        QSignalBlocker b(playlists_);
        playlists_->clear();
        for (size_t i = 0; i < entries_.size(); ++i) {
            auto* it = new QListWidgetItem(QStringLiteral("%1  (%2 tracks)").arg(QString::fromStdString(entries_[i].name)).arg(entries_[i].tracks), playlists_);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(Qt::Checked);
            it->setData(Qt::UserRole, int(i));
        }
    }
    refreshSummary();
}

bool TrakcrateExportDialog::raiseLimits() const { return raiseLimits_->isChecked(); }
void TrakcrateExportDialog::setRaiseLimits(bool on) { raiseLimits_->setChecked(on); }
bool TrakcrateExportDialog::raiseLimitsVisible() const { return !raiseLimits_->isHidden(); }

QString TrakcrateExportDialog::settingsSet() const { return settingsSet_->currentData().toString(); }
bool TrakcrateExportDialog::replaceSettings() const { return replace_->isChecked(); }
void TrakcrateExportDialog::setReplaceSettings(bool on) { (on ? replace_ : addMissing_)->setChecked(true); }

void TrakcrateExportDialog::setSettingsSet(const QString& name)
{
    const int i = settingsSet_->findData(name);
    settingsSet_->setCurrentIndex(i >= 0 ? i : 0);
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

namespace {
struct DriveRow {
    QString text, mountPoint;
};

std::vector<DriveRow> scanDriveRows()
{
    std::vector<DriveRow> rows;
    for (const PhysicalPendrive& d : PendriveRegistry::scanPhysical()) {
        const trakcrate::UsbLink usb = trakcrate::usbLinkOfPath(d.mountPoint.toStdString());
        rows.push_back({QStringLiteral("%1  (%2 free%3)").arg(d.name).arg(QLocale().formattedDataSize(d.freeBytes))
                            .arg(!usb.found ? QString() : QStringLiteral(", ") + QString::fromStdString(usb.label) + (usb.downgraded() ? QStringLiteral(" - supports USB 3") : QString())),
                        d.mountPoint});
    }
    return rows;
}
} // namespace

void TrakcrateExportDialog::rescanDrives()
{
    const QString chosen = drives_->currentIndex() > 0 ? drives_->currentData().toString() : QString();
    auto show = [this, chosen](const std::vector<DriveRow>& rows) {
        drives_->clear();
        drives_->addItem(QStringLiteral("Mounted drives..."));
        for (const DriveRow& r : rows) drives_->addItem(r.text, r.mountPoint);
        const int again = chosen.isEmpty() ? -1 : drives_->findData(chosen);
        if (again > 0) drives_->setCurrentIndex(again);
        QStringList ticked;
        for (int i = 0; i < extraDrives_->count(); ++i)
            if (extraDrives_->item(i)->checkState() == Qt::Checked) ticked << extraDrives_->item(i)->data(Qt::UserRole).toString();
        QSignalBlocker b(extraDrives_);
        extraDrives_->clear();
        for (const DriveRow& r : rows) {
            auto* it = new QListWidgetItem(r.text, extraDrives_);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(ticked.contains(r.mountPoint) ? Qt::Checked : Qt::Unchecked);
            it->setData(Qt::UserRole, r.mountPoint);
        }
        syncExtraDrives();
    };
    if (!async_) {
        show(scanDriveRows());
        return;
    }
    drives_->clear();
    drives_->addItem(QStringLiteral("Looking for drives..."));
    const quint64 gen = ++drivesGen_;
    spawn(alive_, [this, gen, show] {
        auto rows = scanDriveRows();
        return std::function<void()>([this, gen, show, rows = std::move(rows)] { if (gen == drivesGen_) show(rows); });
    });
}

void TrakcrateExportDialog::detectedChosen(int index)
{
    if (index > 0) setDestination(drives_->itemData(index).toString());     // the chosen drive stays shown in the combo
}

// A ticked entry of the pendrive: its tracks, in the order set on the pendrive (reads every link: not for the GUI thread of a big pendrive).
static void resolveEntries(const trakcrate::Library& lib, const std::string& pendrive, const std::vector<trakcrate::Library::DriveEntry>& entries,
                           const std::vector<int>& idx, trakcrate::ExportOptions* o)
{
    for (int entry : idx)
        if (entry >= 0 && size_t(entry) < entries.size())
            o->lists.push_back({entries[size_t(entry)].name, lib.driveLibrary(pendrive).playlistTracks(entries[size_t(entry)].dir)});
}

trakcrate::ExportOptions TrakcrateExportDialog::options() const
{
    std::vector<int> idx;
    trakcrate::ExportOptions o = baseOptions(&idx);
    resolveEntries(lib_, pendrive_.toStdString(), entries_, idx, &o);
    return o;
}

trakcrate::ExportOptions TrakcrateExportDialog::baseOptions(std::vector<int>* entryIdx) const
{
    trakcrate::ExportOptions o;
    for (int i = 0; i < playlists_->count(); ++i) {
        if (playlists_->item(i)->checkState() != Qt::Checked) continue;
        if (pendrive_.isEmpty()) o.playlists.push_back(playlists_->item(i)->text().toStdString());
        else entryIdx->push_back(playlists_->item(i)->data(Qt::UserRole).toInt());
    }
    o.dest = dest_->text().trimmed().toStdString();
    o.allowUnanalysed = unanalysed_->isChecked();
    o.raiseLimits = raiseLimits_->isChecked();
    if (settingsSet() == builtInSettingsKey()) o.settingsBuiltIn = true;
    else o.settingsSet = settingsSet().toStdString();
    o.settingsPolicy = replace_->isChecked() ? trakcrate::ExportOptions::SettingsPolicy::Replace : trakcrate::ExportOptions::SettingsPolicy::AddMissing;
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
    return o;
}

std::vector<QString> TrakcrateExportDialog::tickedExtraDrives() const
{
    std::vector<QString> v;
    if (!physical_ || !extraDrives_) return v;
    const QString primary = QDir::cleanPath(dest_->text().trimmed());
    for (int i = 0; i < extraDrives_->count(); ++i) {
        const QListWidgetItem* it = extraDrives_->item(i);
        const QString mp = it->data(Qt::UserRole).toString();
        if (it->checkState() != Qt::Checked || QDir::cleanPath(mp) == primary) continue;
        v.push_back(mp);
    }
    return v;
}

void TrakcrateExportDialog::addExtraDrive(const QString& mountPoint, const QString& label)
{
    QSignalBlocker b(extraDrives_);
    auto* it = new QListWidgetItem(label, extraDrives_);
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(Qt::Unchecked);
    it->setData(Qt::UserRole, mountPoint);
    syncExtraDrives();
}

// The main destination is not also an additional destination: its row is hidden and unticked.
void TrakcrateExportDialog::syncExtraDrives()
{
    if (!extraDrives_) return;
    const QString primary = dest_->text().trimmed().isEmpty() ? QString() : QDir::cleanPath(dest_->text().trimmed());
    QSignalBlocker b(extraDrives_);
    for (int i = 0; i < extraDrives_->count(); ++i) {
        QListWidgetItem* it = extraDrives_->item(i);
        const bool same = !primary.isEmpty() && QDir::cleanPath(it->data(Qt::UserRole).toString()) == primary;
        it->setHidden(same);
        if (same) it->setCheckState(Qt::Unchecked);
    }
}

void TrakcrateExportDialog::selectExtraDrive(const QString& mountPoint, bool on)
{
    for (int i = 0; i < extraDrives_->count(); ++i)
        if (!extraDrives_->item(i)->isHidden() && extraDrives_->item(i)->data(Qt::UserRole).toString() == mountPoint) extraDrives_->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    refreshSummary();
}

std::vector<trakcrate::ExportOptions> TrakcrateExportDialog::allOptions() const
{
    std::vector<trakcrate::ExportOptions> all;
    const trakcrate::ExportOptions first = options();
    all.push_back(first);
    for (const QString& mp : tickedExtraDrives()) {
        trakcrate::ExportOptions o = first;
        o.dest = mp.toStdString();
        o.update = !trakcrate::exportConflicts(o.dest).empty();             // the drive already holds an export: refresh it
        all.push_back(std::move(o));
    }
    return all;
}

// What the checks found out about the export and the destination; made off the GUI thread when the dialog is async.
struct TrakcrateExportDialog::SummaryData {
    bool haveLists = false;
    trakcrate::ExportSummary s;
    bool pathExists = false, pathIsDir = false;
    std::vector<std::string> clash;
    bool usbFound = false, usbDowngraded = false;
    std::string usbLabel;
    QString storageName;
    qint64 avail = 0;
    struct Extra {
        QString mountPoint, name;
        bool update = false, usbDowngraded = false;
        qint64 avail = 0;
    };
    std::vector<Extra> extras;                          // the other drives ticked under "Simultaneous export destination(s)"
};

void TrakcrateExportDialog::setLoading()
{
    summary_->setText(QStringLiteral("<i>Loading...</i>"));
    ok_->setEnabled(false);
}

void TrakcrateExportDialog::refreshSummary()
{
    std::vector<int> idx;
    trakcrate::ExportOptions o = baseOptions(&idx);
    auto compute = [](const trakcrate::Library& lib, const trakcrate::ExportOptions& opts) {
        SummaryData d;
        d.haveLists = !opts.playlists.empty() || !opts.lists.empty();
        if (!d.haveLists) return d;
        d.s = trakcrate::summariseExport(lib, opts);
        if (opts.dest.empty()) return d;
        const QString path = QString::fromStdString(opts.dest.string());
        QFileInfo fi(path);
        while (!fi.exists() && !fi.isRoot() && fi.absoluteDir().exists() == false) fi = QFileInfo(fi.absolutePath());
        QDir existing(fi.exists() ? (fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath()) : fi.absolutePath());
        d.pathExists = QFileInfo(path).exists();
        d.pathIsDir = QDir(path).exists();
        if (d.pathExists && d.pathIsDir) d.clash = trakcrate::exportConflicts(opts.dest);      // other files on a stick are fine
        const trakcrate::UsbLink usb = trakcrate::usbLinkOfPath(existing.exists() ? existing.absolutePath().toStdString() : std::string());
        d.usbFound = usb.found;
        d.usbLabel = usb.label;
        d.usbDowngraded = usb.downgraded();
        QStorageInfo st(existing.exists() ? existing.absolutePath() : QDir::homePath());
        d.storageName = st.displayName();
        d.avail = st.bytesAvailable();
        return d;
    };
    const std::vector<QString> extraMounts = tickedExtraDrives();
    auto addExtras = [extraMounts](SummaryData& d) {
        for (const QString& mp : extraMounts) {
            SummaryData::Extra e;
            e.mountPoint = mp;
            e.name = QFileInfo(mp).fileName().isEmpty() ? mp : QFileInfo(mp).fileName();
            e.update = !trakcrate::exportConflicts(mp.toStdString()).empty();
            e.usbDowngraded = trakcrate::usbLinkOfPath(mp.toStdString()).downgraded();
            e.avail = QStorageInfo(mp).bytesAvailable();
            d.extras.push_back(std::move(e));
        }
    };
    if (!async_) {
        resolveEntries(lib_, pendrive_.toStdString(), entries_, idx, &o);
        SummaryData d = compute(lib_, o);
        addExtras(d);
        applySummary(d, o);
        return;
    }
    setLoading();
    const quint64 gen = ++summaryGen_;
    if (loadingEntries_) return;                                   // the entries arriving ask again
    const trakcrate::Library lib = lib_;
    const std::string pendrive = pendrive_.toStdString();
    const auto entries = entries_;
    spawn(alive_, [this, gen, lib, pendrive, entries, idx, o, compute, addExtras]() mutable {
        resolveEntries(lib, pendrive, entries, idx, &o);
        SummaryData d = compute(lib, o);
        addExtras(d);
        return std::function<void()>([this, gen, d = std::move(d), o = std::move(o)] { if (gen == summaryGen_) applySummary(d, o); });
    });
}

// Measures a location's rekordbox folders (off the GUI thread when async); the summary is made again when the answer arrives.
void TrakcrateExportDialog::ensureUsage(const QString& base)
{
    if (base.isEmpty() || usageCache_.contains(base) || usagePending_.contains(base)) return;
    if (!async_) {
        usageCache_.insert(base, measureRekordbox(base));
        return;
    }
    usagePending_.insert(base);
    spawn(alive_, [this, base] {
        const DirUsage u = measureRekordbox(base);
        return std::function<void()>([this, base, u] {
            usagePending_.remove(base);
            usageCache_.insert(base, u);
            if (!lastSummary_) return;
            const std::shared_ptr<SummaryData> keep = lastSummary_;                 // applySummary replaces lastSummary_
            const trakcrate::ExportOptions opts = lastOptions_;
            applySummary(*keep, opts);
        });
    });
}

// The space table rows: the chosen destination, then each ticked extra drive.
std::vector<TrakcrateExportDialog::LocationSpace> TrakcrateExportDialog::locationSpaces(const SummaryData& d, const trakcrate::ExportOptions& o) const
{
    const qint64 estimate = qint64(d.s.audioBytes + d.s.metadataBytes);
    auto make = [&](const QString& name, const QString& base, qint64 avail) {
        LocationSpace l;
        l.name = name;
        l.base = base;
        l.freeBytes = avail;
        const auto it = usageCache_.constFind(base);
        l.currentKnown = it != usageCache_.constEnd();
        if (l.currentKnown) {
            l.currentBytes = it->bytes;
            l.currentTracks = it->audioFiles;
        }
        l.requiredBytes = std::max<qint64>(0, estimate - l.currentBytes);
        return l;
    };
    std::vector<LocationSpace> v;
    if (!o.dest.empty()) v.push_back(make(d.storageName, QString::fromStdString(o.dest.string()), d.avail));
    for (const SummaryData::Extra& e : d.extras) v.push_back(make(e.name, e.mountPoint, e.avail));
    return v;
}

void TrakcrateExportDialog::applySummary(const SummaryData& d, const trakcrate::ExportOptions& o)
{
    lastSummary_ = std::make_shared<SummaryData>(d);
    lastOptions_ = o;
    QString html;
    const bool haveLists = d.haveLists;
    bool good = haveLists && !o.dest.empty();
    if (!haveLists) raiseLimits_->setVisible(false);
    if (!haveLists) html += QStringLiteral("Tick the playlists to put on the drive.<br>");
    if (haveLists) {
        const trakcrate::ExportSummary& s = d.s;
        html += QStringLiteral("%1 track(s), %2 of audio.<br>").arg(s.tracks).arg(QLocale().formattedDataSize(qint64(s.audioBytes)));
        raiseLimits_->setVisible(s.overDefaultLimits);                    // the offer appears only when the lower limits are hit
        if (!s.overDefaultLimits && raiseLimits_->isChecked()) { QSignalBlocker b(raiseLimits_); raiseLimits_->setChecked(false); }
        for (const std::string& p : s.problems) { good = false; html += QStringLiteral("<span style='color:#c00'>%1</span><br>").arg(QString::fromStdString(p).toHtmlEscaped()); }
        if (!o.dest.empty()) {
            if (d.pathExists && !d.pathIsDir) { good = false; html += QStringLiteral("<span style='color:#c00'>The destination is not a folder.</span><br>"); }
            else if (d.pathIsDir) {
                update_->setVisible(!d.clash.empty());
                if (!d.clash.empty()) {
                    QStringList names;
                    for (const std::string& n : d.clash) names << QString::fromStdString(n);
                    if (o.update) {
                        html += QStringLiteral("Updating the earlier export on the drive (%1): nothing is deleted.<br>").arg(names.join(QStringLiteral(", ")).toHtmlEscaped());
                    } else {
                        good = false;
                        html += QStringLiteral("<span style='color:#c00'>The destination already has %1 (an earlier export?). Tick Update to refresh it.</span><br>").arg(names.join(QStringLiteral(", ")).toHtmlEscaped());
                    }
                }
            }
            if (d.usbFound) html += QStringLiteral("Connected as %1.<br>").arg(QString::fromStdString(d.usbLabel).toHtmlEscaped());
            if (d.usbDowngraded)
                html += QStringLiteral("<span style='color:#b71c1c'><b>&#9888; This drive supports USB 3 but is connected as %1.</b> The socket, the cable or the "
                                       "drive's USB 3 contacts are not working: unplug it and plug it in again, or try another socket, before exporting.</span><br>")
                            .arg(QString::fromStdString(d.usbLabel).toHtmlEscaped());
            ensureUsage(QString::fromStdString(o.dest.string()));
        }
    }
    if (haveLists && !d.extras.empty()) {
        html += QStringLiteral("Also writing to %1 more drive(s):<br>").arg(d.extras.size());
        for (const SummaryData::Extra& e : d.extras) {
            html += QStringLiteral("&nbsp;&bull; %1: %2").arg(e.name.toHtmlEscaped(), e.update ? QStringLiteral("updating the earlier export") : QStringLiteral("new export"));
            ensureUsage(e.mountPoint);
            if (e.usbDowngraded) html += QStringLiteral(" <span style='color:#b71c1c'>- connected at lower USB speed</span>");
            html += QStringLiteral("<br>");
        }
    }
    if (o.dest.empty() || !d.pathIsDir) update_->setVisible(false);
    // room: what is still to be written against what is free. While a location is being measured the whole estimate counts, unless it holds an earlier export.
    spaces_.clear();
    if (haveLists) {
        spaces_ = locationSpaces(d, o);
        for (size_t i = 0; i < spaces_.size(); ++i) {
            const LocationSpace& l = spaces_[i];
            const bool main = !o.dest.empty() && i == 0;
            const bool holdsExport = main ? !d.clash.empty() : d.extras[i - (o.dest.empty() ? 0 : 1)].update;
            if (!l.currentKnown && holdsExport) continue;
            if (l.freeBytes < l.requiredBytes) {
                good = false;
                html += main ? QStringLiteral("<span style='color:#c00'>Not enough space.</span><br>")
                               : QStringLiteral("<span style='color:#c00'>Not enough space on %1.</span><br>").arg(l.name.toHtmlEscaped());
            }
        }
    }
    summary_->setText(html);
    showSpaces();
    good_ = good;
    updateOkEnabled();
}

void TrakcrateExportDialog::updateOkEnabled() { ok_->setEnabled(good_ && !deleting()); }

QString TrakcrateExportDialog::deleteConfirmText(const QString& base, qint64 currentBytes)
{
    QString t = QStringLiteral("Delete all data from:\n%1\n%2\n").arg(QDir(base).filePath(QStringLiteral("PIONEER")), QDir(base).filePath(QStringLiteral("Contents")));
    if (currentBytes > (qint64(1) << 30)) t += QStringLiteral("\nThis may take a while.");
    return t;
}

// Deleting runs on a worker; until it is done Export and this location's Clean up and Delete stay disabled. Other locations can delete at the same time.
void TrakcrateExportDialog::startDelete(const QString& base)
{
    if (deletingBases_.contains(base)) return;
    const auto cached = usageCache_.constFind(base);
    const QString text = deleteConfirmText(base, cached == usageCache_.constEnd() ? 0 : cached->bytes);
    const bool yes = confirm_ ? confirm_(text)
                              : QMessageBox::question(this, QStringLiteral("Delete"), text, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
    if (!yes) return;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    deletingBases_.insert(base, cancel);
    showSpaces();
    updateOkEnabled();
    spawn(alive_, [this, base, cancel, deleter = deleter_] {
        deleter(base, *cancel);
        return std::function<void()>([this, base] {
            deletingBases_.remove(base);
            usageCache_.remove(base);
            refreshSummary();                                                // the free space and the sizes again
            showSpaces();
            updateOkEnabled();
        });
    });
}

void TrakcrateExportDialog::done(int result)
{
    for (const auto& cancel : deletingBases_) cancel->store(true);
    QDialog::done(result);
}

static QString trashDir(const QString& base) { return QDir(base).filePath(QStringLiteral(".Trash-%1").arg(unsigned(getuid()))); }

// a plain folder with something in it
static bool trashHasEntries(const QString& base)
{
    const QFileInfo fi(trashDir(base));
    return fi.isDir() && !fi.isSymLink() && !QDir(fi.absoluteFilePath()).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
}

// Everything in dir, but not dir itself.
static bool removeContents(const QString& dir, const std::atomic<bool>& cancel)
{
    bool ok = true;
    const QDir d(dir);
    for (const QFileInfo& e : d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
        if (cancel.load()) return false;
        if (e.isDir() && !e.isSymLink()) {
            ok = removeContents(e.absoluteFilePath(), cancel) && ok;
            ok = d.rmdir(e.fileName()) && ok;
        } else {
            ok = QFile::remove(e.absoluteFilePath()) && ok;
        }
    }
    return ok;
}

bool TrakcrateExportDialog::deleteRegenerable(const QString& base, const std::atomic<bool>& cancel)
{
    bool ok = true;
    for (const char* sub : {"PIONEER/USBANLZ", "PIONEER/rekordbox", "PIONEER/Artwork", "Contents"}) {
        const QFileInfo fi(QDir(base).filePath(QLatin1String(sub)));
        if (!fi.isDir() || fi.isSymLink()) continue;
        ok = removeContents(fi.absoluteFilePath(), cancel) && ok;
        if (cancel.load()) return false;
    }
    return ok;
}

bool TrakcrateExportDialog::emptyTrash(const QString& base)
{
    const QFileInfo fi(trashDir(base));
    if (!fi.isDir() || fi.isSymLink()) return false;                         // never follow a link out of the drive
    bool ok = true;
    const QDir trash(fi.absoluteFilePath());
    for (const QFileInfo& e : trash.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
        if (e.isDir() && !e.isSymLink()) ok = QDir(e.absoluteFilePath()).removeRecursively() && ok;
        else ok = QFile::remove(e.absoluteFilePath()) && ok;
    }
    return ok;
}

void TrakcrateExportDialog::startEmptyTrash(const QString& base)
{
    if (trashing_.contains(base)) return;
    const QString text = QStringLiteral("Empty the trash on the drive?\n%1\n").arg(trashDir(base));
    const bool yes = confirm_ ? confirm_(text)
                              : QMessageBox::question(this, QStringLiteral("Empty trash"), text, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
    if (!yes) return;
    trashing_.insert(base);
    showSpaces();
    updateOkEnabled();
    spawn(alive_, [this, base] {
        emptyTrash(base);
        return std::function<void()>([this, base] {
            trashing_.remove(base);
            refreshSummary();                                                // the free space again
            showSpaces();
            updateOkEnabled();
        });
    });
}

bool TrakcrateExportDialog::cleanUpAfterExport(const QString& base) const
{
    for (const SpaceBlock& b : blocks_)
        if (b.base == base) return b.after->isChecked();
    return false;
}

// One block per location. The blocks are kept while the locations stay the same, so the table does not flicker as the sizes arrive.
void TrakcrateExportDialog::showSpaces()
{
    auto size = [](qint64 bytes) { return QLocale().formattedDataSize(bytes, 2, QLocale::DataSizeIecFormat); };
    bool same = blocks_.size() == spaces_.size();
    for (size_t i = 0; same && i < blocks_.size(); ++i) same = blocks_[i].base == spaces_[i].base;
    if (!same) {
        for (SpaceBlock& b : blocks_) delete b.box;
        blocks_.clear();
        for (const LocationSpace& l : spaces_) {
            SpaceBlock b;
            b.base = l.base;
            b.box = new QFrame;
            static_cast<QFrame*>(b.box)->setFrameShape(QFrame::StyledPanel);
            auto* grid = new QGridLayout(b.box);
            b.title = new QLabel;
            QFont f = b.title->font();
            f.setBold(true);
            b.title->setFont(f);
            b.title->setTextInteractionFlags(Qt::TextSelectableByMouse);
            grid->addWidget(b.title, 0, 0, 1, 2);
            auto row = [&](int r, const char* label, const char* name) {
                grid->addWidget(new QLabel(QLatin1String(label)), r, 0);
                auto* v = new QLabel;
                v->setObjectName(QLatin1String(name));
                grid->addWidget(v, r, 1);
                return v;
            };
            b.free = row(1, "Free", "freeValue");
            b.current = row(2, "Current size", "currentValue");
            b.required = row(3, "Required", "requiredValue");
            auto* buttons = new QHBoxLayout;
            b.cleanUp = new QPushButton(QStringLiteral("Clean up"));
            b.cleanUp->setObjectName(QStringLiteral("cleanUp"));
            b.cleanUp->setToolTip(QStringLiteral("Delete the tracks in Contents/ that no export refers to"));
            b.after = new QCheckBox;
            b.after->setObjectName(QStringLiteral("cleanUpAfter"));
            b.after->setToolTip(QStringLiteral("Clean up after export"));
            b.after->setAccessibleName(QStringLiteral("Clean up after export"));
            b.del = new QPushButton(QStringLiteral("Delete"));
            b.del->setObjectName(QStringLiteral("deleteData"));
            b.del->setToolTip(QStringLiteral("Delete all the exported data from this drive: the tracks, analysis, artwork and database, so there is room for a new export"));
            connect(b.del, &QPushButton::clicked, this, [this, base = l.base] { startDelete(base); });
            b.emptyTrash = new QPushButton(QStringLiteral("Empty trash"));
            b.emptyTrash->setObjectName(QStringLiteral("emptyTrash"));
            connect(b.emptyTrash, &QPushButton::clicked, this, [this, base = l.base] { startEmptyTrash(base); });
            for (QWidget* w : {static_cast<QWidget*>(b.cleanUp), static_cast<QWidget*>(b.after), static_cast<QWidget*>(b.del), static_cast<QWidget*>(b.emptyTrash)}) buttons->addWidget(w);
            buttons->addStretch(1);
            grid->addLayout(buttons, 4, 0, 1, 2);
            spacesBox_->layout()->addWidget(b.box);
            blocks_.push_back(b);
        }
    }
    for (size_t i = 0; i < blocks_.size(); ++i) {
        const LocationSpace& l = spaces_[i];
        SpaceBlock& b = blocks_[i];
        b.title->setText(QStringLiteral("%1 - %2").arg(l.name.isEmpty() ? QFileInfo(l.base).fileName() : l.name, l.base));
        b.free->setText(size(l.freeBytes));
        b.current->setText(l.currentKnown ? QStringLiteral("%1 (%2 tracks)").arg(size(l.currentBytes)).arg(l.currentTracks) : QStringLiteral("calculating..."));
        b.required->setText(size(l.requiredBytes));
        b.emptyTrash->setEnabled(!trashing_.contains(l.base) && trashHasEntries(l.base));
        const bool busy = deletingBases_.contains(l.base);
        b.cleanUp->setEnabled(!busy);
        b.del->setEnabled(!busy);
    }
    spacesBox_->setVisible(!blocks_.empty());
}

} // namespace vdj
