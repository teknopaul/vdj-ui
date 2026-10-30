#ifndef TRAKCRATE_RBIMPORT_DIALOG_H
#define TRAKCRATE_RBIMPORT_DIALOG_H

#include <QDialog>

#include "trakcrate/library.h"
#include "trakcrate/rb_import.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

namespace vdj {

// Asks which rekordbox pendrive to import and what to take from it. Mounted drives are listed when the dialog opens; nothing watches for a drive
// being plugged in and nothing pops up by itself.
class TrakcrateRbImportDialog : public QDialog {
    Q_OBJECT

public:
    // lib (may be null): playlists and folders it already has are listed unticked, ticking one adds only the tracks the library lacks.
    explicit TrakcrateRbImportDialog(const trakcrate::Library* lib = nullptr, QWidget* parent = nullptr);

    void setFolder(const QString& folder);
    QString folder() const;
    trakcrate::RbImportOptions options() const;

private slots:
    void browse();
    void folderEdited();

private:
    trakcrate::Library lib_;
    bool haveLib_ = false;
    QComboBox* drives_;
    QLineEdit* folder_;
    QLabel* status_;
    QLabel* playlistNote_;
    QTreeWidget* lists_;
    QCheckBox *playlistFolders_, *loosePaths_;
    void fillPlaylists();
    QCheckBox *playlists_, *grid_, *cues_, *waveform_, *artwork_, *anlz_, *settings_, *device_, *profile_, *link_, *dryRun_;
    QPushButton* ok_ = nullptr;
};

} // namespace vdj

#endif
