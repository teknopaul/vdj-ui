#ifndef TRAKCRATE_SETTINGS_DIALOGS_H
#define TRAKCRATE_SETTINGS_DIALOGS_H

#include <QDialog>
#include <QString>
#include <QStringList>

#include "trakcrate/library.h"

class QLabel;
class QLineEdit;
class QPushButton;

namespace vdj {

// A suggestion for the djprofile.nxs name: the login name with a capital letter ("paul" -> "Paul"). Never the name of the person who made the
// built in profile.
QString defaultSettingsUserName();

// "New settings": a name for the set and the user name for djprofile.nxs. The other four files are the built in ones.
class NewSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewSettingsDialog(const trakcrate::Library& lib, QWidget* parent = nullptr);

    QString setName() const;
    QString userName() const;
    void setSetName(const QString& name);
    void setUserName(const QString& name);

private:
    void check();

    trakcrate::Library lib_;
    QLineEdit *name_, *user_;
    QLabel* note_;
    QPushButton* ok_;
};

// "Copy from a rekordbox pendrive": a mounted drive (a folder holding PIONEER/) and a name for the new set. Shows which of the five files the drive has.
class CopySettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit CopySettingsDialog(const trakcrate::Library& lib, QWidget* parent = nullptr);

    QString setName() const;
    QString driveFolder() const;
    void setDriveFolder(const QString& folder);
    void setSetName(const QString& name);

private:
    void browse();
    void check();

    trakcrate::Library lib_;
    QLineEdit *name_, *folder_;
    QLabel* found_;
    QPushButton* ok_;
};

} // namespace vdj

#endif
