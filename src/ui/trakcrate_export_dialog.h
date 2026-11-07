#ifndef TRAKCRATE_EXPORT_DIALOG_H
#define TRAKCRATE_EXPORT_DIALOG_H

#include <QDialog>

#include <atomic>
#include <functional>
#include <memory>
#include <QHash>
#include <QSet>
#include <vector>

#include "trakcrate/exporter.h"
#include "ui/dir_usage.h"
#include "trakcrate/library.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QTimer;

namespace vdj {

// Asks what to put on a drive and where: the playlists, an empty destination folder (mounted sticks are offered), and, under "Advanced
// Export Options", which optional pieces to include. Shows what the export involves and whether there is room: the
// warnings, then a table per location (free, current size, still required). From the top: destination, the drives (physical device mode only),
// Update, the status, the folders to include, User Settings, Advanced Export Options. The panel runs it.
class TrakcrateExportDialog : public QDialog {
    Q_OBJECT

public:
    // async: the slow parts (the drives list, the pendrive's entries, the analysis behind the summary) run on a worker thread: the dialog opens at
    // once showing "Loading..." with Export disabled, and Export is enabled when the checks are done and found nothing wrong.
    TrakcrateExportDialog(const trakcrate::Library& lib, QWidget* parent = nullptr, bool async = false);
    ~TrakcrateExportDialog() override;

    // The options chosen. The caller adds the template database and the artwork encoder.
    trakcrate::ExportOptions options() const;
    // Every export to start: options() for the chosen destination, then the same export for each other mounted drive ticked under "Simultaneous export destination(s)"
    // (physical device mode). A drive that already holds an export is updated, an empty one gets a fresh export.
    std::vector<trakcrate::ExportOptions> allOptions() const;
    void addExtraDrive(const QString& mountPoint, const QString& label);     // offer one more drive under "Simultaneous export destination(s)" (the scan adds the mounted ones)
    void selectExtraDrive(const QString& mountPoint, bool on = true);       // tick a mounted drive under "Simultaneous export destination(s)"
    void setDestination(const QString& dir);
    void selectPlaylist(const QString& name, bool on = true);
    // Export a user defined pendrive: its entries (ticked here, in the pendrive's order) replace the playlist list. physical = write to a mounted
    // stick: the destination is chosen from the list of mounted drives only, it cannot be typed or browsed.
    void setPendrive(const QString& drive, bool physical);
    // The settings set chosen for the player files: "" = the library's default set, kBuiltInSettings = the compiled in files.
    QString settingsSet() const;
    void setSettingsSet(const QString& name);
    bool raiseLimits() const;
    void setRaiseLimits(bool on);
    bool raiseLimitsVisible() const;                  // offered only when the pendrive is above the limits of older decks
    bool replaceSettings() const;
    void setReplaceSettings(bool on);
    // One place the export goes to, as the space table shows it (sizes in bytes). required = what is still to be written: never negative.
    struct LocationSpace {
        QString name, base;
        qint64 freeBytes = 0, currentBytes = 0, requiredBytes = 0;
        int currentTracks = 0;
        bool currentKnown = false;       // the folders are measured on a worker thread
    };
    const std::vector<LocationSpace>& spaces() const { return spaces_; }
    // The "Clean up after export" box of a location in the space table. Nothing acts on it yet.
    bool cleanUpAfterExport(const QString& base) const;
    // What Delete asks and does. Both can be replaced (tests): confirm shows the question and returns whether to go on; the deleter runs on a worker
    // thread and should stop soon after cancel is set. The default is deleteRegenerable().
    using Deleter = std::function<void(const QString& base, const std::atomic<bool>& cancel)>;
    void setDeleter(Deleter d) { deleter_ = std::move(d); }
    void setConfirm(std::function<bool(const QString&)> c) { confirm_ = std::move(c); }
    static QString deleteConfirmText(const QString& base, qint64 currentBytes);
    bool deleting() const { return !deletingBases_.isEmpty() || !trashing_.isEmpty(); }
    // Removes what is in the location's .Trash-<uid> folder (not the folder itself). false: it is not a plain folder, or something was left.
    static bool emptyTrash(const QString& base);
    // Deletes what an export can write again: the contents of PIONEER/USBANLZ, PIONEER/rekordbox, PIONEER/Artwork and Contents under base. Links are
    // removed, never followed; other files on the drive are left alone. Stops early when cancel is set. false: something could not be removed.
    static bool deleteRegenerable(const QString& base, const std::atomic<bool>& cancel);
    void done(int result) override;                                          // closing the dialog stops any deleting
    static QString builtInSettingsKey() { return QStringLiteral("\x01builtin"); }

private slots:
    void browse();
    void detectedChosen(int index);
    void rescanDrives();
    void refreshSummary();

private:
    struct SummaryData;
    std::vector<QString> tickedExtraDrives() const;                          // mount points, without the chosen destination
    trakcrate::ExportOptions baseOptions(std::vector<int>* entryIdx) const;     // the widgets as options; a pendrive's entries as indexes into entries_
    void fillEntries();
    void startDelete(const QString& base);
    void startEmptyTrash(const QString& base);
    void updateOkEnabled();                                                  // Export: the checks are fine and nothing is being deleted
    void showSpaces();                                                       // the space table from spaces_
    void ensureUsage(const QString& base);                                   // measure the rekordbox folders of a location, once
    std::vector<LocationSpace> locationSpaces(const SummaryData& d, const trakcrate::ExportOptions& o) const;
    void syncExtraDrives();                                                  // the chosen destination is not offered as an extra one
    void setLoading();
    void applySummary(const SummaryData& d, const trakcrate::ExportOptions& o);

    bool async_ = false;
    bool loadingEntries_ = false;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    quint64 summaryGen_ = 0, drivesGen_ = 0;
    std::vector<trakcrate::Library::DriveEntry> entries_;
    trakcrate::Library lib_;
    QListWidget* playlists_;
    QString pendrive_;
    QPushButton* browse_ = nullptr;
    QLineEdit* dest_;
    QComboBox* drives_;
    QWidget* physicalBox_ = nullptr;                 // the drives combo and the list below it: physical device mode only
    QWidget* extraBox_ = nullptr;                    // "Simultaneous export destination(s)": more drives for the same export, physical device mode only
    QListWidget* extraDrives_ = nullptr;
    bool physical_ = false;
    QHash<QString, DirUsage> usageCache_;             // by location
    QSet<QString> usagePending_;
    std::shared_ptr<SummaryData> lastSummary_;
    trakcrate::ExportOptions lastOptions_;
    std::vector<LocationSpace> spaces_;
    struct SpaceBlock {
        QString base;
        QWidget* box;
        QLabel *title, *free, *current, *required;
        QPushButton *cleanUp, *del, *emptyTrash;
        QCheckBox* after;
    };
    QWidget* spacesBox_ = nullptr;
    bool good_ = false;
    QSet<QString> trashing_;                                                 // the locations whose trash is being emptied
    QHash<QString, std::shared_ptr<std::atomic<bool>>> deletingBases_;       // the locations being deleted from, with their cancel flags
    Deleter deleter_ = [](const QString& base, const std::atomic<bool>& cancel) { deleteRegenerable(base, cancel); };
    std::function<bool(const QString&)> confirm_;
    std::vector<SpaceBlock> blocks_;
    QCheckBox* update_;
    QComboBox* settingsSet_;
    QRadioButton *addMissing_, *replace_;
    QLabel* summary_;
    QCheckBox* raiseLimits_;
    QGroupBox* advanced_;
    QCheckBox *settings_, *artwork_, *pvbr_, *pwv4_, *pwv5_, *manifest_, *verify_, *unanalysed_;
    QPushButton* ok_;
    QTimer* refreshTimer_;
};

} // namespace vdj

#endif
