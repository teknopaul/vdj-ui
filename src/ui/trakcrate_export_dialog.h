#ifndef TRAKCRATE_EXPORT_DIALOG_H
#define TRAKCRATE_EXPORT_DIALOG_H

#include <QDialog>

#include "trakcrate/exporter.h"
#include "trakcrate/library.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;

namespace vdj {

// Asks what to put on a drive and where: the playlists, an empty destination folder (mounted sticks are offered), and, under "Advanced /
// compatibility testing", which optional pieces to include. Shows what the export involves and whether there is room. The panel runs it.
class TrakcrateExportDialog : public QDialog {
    Q_OBJECT

public:
    TrakcrateExportDialog(const trakcrate::Library& lib, QWidget* parent = nullptr);

    // The options chosen. The caller adds the template database and the artwork encoder.
    trakcrate::ExportOptions options() const;
    bool registerAsVirtualPendrive() const;
    QString virtualPendriveName() const;
    void setDestination(const QString& dir);
    void selectPlaylist(const QString& name, bool on = true);
    // Export a user defined pendrive: its entries (ticked here, in the pendrive's order) replace the playlist list. physical = write to a mounted
    // stick: the destination is chosen from the list of mounted drives only, it cannot be typed or browsed.
    void setPendrive(const QString& drive, bool physical);

private slots:
    void browse();
    void detectedChosen(int index);
    void refreshSummary();

private:
    trakcrate::Library lib_;
    QListWidget* playlists_;
    QString pendrive_;
    QPushButton* browse_ = nullptr;
    QLineEdit* dest_;
    QComboBox* drives_;
    QLabel* summary_;
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
