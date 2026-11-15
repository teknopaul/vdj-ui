#ifndef MIX_RENDER_DIALOG_H
#define MIX_RENDER_DIALOG_H

#include "trakcrate/mix_model.h"
#include "trakcrate/stretch_cache.h"

#include <QDialog>

#include <atomic>
#include <thread>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace vdj {

class ConfigManager;

// "Render MP3": name, author, cover and folder of the mix file, the file name and length it will get, warnings, then the render on a worker thread
// with a progress bar and cancel. Name, author and folder are remembered (EditPrefs).
class MixRenderDialog : public QDialog {
    Q_OBJECT

public:
    MixRenderDialog(const trakcrate::Mix& mix, const trakcrate::fs::path& libraryRoot, ConfigManager* config, trakcrate::StretchCache* cache, QWidget* parent = nullptr);
    ~MixRenderDialog() override;

    QLineEdit* nameEdit() const { return name_; }
    QLineEdit* authorEdit() const { return author_; }
    QLineEdit* imageEdit() const { return image_; }
    QLineEdit* folderEdit() const { return folder_; }
    QLabel* plannedLabel() const { return planned_; }
    QLabel* warningsLabel() const { return warnings_; }
    QLabel* resultLabel() const { return result_; }
    QProgressBar* progressBar() const { return progress_; }
    QPushButton* renderButton() const { return render_; }
    QStringList warnings() const { return warningList_; }
    bool isRendering() const { return running_; }
    QString outputFile() const { return outFile_; }
    // The mix as the dialog has it: name, author and image filled in.
    trakcrate::Mix resultMix() const;

    void startRender();
    void cancelRender();
    void waitForFinish();                          // for tests

signals:
    void rendered(const QString& file);

protected:
    void reject() override;

private:
    void refreshPlan();
    void finish(bool ok, const QString& message, const QString& file);

    trakcrate::Mix mix_;
    trakcrate::fs::path root_;
    ConfigManager* config_;
    trakcrate::StretchCache* cache_;
    std::vector<trakcrate::TrackFacts> facts_;
    QString factsError_;
    QStringList warningList_;
    QLineEdit *name_, *author_, *image_, *folder_;
    QLabel *thumb_, *planned_, *warnings_, *result_, *stage_;
    QProgressBar* progress_;
    QPushButton *render_, *close_;
    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    QString outFile_;
};

} // namespace vdj

#endif
