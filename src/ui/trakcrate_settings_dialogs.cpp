#include "trakcrate_settings_dialogs.h"

#include "trakcrate/fsutil.h"
#include "trakcrate/settings_files.h"
#include "trakcrate/settings_sets.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace vdj {

QString defaultSettingsUserName()
{
    QString u = QString::fromLocal8Bit(qgetenv("USER")).trimmed();
    if (u.isEmpty()) u = QString::fromLocal8Bit(qgetenv("USERNAME")).trimmed();
    if (u.isEmpty()) return QString();
    u[0] = u[0].toUpper();
    return u;
}

NewSettingsDialog::NewSettingsDialog(const trakcrate::Library& lib, QWidget* parent) : QDialog(parent), lib_(lib)
{
    setWindowTitle(QStringLiteral("New settings"));
    auto* top = new QVBoxLayout(this);
    top->addWidget(new QLabel(QStringLiteral("The player settings are copied onto every pendrive. The built in files are used, with your name in the profile.")));
    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(trakcrate::uniqueSettingsSetName(lib_, "My settings")));
    user_ = new QLineEdit(defaultSettingsUserName());
    user_->setMaxLength(int(trakcrate::kDjprofileNameMax));
    user_->setToolTip(QStringLiteral("The name the players show for the person who made the pendrive (djprofile.nxs)"));
    form->addRow(QStringLiteral("Name of the settings:"), name_);
    form->addRow(QStringLiteral("Your name on the players:"), user_);
    top->addLayout(form);
    note_ = new QLabel;
    note_->setStyleSheet(QStringLiteral("color: #c04040;"));
    top->addWidget(note_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(QStringLiteral("Create"));
    top->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(name_, &QLineEdit::textChanged, this, [this] { check(); });
    connect(user_, &QLineEdit::textChanged, this, [this] { check(); });
    check();
}

QString NewSettingsDialog::setName() const { return name_->text().trimmed(); }
QString NewSettingsDialog::userName() const { return user_->text().trimmed(); }
void NewSettingsDialog::setSetName(const QString& n) { name_->setText(n); }
void NewSettingsDialog::setUserName(const QString& n) { user_->setText(n); }

void NewSettingsDialog::check()
{
    QString why;
    const std::string n = setName().toStdString();
    if (n.empty()) why = QStringLiteral("Give the settings a name");
    else if (!trakcrate::isSafeSettingsSetName(n)) why = QStringLiteral("That name cannot be used (no '/', no leading '.')");
    else if (trakcrate::hasSettingsSet(lib_, n)) why = QStringLiteral("There are settings with that name already");
    else if (userName().isEmpty()) why = QStringLiteral("Enter the name to show on the players");
    note_->setText(why);
    ok_->setEnabled(why.isEmpty());
}

CopySettingsDialog::CopySettingsDialog(const trakcrate::Library& lib, QWidget* parent) : QDialog(parent), lib_(lib)
{
    setWindowTitle(QStringLiteral("Copy settings from a rekordbox pendrive"));
    setMinimumWidth(480);
    auto* top = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    auto* row = new QHBoxLayout;
    folder_ = new QLineEdit;
    folder_->setPlaceholderText(QStringLiteral("the pendrive (the folder that holds PIONEER)"));
    auto* browse = new QPushButton(QStringLiteral("Browse..."));
    row->addWidget(folder_, 1);
    row->addWidget(browse);
    name_ = new QLineEdit(QString::fromStdString(trakcrate::uniqueSettingsSetName(lib_, "rekordbox")));
    form->addRow(QStringLiteral("Pendrive:"), row);
    form->addRow(QStringLiteral("Name of the settings:"), name_);
    top->addLayout(form);
    found_ = new QLabel;
    found_->setWordWrap(true);
    top->addWidget(found_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(QStringLiteral("Copy"));
    top->addWidget(buttons);
    connect(browse, &QPushButton::clicked, this, [this] { this->browse(); });
    connect(folder_, &QLineEdit::textChanged, this, [this] { check(); });
    connect(name_, &QLineEdit::textChanged, this, [this] { check(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    check();
}

QString CopySettingsDialog::setName() const { return name_->text().trimmed(); }
QString CopySettingsDialog::driveFolder() const { return folder_->text().trimmed(); }
void CopySettingsDialog::setDriveFolder(const QString& f) { folder_->setText(f); }
void CopySettingsDialog::setSetName(const QString& n) { name_->setText(n); }

void CopySettingsDialog::browse()
{
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose the rekordbox pendrive"), driveFolder());
    if (!d.isEmpty()) folder_->setText(d);
}

void CopySettingsDialog::check()
{
    QString why, info;
    if (!driveFolder().isEmpty()) {
        const auto& disk = trakcrate::settingsFileNames();
        const auto& onDrive = trakcrate::settingsDriveFileNames();
        QStringList have, lack;
        for (size_t i = 0; i < disk.size(); ++i) {
            std::error_code ec;
            const bool is = trakcrate::fs::is_regular_file(trakcrate::fs::path(driveFolder().toStdString()) / onDrive[i], ec);
            (is ? have : lack) << QString::fromStdString(disk[i]);
        }
        if (have.isEmpty()) why = QStringLiteral("No settings files found under PIONEER/ on that folder");
        else info = QStringLiteral("Found: %1").arg(have.join(QStringLiteral(", "))) +
                    (lack.isEmpty() ? QString() : QStringLiteral("\nNot on the drive (the built in file is used): %1").arg(lack.join(QStringLiteral(", "))));
    } else {
        why = QStringLiteral("Choose the pendrive to copy from");
    }
    const std::string n = setName().toStdString();
    if (why.isEmpty()) {
        if (n.empty()) why = QStringLiteral("Give the settings a name");
        else if (!trakcrate::isSafeSettingsSetName(n)) why = QStringLiteral("That name cannot be used (no '/', no leading '.')");
        else if (trakcrate::hasSettingsSet(lib_, n)) why = QStringLiteral("There are settings with that name already");
    }
    found_->setText(why.isEmpty() ? info : (info.isEmpty() ? why : info + QStringLiteral("\n") + why));
    ok_->setEnabled(why.isEmpty());
}

} // namespace vdj
