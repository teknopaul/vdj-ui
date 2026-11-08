#ifndef TRAKCRATE_EDIT_LOOP_DIALOG_H
#define TRAKCRATE_EDIT_LOOP_DIALOG_H

#include <QDialog>
#include <QTemporaryDir>

#include <atomic>

#include "audio/audio_player.h"
#include "trakcrate/edit_job.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QThread;

namespace vdj {

class ConfigManager;

// Cuts a loop out of the original for Acid: a name (asked for here), the length in beats, a click avoidance algo for the seam, format and folder.
// "Preview" plays the sample repeating. The folder chosen is remembered and is the default from then on. The original and the library are not touched.
class EditLoopDialog : public QDialog {
    Q_OBJECT

public:
    EditLoopDialog(const trakcrate::fs::path& trackDir, const trakcrate::BeatGrid& grid, int startBeat, int lenBeats, trakcrate::EditRenderCache* cache,
                   ConfigManager* config, QWidget* parent = nullptr);
    ~EditLoopDialog() override;

    QLineEdit* nameEdit() const { return name_; }
    QComboBox* lengthBox() const { return length_; }
    QComboBox* algoBox() const { return algo_; }
    QComboBox* paramBox() const { return param_; }
    QComboBox* formatBox() const { return format_; }
    QComboBox* bitrateBox() const { return bitrate_; }
    QLineEdit* folderEdit() const { return folder_; }
    QLabel* fileLabel() const { return file_; }
    QLabel* resultLabel() const { return result_; }
    QPushButton* exportButton() const { return export_; }
    QPushButton* previewButton() const { return preview_; }
    AudioPlayer* player() { return &player_; }
    bool isBusy() const { return thread_ != nullptr; }

public slots:
    void startExport();
    void startPreview();

signals:
    void exported(const QString& file);
    void failed(const QString& message);
    void previewReady();

private slots:
    void workDone(bool ok, const QString& error, const QString& file, bool preview);

private:
    trakcrate::LoopExportParams params() const;
    void fillParam();
    void updateLabels();

    trakcrate::fs::path trackDir_;
    trakcrate::BeatGrid grid_;
    int startBeat_;
    trakcrate::EditRenderCache* cache_;
    ConfigManager* config_;
    QTemporaryDir tmp_;
    AudioPlayer player_;
    std::atomic<bool> cancel_{false};
    QThread* thread_ = nullptr;
    QLineEdit* name_;
    QComboBox* length_;
    QComboBox* algo_;
    QComboBox* param_;
    QComboBox* format_;
    QComboBox* bitrate_;
    QLineEdit* folder_;
    QPushButton* choose_;
    QLabel* file_;
    QLabel* result_;
    QPushButton* preview_;
    QPushButton* export_;
};

} // namespace vdj

#endif
