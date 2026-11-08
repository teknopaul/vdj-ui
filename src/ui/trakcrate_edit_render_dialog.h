#ifndef TRAKCRATE_EDIT_RENDER_DIALOG_H
#define TRAKCRATE_EDIT_RENDER_DIALOG_H

#include <QDialog>

#include <atomic>

#include "trakcrate/edit_job.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QThread;

namespace vdj {

class ConfigManager;
class EditPrefs;

// Saves the edit: as a new track of the library next to the original (the default; analysed, and put in the playlist named like the folder when there is
// one), or as a file in a folder. Format, bitrate, author and folder come from and go back to the remembered preferences (EditPrefs) when OK is pressed.
// The work runs on a worker thread; errors leave the dialog open.
class EditRenderDialog : public QDialog {
    Q_OBJECT

public:
    // lib may be null (then only "Save file only" is offered).
    EditRenderDialog(const trakcrate::EditRequest& req, trakcrate::EditRenderCache* cache, const trakcrate::Library* lib, ConfigManager* config, QWidget* parent = nullptr);
    ~EditRenderDialog() override;

    QRadioButton* libraryRadio() const { return library_; }
    QRadioButton* fileRadio() const { return file_; }
    QComboBox* formatBox() const { return format_; }
    QComboBox* bitrateBox() const { return bitrate_; }
    QLineEdit* authorEdit() const { return author_; }
    QLineEdit* folderEdit() const { return folder_; }
    QLabel* nameLabel() const { return name_; }
    QLabel* targetLabel() const { return target_; }
    QLabel* resultLabel() const { return result_; }
    QPushButton* okButton() const { return ok_; }
    bool isBusy() const { return thread_ != nullptr; }
    QString createdTrackDir() const { return createdDir_; }

public slots:
    void startRender();                                  // what OK does

signals:
    void created(const QString& trackDir, const QString& message);   // the edit exists; trackDir is empty for a file only edit
    void failed(const QString& message);

private slots:
    void finished(bool ok, const QString& error, const QString& trackDir, const QString& file, const QString& playlist, int index, const QString& note);

private:
    trakcrate::EditOutputParams params() const;
    void updateLabels();
    void savePrefs();

    trakcrate::EditRequest req_;
    trakcrate::EditRenderCache* cache_;
    const trakcrate::Library* lib_;
    ConfigManager* config_;
    std::atomic<bool> cancel_{false};
    QThread* thread_ = nullptr;
    QString createdDir_;
    QRadioButton* library_;
    QRadioButton* file_;
    QComboBox* format_;
    QComboBox* bitrate_;
    QLineEdit* author_;
    QLineEdit* folder_;
    QPushButton* choose_;
    QLabel* name_;
    QLabel* target_;
    QLabel* result_;
    QPushButton* ok_;
    QPushButton* cancelButton_;
};

} // namespace vdj

#endif
