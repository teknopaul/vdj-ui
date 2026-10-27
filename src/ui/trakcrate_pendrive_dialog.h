#ifndef TRAKCRATE_PENDRIVE_DIALOG_H
#define TRAKCRATE_PENDRIVE_DIALOG_H

#include <QDialog>
#include <QStringList>

#include "trakcrate/library.h"

class QLineEdit;
class QTreeWidget;
class QPushButton;

namespace vdj {

// "New pendrive": a name and the initial contents, chosen from the library's folders and playlists. Each ticked folder or playlist becomes one
// playlist on the pendrive. More can be added later from the folder and playlist views ("Add to pendrive").
class TrakcratePendriveDialog : public QDialog {
    Q_OBJECT

public:
    explicit TrakcratePendriveDialog(const trakcrate::Library& lib, QWidget* parent = nullptr);

    QString name() const;
    QStringList folders() const;              // ticked folders
    QStringList playlists() const;            // ticked playlists
    void setName(const QString& name);
    void tickFolder(const QString& name, bool on = true);
    void tickPlaylist(const QString& name, bool on = true);

private:
    void tick(int group, const QString& name, bool on);
    QStringList ticked(int group) const;
    void check();

    trakcrate::Library lib_;
    QLineEdit* name_;
    QTreeWidget* tree_;
    QPushButton* ok_;
};

} // namespace vdj

#endif
