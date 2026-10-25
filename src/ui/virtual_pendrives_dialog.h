#ifndef VIRTUAL_PENDRIVES_DIALOG_H
#define VIRTUAL_PENDRIVES_DIALOG_H

#include <QDialog>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace vdj {

class PendriveRegistry;

// The user's virtual pendrives (named folders) and the pendrives mounted in this PC, with what to do with them: look at one in the
// drive viewer pane, export one to the decks (switching the exported one like a deck does), eject the exported one.
// The dialog changes nothing but the registry; the rest is asked of the main window through signals.
class VirtualPendrivesDialog : public QDialog {
    Q_OBJECT

public:
    VirtualPendrivesDialog(PendriveRegistry* registry, QWidget* parent = nullptr);

    // what is exported now, for the status column (folder empty = nothing)
    void setExported(const QString& folder, const QString& state);

signals:
    void openRequested(const QString& folder);
    void exportRequested(const QString& folder, const QString& name);
    void ejectRequested();

private slots:
    void rebuild();
    void addFolder();
    void removeSelected();
    void renameSelected();
    void updateButtons();

private:
    struct Choice { QString name, folder; bool physical = false; bool valid = false; };
    Choice currentChoice() const;

    PendriveRegistry* registry_;
    QTreeWidget* tree_;
    QPushButton *addBtn_, *removeBtn_, *renameBtn_, *openBtn_, *exportBtn_, *ejectBtn_;
    QLabel* exportedLabel_;
    QString exportedFolder_, exportedState_;
};

} // namespace vdj

#endif // VIRTUAL_PENDRIVES_DIALOG_H
