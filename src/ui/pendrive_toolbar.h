#ifndef PENDRIVE_TOOLBAR_H
#define PENDRIVE_TOOLBAR_H

#include <QWidget>

class QComboBox;
class QToolButton;

namespace vdj {

class PendriveRegistry;

// One slim line above the drive viewer: a list of the configured and plugged-in pendrives (choosing one opens it in the viewer only)
// and the two deck-like buttons: USB STOP (eject the exported pendrive) and Insert (export the chosen one to the decks).
class PendriveToolBar : public QWidget {
    Q_OBJECT

public:
    explicit PendriveToolBar(PendriveRegistry* registry, QWidget* parent = nullptr);

    // The pendrive the viewer shows now (selects it in the list when it is one of them, else nothing is selected).
    void setViewed(const QString& folder);
    // The pendrive the decks see now (marked in the list); empty = nothing.
    void setExported(const QString& folder);
    // Dim USB STOP when pressing it will not eject (the button stays clickable: the decks show who uses the stick).
    void setStopHint(bool works, const QString& why);

    QString selectedFolder() const;

signals:
    void openRequested(const QString& folder);
    void insertRequested(const QString& folder, const QString& name);
    void stopRequested();

private:
    void rebuild();

    PendriveRegistry* registry_;
    QComboBox* combo_;
    QToolButton* stopBtn_;
    QToolButton* insertBtn_;
    QString viewed_, exported_;
};

} // namespace vdj

#endif // PENDRIVE_TOOLBAR_H
