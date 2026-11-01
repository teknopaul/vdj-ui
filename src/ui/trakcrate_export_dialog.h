#ifndef TRAKCRATE_EXPORT_DIALOG_H
#define TRAKCRATE_EXPORT_DIALOG_H

#include <QDialog>

#include <atomic>
#include <memory>
#include <vector>

#include "trakcrate/exporter.h"
#include "trakcrate/library.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QRadioButton;
class QTimer;

namespace vdj {

// Asks what to put on a drive and where: the playlists, an empty destination folder (mounted sticks are offered), and, under "Advanced /
// compatibility testing", which optional pieces to include. Shows what the export involves and whether there is room. The panel runs it.
class TrakcrateExportDialog : public QDialog {
    Q_OBJECT

public:
    // async: the slow parts (the drives list, the pendrive's entries, the analysis behind the summary) run on a worker thread: the dialog opens at
    // once showing "Loading..." with Export disabled, and Export is enabled when the checks are done and found nothing wrong.
    TrakcrateExportDialog(const trakcrate::Library& lib, QWidget* parent = nullptr, bool async = false);
    ~TrakcrateExportDialog() override;

    // The options chosen. The caller adds the template database and the artwork encoder.
    trakcrate::ExportOptions options() const;
    // Every export to start: options() for the chosen destination, then the same export for each other mounted drive ticked under "Also write to"
    // (physical device mode). A drive that already holds an export is updated, an empty one gets a fresh export.
    std::vector<trakcrate::ExportOptions> allOptions() const;
    bool registerAsVirtualPendrive() const;
    QString virtualPendriveName() const;
    void addExtraDrive(const QString& mountPoint, const QString& label);     // offer one more drive under "Also write to" (the scan adds the mounted ones)
    void selectExtraDrive(const QString& mountPoint, bool on = true);       // tick a mounted drive under "Also write to"
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
    QWidget* extraBox_ = nullptr;                    // "Also write to": more drives for the same export, physical device mode only
    QListWidget* extraDrives_ = nullptr;
    bool physical_ = false;
    QCheckBox* update_;
    QComboBox* settingsSet_;
    QRadioButton *addMissing_, *replace_;
    QLabel* summary_;
    QCheckBox* raiseLimits_;
    QGroupBox* advanced_;
    QCheckBox *settings_, *artwork_, *pvbr_, *pwv4_, *pwv5_, *manifest_, *verify_, *unanalysed_;
    QLineEdit* realDrive_;
    QCheckBox *exportExt_, *rbfltr_, *usbmng_, *djprofile_;
    QCheckBox* register_;
    QPushButton* ok_;
    QTimer* refreshTimer_;
};

} // namespace vdj

#endif
