#include "artwork_view.h"

#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "artwork_launcher.h"
#include "config/config_manager.h"
#include "trakcrate/artwork_edit.h"

namespace vdj {

namespace {
const QString kSection = QStringLiteral("trakcrate");
const QString kKey = QStringLiteral("artwork_editor");
}

ArtworkView::ArtworkView(QWidget* parent) : QWidget(parent)
{
    setAcceptDrops(true);
    picture_ = new QLabel(QStringLiteral("no artwork"));
    picture_->setFixedSize(160, 160);
    picture_->setAlignment(Qt::AlignCenter);
    picture_->setFrameShape(QFrame::StyledPanel);
    picture_->setCursor(Qt::PointingHandCursor);
    picture_->setToolTip(QStringLiteral("Click to enlarge. Drop an image file here to replace it."));
    picture_->installEventFilter(this);

    editor_ = new QToolButton;
    editor_->setText(QStringLiteral("Open in editor"));
    editor_->setPopupMode(QToolButton::MenuButtonPopup);
    editor_->setToolTip(QStringLiteral("Open artwork.jpg in an image editor. Editors such as GIMP export to JPEG: use File > Overwrite artwork.jpg."));
    menu_ = new QMenu(this);
    editor_->setMenu(menu_);
    auto* replace = new QPushButton(QStringLiteral("Replace..."));
    auto* remove = new QPushButton(QStringLiteral("Remove"));
    auto* extract = new QPushButton(QStringLiteral("Re-extract"));
    extract->setToolTip(QStringLiteral("Take the cover embedded in the audio file again"));
    auto* restoreBtn = new QPushButton(QStringLiteral("Restore"));
    restoreBtn->setToolTip(QStringLiteral("Put back the previous artwork (one level is kept in .trakcrate-history)"));

    auto* buttons = new QVBoxLayout;
    for (QWidget* w : {static_cast<QWidget*>(editor_), static_cast<QWidget*>(replace), static_cast<QWidget*>(remove), static_cast<QWidget*>(extract), static_cast<QWidget*>(restoreBtn)})
        buttons->addWidget(w);
    buttons->addStretch(1);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(picture_);
    row->addLayout(buttons);

    watcher_ = new QFileSystemWatcher(this);
    poll_ = new QTimer(this);
    poll_->setInterval(1500);                                // some editors save by rename: the watch is lost, the poll is not
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, [this] { checkExternalChange(); watch(); });
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { checkExternalChange(); watch(); });
    connect(poll_, &QTimer::timeout, this, &ArtworkView::checkExternalChange);

    connect(editor_, &QToolButton::clicked, this, [this] {
        const QString last = rememberedEditor();
        if (last.isEmpty()) { rebuildEditorMenu(); editor_->showMenu(); return; }
        QString err;
        if (!openInEditor(last, &err)) emit status(err);
    });
    connect(menu_, &QMenu::aboutToShow, this, &ArtworkView::rebuildEditorMenu);
    connect(replace, &QPushButton::clicked, this, [this] {
        if (dir_.isEmpty()) return;
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Replace artwork"), QString(), QStringLiteral("Images (*.jpg *.jpeg *.png)"));
        QString err;
        if (!f.isEmpty() && !replaceWith(f, &err)) emit status(err);
    });
    connect(remove, &QPushButton::clicked, this, [this] { QString err; if (!removeArtwork(&err)) emit status(err); });
    connect(extract, &QPushButton::clicked, this, [this] { QString err; if (!reextract(&err)) emit status(err); });
    connect(restoreBtn, &QPushButton::clicked, this, [this] { QString err; if (!restore(&err)) emit status(err); });
    setEnabled(false);
}

void ArtworkView::setTrack(const QString& trackDir)
{
    dir_ = trackDir;
    setEnabled(!dir_.isEmpty());
    reload();
}

void ArtworkView::reload()
{
    const std::string f = dir_.isEmpty() ? std::string() : trakcrate::artworkOf(dir_.toStdString()).string();
    file_ = QString::fromStdString(f);
    QPixmap pm;
    if (!file_.isEmpty()) pm.load(file_);
    if (pm.isNull()) { picture_->setPixmap(QPixmap()); picture_->setText(QStringLiteral("no artwork")); }
    else picture_->setPixmap(pm.scaled(picture_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    const QFileInfo fi(file_);
    seen_ = fi.exists() ? fi.lastModified() : QDateTime();
    seenSize_ = fi.exists() ? fi.size() : -1;
    watch();
    if (dir_.isEmpty()) poll_->stop(); else poll_->start();
}

void ArtworkView::watch()
{
    if (!watcher_->files().isEmpty()) watcher_->removePaths(watcher_->files());
    if (!watcher_->directories().isEmpty()) watcher_->removePaths(watcher_->directories());
    if (dir_.isEmpty()) return;
    watcher_->addPath(dir_);                                 // a rename over the file shows as a change of the folder
    if (!file_.isEmpty()) watcher_->addPath(file_);           // added again after every event, a replaced file is a new inode
}

void ArtworkView::checkExternalChange()
{
    if (dir_.isEmpty() || ownChange_) return;
    const std::string f = trakcrate::artworkOf(dir_.toStdString()).string();
    const QFileInfo fi(QString::fromStdString(f));
    const QDateTime when = fi.exists() ? fi.lastModified() : QDateTime();
    const qint64 size = fi.exists() ? fi.size() : -1;
    if (QString::fromStdString(f) == file_ && when == seen_ && size == seenSize_) return;
    std::string err;
    trakcrate::markArtworkEdited(dir_.toStdString(), &err);
    reload();
    emit artworkChanged(dir_);
    emit status(QStringLiteral("Artwork changed, reloaded"));
}

void ArtworkView::changed(const QString& message)
{
    reload();
    emit artworkChanged(dir_);
    emit status(message);
}

bool ArtworkView::replaceWith(const QString& imagePath, QString* err)
{
    if (dir_.isEmpty()) { if (err) *err = QStringLiteral("no track"); return false; }
    std::string e;
    ownChange_ = true;
    const bool ok = trakcrate::replaceArtwork(dir_.toStdString(), imagePath.toStdString(), &e);
    ownChange_ = false;
    if (!ok) { if (err) *err = QString::fromStdString(e); return false; }
    changed(QStringLiteral("Artwork replaced"));
    return true;
}

bool ArtworkView::removeArtwork(QString* err)
{
    if (dir_.isEmpty()) { if (err) *err = QStringLiteral("no track"); return false; }
    std::string e;
    ownChange_ = true;
    const bool ok = trakcrate::removeArtwork(dir_.toStdString(), &e);
    ownChange_ = false;
    if (!ok) { if (err) *err = QString::fromStdString(e); return false; }
    changed(QStringLiteral("Artwork removed"));
    return true;
}

bool ArtworkView::reextract(QString* err)
{
    if (dir_.isEmpty()) { if (err) *err = QStringLiteral("no track"); return false; }
    std::string e;
    ownChange_ = true;
    const bool ok = trakcrate::reextractArtwork(dir_.toStdString(), &e);
    ownChange_ = false;
    if (!ok) { if (err) *err = QString::fromStdString(e); return false; }
    changed(QStringLiteral("Artwork taken from the audio file again"));
    return true;
}

bool ArtworkView::restore(QString* err)
{
    if (dir_.isEmpty()) { if (err) *err = QStringLiteral("no track"); return false; }
    std::string e;
    ownChange_ = true;
    const bool ok = trakcrate::restoreArtwork(dir_.toStdString(), &e);
    ownChange_ = false;
    if (!ok) { if (err) *err = QString::fromStdString(e); return false; }
    changed(QStringLiteral("Earlier artwork restored"));
    return true;
}

QString ArtworkView::rememberedEditor() const
{
    return config_ ? config_->loadValue(kSection, kKey) : QString();
}

bool ArtworkView::openInEditor(const QString& exec, QString* err)
{
    if (file_.isEmpty()) { if (err) *err = QStringLiteral("this track has no artwork to edit"); return false; }
    std::string e;
    trakcrate::backupArtwork(dir_.toStdString(), &e);          // so a bad save in the editor can be undone with Restore
    if (!launchEditor(exec, file_, err)) return false;
    if (config_) config_->saveValue(kSection, kKey, exec);
    return true;
}

void ArtworkView::rebuildEditorMenu()
{
    menu_->clear();
    const QList<DesktopApp> apps = scanDesktopApps(defaultApplicationDirs(), QStringLiteral("image/jpeg"));
    for (const DesktopApp& a : apps) {
        QAction* act = menu_->addAction(a.name);
        const QString exec = a.exec;
        connect(act, &QAction::triggered, this, [this, exec] { QString err; if (!openInEditor(exec, &err)) emit status(err); });
    }
    if (apps.isEmpty()) menu_->addAction(QStringLiteral("No image editor found (sudo apt install gimp)"))->setEnabled(false);
    menu_->addSeparator();
    QAction* custom = menu_->addAction(QStringLiteral("Custom command..."));
    connect(custom, &QAction::triggered, this, [this] {
        bool ok = false;
        const QString cmd = QInputDialog::getText(this, QStringLiteral("Image editor"),
                                                  QStringLiteral("Command (%f is the file, no shell is used):"), QLineEdit::Normal,
                                                  rememberedEditor().isEmpty() ? QStringLiteral("gimp %f") : rememberedEditor(), &ok);
        QString err;
        if (ok && !cmd.trimmed().isEmpty() && !openInEditor(cmd.trimmed(), &err)) emit status(err);
    });
}

void ArtworkView::enlarge()
{
    if (file_.isEmpty()) return;
    QPixmap pm(file_);
    if (pm.isNull()) return;
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QStringLiteral("Artwork"));
    auto* l = new QVBoxLayout(dlg);
    auto* img = new QLabel;
    img->setPixmap(pm.scaled(480, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    l->addWidget(img);
    l->addWidget(new QLabel(QStringLiteral("%1 x %2").arg(pm.width()).arg(pm.height())));
    dlg->show();
}

bool ArtworkView::eventFilter(QObject* o, QEvent* e)
{
    if (o == picture_ && e->type() == QEvent::MouseButtonRelease) { enlarge(); return true; }
    return QWidget::eventFilter(o, e);
}

void ArtworkView::dragEnterEvent(QDragEnterEvent* e)
{
    if (!dir_.isEmpty() && e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void ArtworkView::dropEvent(QDropEvent* e)
{
    for (const QUrl& u : e->mimeData()->urls()) {
        if (!u.isLocalFile()) continue;
        QString err;
        if (!replaceWith(u.toLocalFile(), &err)) emit status(err);
        e->acceptProposedAction();
        return;
    }
}

} // namespace vdj
