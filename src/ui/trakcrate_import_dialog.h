#ifndef TRAKCRATE_IMPORT_DIALOG_H
#define TRAKCRATE_IMPORT_DIALOG_H

#include <QDialog>

#include "trakcrate/importer.h"

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace vdj {

// Asks what to import: a folder of bare audio files, the playlist name (default: the folder's name), recursion, copy/move/link and
// what to do when the playlist already exists. The panel runs the import.
class TrakcrateImportDialog : public QDialog {
    Q_OBJECT

public:
    explicit TrakcrateImportDialog(QWidget* parent = nullptr);

    void setFolder(const QString& folder);
    QString folder() const;
    trakcrate::ImportOptions options() const;

private slots:
    void browse();
    void folderEdited();

private:
    QLineEdit* folder_;
    QLineEdit* playlist_;
    QLineEdit* masterFolder_;
    QCheckBox* noPlaylist_;
    QCheckBox* recursive_;
    QCheckBox* dryRun_;
    QCheckBox* analyse_;
    QComboBox* mode_;
    QComboBox* ifExists_;
    bool playlistEdited_ = false;
};

} // namespace vdj

#endif
