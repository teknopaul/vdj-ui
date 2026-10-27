#include "trakcrate_import_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace vdj {

TrakcrateImportDialog::TrakcrateImportDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Import a folder of audio files"));
    folder_ = new QLineEdit;
    auto* browse = new QPushButton(QStringLiteral("Browse..."));
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(browse);

    playlist_ = new QLineEdit;
    playlist_->setPlaceholderText(QStringLiteral("the folder's name"));
    masterFolder_ = new QLineEdit;
    masterFolder_->setPlaceholderText(QStringLiteral("the folder's name"));
    noPlaylist_ = new QCheckBox(QStringLiteral("Do not create a playlist"));
    recursive_ = new QCheckBox(QStringLiteral("Include subfolders"));
    dryRun_ = new QCheckBox(QStringLiteral("Dry run (show what would be imported, write nothing)"));
    analyse_ = new QCheckBox(QStringLiteral("Analyse the new tracks (beat grid, waveform, loudness)"));
    analyse_->setChecked(true);
    mode_ = new QComboBox;
    mode_->addItem(QStringLiteral("Copy the files"), int(trakcrate::ImportMode::Copy));
    mode_->addItem(QStringLiteral("Move the files (removes the originals)"), int(trakcrate::ImportMode::Move));
    mode_->addItem(QStringLiteral("Link to the files (no copy)"), int(trakcrate::ImportMode::Link));
    ifExists_ = new QComboBox;
    ifExists_->addItem(QStringLiteral("Stop"), 0);
    ifExists_->addItem(QStringLiteral("Add the new tracks at the end"), 1);
    ifExists_->addItem(QStringLiteral("Replace the playlist"), 2);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Folder"), folderRow);
    form->addRow(QStringLiteral("Playlist name"), playlist_);
    form->addRow(QString(), noPlaylist_);
    form->addRow(QStringLiteral("If the playlist exists"), ifExists_);
    form->addRow(QStringLiteral("Library folder"), masterFolder_);
    form->addRow(QStringLiteral("Files"), mode_);
    form->addRow(QString(), recursive_);
    form->addRow(QString(), analyse_);
    form->addRow(QString(), dryRun_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Import"));
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    resize(520, sizeHint().height());

    connect(browse, &QPushButton::clicked, this, &TrakcrateImportDialog::browse);
    connect(folder_, &QLineEdit::textEdited, this, &TrakcrateImportDialog::folderEdited);
    connect(playlist_, &QLineEdit::textEdited, this, [this]() { playlistEdited_ = true; });
    connect(noPlaylist_, &QCheckBox::toggled, playlist_, &QWidget::setDisabled);
    connect(noPlaylist_, &QCheckBox::toggled, ifExists_, &QWidget::setDisabled);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { if (!folder().isEmpty()) accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void TrakcrateImportDialog::setFolder(const QString& folder)
{
    folder_->setText(folder);
    folderEdited();
}

QString TrakcrateImportDialog::folder() const { return folder_->text().trimmed(); }

void TrakcrateImportDialog::browse()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Folder of audio files"), folder().isEmpty() ? QDir::homePath() : folder());
    if (!dir.isEmpty()) setFolder(dir);
}

// The playlist name follows the folder's name until the user types a name of their own.
void TrakcrateImportDialog::folderEdited()
{
    if (playlistEdited_) return;
    QString base = QFileInfo(QDir::cleanPath(folder())).fileName();
    playlist_->setText(base);
}

trakcrate::ImportOptions TrakcrateImportDialog::options() const
{
    trakcrate::ImportOptions o;
    o.recursive = recursive_->isChecked();
    o.mode = trakcrate::ImportMode(mode_->currentData().toInt());
    o.folder = masterFolder_->text().trimmed().toStdString();
    o.playlist = playlist_->text().trimmed().toStdString();
    o.noPlaylist = noPlaylist_->isChecked();
    o.append = ifExists_->currentData().toInt() == 1;
    o.replace = ifExists_->currentData().toInt() == 2;
    o.dryRun = dryRun_->isChecked();
    o.analyse = analyse_->isChecked();
    return o;
}

} // namespace vdj
