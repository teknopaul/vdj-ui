#ifndef TRAKCRATE_SETTINGS_VIEWER_H
#define TRAKCRATE_SETTINGS_VIEWER_H

#include <QDialog>
#include <QString>

#include "trakcrate/library.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace vdj {

// Shows what is known about the files of a settings set, read only, and optionally the differences to a second set. The one thing that can be
// changed is the name on the players in djprofile.nxs.
class TrakcrateSettingsViewer : public QDialog {
    Q_OBJECT

public:
    explicit TrakcrateSettingsViewer(const trakcrate::Library& lib, QWidget* parent = nullptr);

    void setSet(const QString& name);                 // A
    void setCompareSet(const QString& name);          // B; empty = no comparison
    void setFile(const QString& fileName);            // "DEVSETTING.DAT", ...
    QString set() const;
    QString compareSet() const;
    int rowCount() const;
    int differingRows() const;                        // rows shown highlighted (comparison only)
    // Saves the name edit for set A (what the Save button does). False with err when the name is refused.
    bool saveUserName(QString* err = nullptr);

signals:
    void userNameChanged(const QString& set);

private:
    void reload();
    bool read(const QString& set, const QString& file, std::string* bytes, bool* builtIn) const;

    trakcrate::Library lib_;
    QComboBox *a_, *b_, *file_;
    QTableWidget* table_;
    QPlainTextEdit* hex_;
    QLabel* status_;
    QLineEdit* userName_;
    QPushButton* save_;
    int differing_ = 0;
};

} // namespace vdj

#endif
