#include "trakcrate_edit_render_dialog.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QThread>
#include <QVBoxLayout>

#include "edit_prefs.h"
#include "trakcrate/track.h"

using namespace trakcrate;

namespace vdj {

namespace {

// Where the original was imported from (source.txt), for the default folder of file only edits.
QString importedFrom(const fs::path& trackDir)
{
    Track t;
    if (!loadTrack(trackDir, &t)) return QString();
    return QString::fromStdString(t.source.get("imported_from"));
}

} // namespace

EditRenderDialog::EditRenderDialog(const EditRequest& req, EditRenderCache* cache, const Library* lib, ConfigManager* config, QWidget* parent)
    : QDialog(parent), req_(req), cache_(cache), lib_(lib), config_(config)
{
    setWindowTitle(QStringLiteral("Render edit"));
    const EditPrefs prefs(config);
    const QString from = importedFrom(req.trackDir);

    library_ = new QRadioButton(QStringLiteral("Add to the library, next to the original"));
    file_ = new QRadioButton(QStringLiteral("Save a file only"));
    folder_ = new QLineEdit(prefs.folder(from));
    choose_ = new QPushButton(QStringLiteral("Choose..."));
    format_ = new QComboBox;
    format_->addItem(QStringLiteral("WAV (16 bit)"), QStringLiteral("wav"));
    format_->addItem(QStringLiteral("MP3 (constant bitrate)"), QStringLiteral("mp3"));
    format_->setCurrentIndex(prefs.format() == QLatin1String("mp3") ? 1 : 0);
    bitrate_ = new QComboBox;
    for (int k : kMp3Kbps) bitrate_->addItem(QStringLiteral("%1 kbps").arg(k), k);
    bitrate_->setCurrentIndex(std::max(0, bitrate_->findData(prefs.mp3Kbps())));
    author_ = new QLineEdit(prefs.author());
    name_ = new QLabel;
    target_ = new QLabel;
    target_->setWordWrap(true);
    result_ = new QLabel;
    result_->setWordWrap(true);
    ok_ = new QPushButton(QStringLiteral("Render"));
    ok_->setDefault(true);
    cancelButton_ = new QPushButton(QStringLiteral("Cancel"));

    library_->setEnabled(lib_ != nullptr);
    (lib_ ? library_ : file_)->setChecked(true);

    auto* col = new QVBoxLayout(this);
    col->addWidget(library_);
    col->addWidget(file_);
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(choose_);
    col->addLayout(folderRow);
    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Format"), format_);
    form->addRow(QStringLiteral("MP3 bitrate"), bitrate_);
    form->addRow(QStringLiteral("Edit author"), author_);
    form->addRow(QStringLiteral("File name"), name_);
    col->addLayout(form);
    col->addWidget(target_);
    col->addWidget(result_);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(ok_);
    row->addWidget(cancelButton_);
    col->addLayout(row);

    connect(choose_, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Folder for the edit"), folder_->text());
        if (!d.isEmpty()) folder_->setText(d);
        file_->setChecked(true);
    });
    for (QRadioButton* r : {library_, file_}) connect(r, &QRadioButton::toggled, this, [this] { updateLabels(); });
    connect(format_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateLabels(); });
    connect(author_, &QLineEdit::textChanged, this, [this] { updateLabels(); });
    connect(folder_, &QLineEdit::textChanged, this, [this] { updateLabels(); });
    connect(ok_, &QPushButton::clicked, this, &EditRenderDialog::startRender);
    connect(cancelButton_, &QPushButton::clicked, this, [this] {
        cancel_ = true;
        if (!thread_) reject();
    });
    updateLabels();
}

EditRenderDialog::~EditRenderDialog()
{
    cancel_ = true;
    if (thread_) {
        thread_->wait();
        delete thread_;
    }
}

EditOutputParams EditRenderDialog::params() const
{
    EditOutputParams p;
    p.intoLibrary = library_->isChecked() && lib_ != nullptr;
    p.folder = folder_->text().toStdString();
    p.format = format_->currentData().toString() == QLatin1String("mp3") ? OutFormat::Mp3 : OutFormat::Wav;
    p.mp3Kbps = bitrate_->currentData().toInt();
    p.author = author_->text().trimmed().toStdString();
    return p;
}

void EditRenderDialog::updateLabels()
{
    const EditOutputParams p = params();
    bitrate_->setEnabled(p.format == OutFormat::Mp3);
    folder_->setEnabled(!p.intoLibrary);
    choose_->setEnabled(!p.intoLibrary);
    name_->setText(QString::fromStdString(plannedFileName(req_, p)));
    if (p.intoLibrary)
        target_->setText(QStringLiteral("Folder %1. %2.")
                             .arg(QString::fromStdString(req_.trackDir.parent_path().filename().string()),
                                  QString::fromStdString(describePlaylistTarget(*lib_, req_.trackDir))));
    else
        target_->setText(QStringLiteral("The audio and a .txt describing the edit go to %1").arg(folder_->text()));
    ok_->setEnabled(!thread_ && !p.author.empty());
}

void EditRenderDialog::savePrefs()
{
    EditPrefs prefs(config_);
    const EditOutputParams p = params();
    prefs.setFormat(p.format == OutFormat::Mp3 ? QStringLiteral("mp3") : QStringLiteral("wav"));
    prefs.setMp3Kbps(p.mp3Kbps);
    prefs.setAuthor(author_->text());
    if (!p.intoLibrary) prefs.setFolder(folder_->text());
}

void EditRenderDialog::startRender()
{
    if (thread_) return;
    const EditOutputParams p = params();
    if (p.author.empty()) {
        result_->setText(QStringLiteral("Enter the author name"));
        return;
    }
    savePrefs();
    result_->setText(QStringLiteral("Rendering..."));
    ok_->setEnabled(false);
    cancel_ = false;
    thread_ = QThread::create([this, p] {
        std::string err;
        bool ok = ensureRendered(cache_, req_, &err, &cancel_);
        EditOutcome o;
        if (ok) {
            o = writeEditOutput(*cache_, req_, p, lib_, &cancel_);
            ok = o.ok;
            err = o.error;
        }
        QMetaObject::invokeMethod(this, "finished", Qt::QueuedConnection, Q_ARG(bool, ok), Q_ARG(QString, QString::fromStdString(err)),
                                  Q_ARG(QString, QString::fromStdString(o.trackDir.string())), Q_ARG(QString, QString::fromStdString(o.audioFile.string())),
                                  Q_ARG(QString, QString::fromStdString(o.playlist)), Q_ARG(int, o.playlistIndex), Q_ARG(QString, QString::fromStdString(o.note)));
    });
    thread_->start();
}

void EditRenderDialog::finished(bool ok, const QString& error, const QString& trackDir, const QString& file, const QString& playlist, int, const QString& note)
{
    if (thread_) {
        thread_->wait();
        delete thread_;
        thread_ = nullptr;
    }
    if (!ok) {
        result_->setText(error);
        updateLabels();
        emit failed(error);
        return;
    }
    createdDir_ = trackDir;
    QString msg = trackDir.isEmpty() ? QStringLiteral("Saved %1").arg(QFileInfo(file).fileName())
                                     : QStringLiteral("Created track %1 in %2").arg(QFileInfo(trackDir).fileName(), QFileInfo(trackDir).dir().dirName());
    if (!playlist.isEmpty()) msg += QStringLiteral(", added to playlist %1 before the original").arg(playlist);
    else if (!note.isEmpty()) msg += QStringLiteral(" (%1)").arg(note);
    result_->setText(msg);
    emit created(trackDir, msg);
    accept();
}

} // namespace vdj
