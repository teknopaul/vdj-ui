#ifndef ARTWORK_VIEW_H
#define ARTWORK_VIEW_H

#include <QDateTime>
#include <QString>
#include <QWidget>

class QFileSystemWatcher;
class QLabel;
class QMenu;
class QTimer;
class QToolButton;

namespace vdj {

class ConfigManager;

// The artwork of one trakcrate track (doc/trakcrate/track-editor.md, "Artwork"): a picture (click to enlarge), Replace (file picker or drop an image
// file on it), Remove, Re-extract from the audio tag, Restore (the one level backup in .trakcrate-history/), and Open in editor, with the editors the
// desktop offers for JPEG/PNG. An editor that saves in place (or by renaming a temporary file over it) is noticed and the picture reloads, and the
// artwork is marked "edited".
class ArtworkView : public QWidget {
    Q_OBJECT

public:
    explicit ArtworkView(QWidget* parent = nullptr);

    void setConfig(ConfigManager* config) { config_ = config; }
    void setTrack(const QString& trackDir);          // empty: none
    QString trackDir() const { return dir_; }
    bool hasArtwork() const { return !file_.isEmpty(); }

    // The actions of the buttons, for code and tests. True on success; err (if given) says why not.
    bool replaceWith(const QString& imagePath, QString* err = nullptr);
    bool removeArtwork(QString* err = nullptr);
    bool reextract(QString* err = nullptr);
    bool restore(QString* err = nullptr);
    bool openInEditor(const QString& exec, QString* err = nullptr);   // exec: a .desktop Exec= line or a custom command with %f
    QString rememberedEditor() const;                                 // the Exec= line chosen last, empty if none

    // One look at the file: a changed modification time reloads the picture and marks it edited. Called by the file watcher and a timer.
    void checkExternalChange();

    QLabel* picture() const { return picture_; }
    QToolButton* editorButton() const { return editor_; }

signals:
    void artworkChanged(const QString& trackDir);     // the artwork file changed (any way)
    void status(const QString& message);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void reload();
    void rebuildEditorMenu();
    void enlarge();
    void changed(const QString& message);
    void watch();

    ConfigManager* config_ = nullptr;
    QString dir_, file_;
    QDateTime seen_;
    qint64 seenSize_ = -1;
    QLabel* picture_;
    QToolButton* editor_;
    QMenu* menu_;
    QFileSystemWatcher* watcher_;
    QTimer* poll_;
    bool ownChange_ = false;
};

} // namespace vdj

#endif
