#ifndef SAMPLE_CHOOSER_H
#define SAMPLE_CHOOSER_H

#include <QDialog>
#include <QWidget>
#include <functional>

class QLabel;
class QToolButton;
class QTreeView;
class QPushButton;

namespace vdj {

class SampleTreeModel;

// The sample of a module: its file name, a button to pick another one (a dialog with the folder tree and a preview button)
// and up / down to step through the .wav files of the same folder (ASCIIbetical). Stepping emits sampleChanged at once;
// the sampler plays the new one from its next hit.
class SampleChooser : public QWidget {
    Q_OBJECT

public:
    using Preview = std::function<void(const QString& path)>;

    explicit SampleChooser(QWidget* parent = nullptr);

    QString sample() const { return path_; }
    void setSample(const QString& path);             // does not emit
    void setFolder(const QString& folder) { folder_ = folder; }   // where the dialog opens when there is no sample yet
    void setPreview(Preview p) { preview_ = std::move(p); }

    QToolButton* chooseButton() const { return choose_; }
    QToolButton* upButton() const { return up_; }
    QToolButton* downButton() const { return down_; }
    QLabel* nameLabel() const { return name_; }
    // What the arrows do: the previous (-1) / next (+1) .wav of the folder; false when there is none to go to.
    bool stepSample(int dir);

signals:
    void sampleChanged(const QString& path);

private:
    void choose();
    void updateLabel();

    QString path_;
    QString folder_;
    Preview preview_;
    QLabel* name_;
    QToolButton* choose_;
    QToolButton* up_;
    QToolButton* down_;
};

// Pick a .wav from the tree of `folder`; Preview plays the highlighted one (double click too).
class SampleBrowserDialog : public QDialog {
    Q_OBJECT

public:
    SampleBrowserDialog(const QString& folder, const QString& current, SampleChooser::Preview preview, QWidget* parent = nullptr);
    QString selected() const;
    QTreeView* tree() const { return tree_; }
    QPushButton* previewButton() const { return previewButton_; }

private:
    void previewCurrent();

    SampleTreeModel* model_;
    QTreeView* tree_;
    QPushButton* previewButton_;
    QPushButton* ok_;
    SampleChooser::Preview preview_;
};

} // namespace vdj

#endif // SAMPLE_CHOOSER_H
