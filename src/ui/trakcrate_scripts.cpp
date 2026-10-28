#include "trakcrate_scripts.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <csignal>
#include <fcntl.h>
#include <unistd.h>

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QMimeDatabase>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include "artwork_launcher.h"
#include "config/config_manager.h"

namespace vdj {

namespace {
const QString kReviewed = QStringLiteral("trakcrate-script-reviewed");
const QString kDisabled = QStringLiteral("trakcrate-script-disabled");

bool processAlive(qint64 pid) { return pid > 0 && (::kill(pid_t(pid), 0) == 0 || errno == EPERM); }
}

ScriptRunner::ScriptRunner(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<vdj::ScriptResult>("vdj::ScriptResult");
}

bool ScriptRunner::start(const trakcrate::ScriptInfo& s, const trakcrate::ScriptRun& r, const Options& o, QString* err)
{
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    if (proc_) return fail(QStringLiteral("a script is already running"));
    if (!s.runnable()) return fail(QStringLiteral("%1 cannot run: %2").arg(QString::fromStdString(s.name), QString::fromStdString(s.warning.empty() ? trakcrate::scriptStateName(s.state) : s.warning)));

    // the library lock: one writer at a time (a lock left by a dead process is taken over)
    lockPath_ = QString::fromStdString((r.libraryRoot / ".trakcrate-lock").string());
    for (int attempt = 0; attempt < 2; ++attempt) {
        int fd = ::open(lockPath_.toLocal8Bit().constData(), O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd >= 0) {
            const QByteArray pid = QByteArray::number(qint64(::getpid())) + "\n";
            (void)!::write(fd, pid.constData(), size_t(pid.size()));
            ::close(fd);
            break;
        }
        QFile f(lockPath_);
        qint64 owner = 0;
        if (f.open(QIODevice::ReadOnly)) owner = f.readAll().trimmed().toLongLong();
        if (attempt == 0 && !processAlive(owner)) { QFile::remove(lockPath_); continue; }
        lockPath_.clear();
        return fail(QStringLiteral("the library is locked by another writer (pid %1)").arg(owner));
    }

    cur_ = ScriptResult();
    cur_.script = QString::fromStdString(s.name);
    cur_.kind = s.kind == trakcrate::ScriptKind::Track ? QStringLiteral("track") : QStringLiteral("crate");
    cur_.target = QString::fromStdString((s.kind == trakcrate::ScriptKind::Track ? r.trackDir : r.libraryRoot).string());
    cur_.when = QDateTime::currentDateTime();
    logPath_ = QString::fromStdString((trakcrate::scriptsDir(r.libraryRoot) / ".log").string());
    if (o.snapshot && !s.noSnapshot) {
        std::string e;
        const trakcrate::fs::path snap = s.kind == trakcrate::ScriptKind::Track ? trakcrate::snapshotTrack(r.trackDir, s.name, &e)
                                                                                : trakcrate::snapshotCrate(r.libraryRoot, s.name, &e);
        if (snap.empty()) { QFile::remove(lockPath_); lockPath_.clear(); return fail(QStringLiteral("snapshot failed: %1").arg(QString::fromStdString(e))); }
        cur_.snapshot = QString::fromStdString(snap.string());
    }

    proc_ = new QProcess(this);
    proc_->setProgram(QString::fromStdString(s.path.string()));
    QStringList args;
    for (const std::string& a : trakcrate::scriptArgs(s, r)) args << QString::fromStdString(a);
    proc_->setArguments(args);
    proc_->setWorkingDirectory(QString::fromStdString(trakcrate::scriptWorkDir(s, r).string()));
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char* v : {"TRAKCRATE_TRACK_POSITION_MS", "TRAKCRATE_DECK", "TRAKCRATE_SELECTION", "TRAKCRATE_DRY_RUN", "TRAKCRATE_BIN"}) env.remove(QLatin1String(v));
    for (const auto& kv : trakcrate::scriptEnvironment(s, r)) env.insert(QString::fromStdString(kv.first), QString::fromStdString(kv.second));
    proc_->setProcessEnvironment(env);
    proc_->setStandardInputFile(QProcess::nullDevice());                   // stdin closed: a script cannot wait for input
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    proc_->setChildProcessModifier([] {
        ::setsid();                                                         // own process group, so the whole tree can be signalled
        errno = 0;
        (void)!::nice(10);                                                  // audio playback keeps priority
    });
    killDelay_ = o.killDelayMs;
    done_ = false;

    QByteArray* buffer = new QByteArray;
    connect(proc_, &QProcess::readyRead, this, [this, buffer] {
        const QByteArray chunk = proc_->readAll();
        const int room = kMaxOutput - int(buffer->size());
        if (room > 0) buffer->append(chunk.left(room));
        if (chunk.size() > room) cur_.truncated = true;
    });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && !done_) { cur_.error = proc_->errorString(); finish(-1, false); }
    });
    connect(proc_, &QProcess::finished, this, [this, buffer](int code, QProcess::ExitStatus st) {
        buffer->append(proc_->readAll().left(qMax(0, kMaxOutput - int(buffer->size()))));
        cur_.output = QString::fromUtf8(*buffer);
        delete buffer;
        if (!done_) finish(code, st == QProcess::CrashExit);
    });

    startedMs_ = QDateTime::currentMSecsSinceEpoch();
    proc_->start();
    cur_.started = proc_->state() != QProcess::NotRunning || cur_.error.isEmpty();

    timeout_ = new QTimer(this);
    timeout_->setSingleShot(true);
    int ms = o.timeoutMs;
    if (ms < 0) ms = s.kind == trakcrate::ScriptKind::Track ? kTrackTimeoutMs : 0;
    if (ms > 0) {
        connect(timeout_, &QTimer::timeout, this, [this] { cur_.timedOut = true; cancel(); });
        timeout_->start(ms);
    }
    return true;
}

void ScriptRunner::killGroup(int sig)
{
    if (proc_ && proc_->processId() > 0) ::kill(-pid_t(proc_->processId()), sig);       // the group: children die with the script
}

void ScriptRunner::cancel()
{
    if (!proc_) return;
    if (!cur_.timedOut) cur_.cancelled = true;
    killGroup(SIGTERM);
    if (!killTimer_) {
        killTimer_ = new QTimer(this);
        killTimer_->setSingleShot(true);
        connect(killTimer_, &QTimer::timeout, this, [this] { killGroup(SIGKILL); });
    }
    killTimer_->start(killDelay_);
}

bool ScriptRunner::waitForFinished(int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (proc_ && t.elapsed() < timeoutMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return !proc_;
}

void ScriptRunner::finish(int exitCode, bool crashed)
{
    if (done_ || !proc_) return;
    done_ = true;
    cur_.exitCode = exitCode;
    cur_.durationMs = QDateTime::currentMSecsSinceEpoch() - startedMs_;
    if (cur_.output.isEmpty() && proc_) cur_.output = QString::fromUtf8(proc_->readAll());
    cur_.ok = cur_.error.isEmpty() && !crashed && exitCode == 0 && !cur_.timedOut && !cur_.cancelled;
    if (timeout_) { timeout_->stop(); timeout_->deleteLater(); timeout_ = nullptr; }
    if (killTimer_) { killTimer_->stop(); killTimer_->deleteLater(); killTimer_ = nullptr; }
    QProcess* p = proc_;
    proc_ = nullptr;
    p->deleteLater();
    if (!lockPath_.isEmpty()) QFile::remove(lockPath_);
    lockPath_.clear();
    QFile log(logPath_);                                                    // the plain text log next to the scripts
    if (QFileInfo(logPath_).dir().exists() && log.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream(&log) << cur_.when.toString(Qt::ISODate) << "  " << cur_.kind << "-script-" << cur_.script << "  "
                          << (cur_.ok ? "ok" : cur_.timedOut ? "timeout" : cur_.cancelled ? "cancelled" : "failed") << " (" << cur_.exitCode << ")  "
                          << cur_.durationMs << " ms  " << cur_.target << "\n";
    }
    last_ = cur_;
    emit finished(last_);
}

// ---- ScriptManager -------------------------------------------------------------------------------------------------------------------------

ScriptManager::ScriptManager(ConfigManager* config, QObject* parent) : QObject(parent), config_(config), runner_(new ScriptRunner(this))
{
    connect(runner_, &ScriptRunner::finished, this, [this](const ScriptResult& r) {
        log_.append(r);
        emit ran(r);
    });
}

QString ScriptManager::key(const trakcrate::ScriptInfo& s) const
{
    return QStringLiteral("%1.%2").arg(s.kind == trakcrate::ScriptKind::Track ? "track" : "crate", QString::fromStdString(s.name));
}

std::vector<trakcrate::ScriptInfo> ScriptManager::scripts(trakcrate::ScriptKind kind) const
{
    if (root_.isEmpty()) return {};
    std::map<std::string, std::string> reviewed;
    std::set<std::string> disabled;
    const QString prefix = QStringLiteral("%1.").arg(kind == trakcrate::ScriptKind::Track ? "track" : "crate");
    if (config_) {
        for (const QString& k : config_->keysOf(kReviewed))
            if (k.startsWith(prefix)) reviewed[k.mid(prefix.size()).toStdString()] = config_->loadValue(kReviewed, k).toStdString();
        for (const QString& k : config_->keysOf(kDisabled))
            if (k.startsWith(prefix) && config_->loadValue(kDisabled, k) == QLatin1String("1")) disabled.insert(k.mid(prefix.size()).toStdString());
    }
    return trakcrate::listScripts(root_.toStdString(), kind, reviewed, disabled);
}

void ScriptManager::review(const trakcrate::ScriptInfo& s)
{
    if (config_) config_->saveValue(kReviewed, key(s), QString::fromStdString(s.sha256));
    emit changed();
}

void ScriptManager::setDisabled(const trakcrate::ScriptInfo& s, bool disabled)
{
    if (config_) config_->saveValue(kDisabled, key(s), disabled ? QStringLiteral("1") : QStringLiteral("0"));
    emit changed();
}

bool ScriptManager::newScript(trakcrate::ScriptKind kind, const QString& name, QString* path, QString* err)
{
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    if (root_.isEmpty()) return fail(QStringLiteral("no library is open"));
    if (name.isEmpty() || !trakcrate::isSafeName(name.toStdString()) || name.startsWith(QLatin1Char('.'))) return fail(QStringLiteral("bad script name"));
    const bool track = kind == trakcrate::ScriptKind::Track;
    const QString file = QStringLiteral("%1/scripts/%2-script-%3").arg(root_, track ? "track" : "crate", name);
    if (QFile::exists(file)) return fail(QStringLiteral("a script with that name exists"));
    QDir().mkpath(root_ + QStringLiteral("/scripts"));
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return fail(f.errorString());
    const QString body = track
        ? QStringLiteral("#!/bin/sh\n# description: what this script does to one track\n# dry-run: no\n"
                         "#\n# Called as: track-script-%1 TRACK_DIR   (the current directory is the track folder)\n"
                         "# Environment: TRAKCRATE_LIBRARY TRAKCRATE_TRACK_DIR TRAKCRATE_TRACK_AUDIO TRAKCRATE_DRY_RUN TRAKCRATE_BIN\n"
                         "# Exit 0 for success. Lines starting with \"trakcrate: \" are highlighted in the log.\n\n"
                         "echo \"trakcrate: running on $TRAKCRATE_TRACK_DIR\"\n").arg(name)
        : QStringLiteral("#!/bin/sh\n# description: what this script does to the whole library\n# selection: no\n# dry-run: no\n"
                         "#\n# Called as: crate-script-%1 LIBRARY_DIR   (the current directory is the library)\n"
                         "# Environment: TRAKCRATE_LIBRARY TRAKCRATE_SELECTION TRAKCRATE_DRY_RUN TRAKCRATE_BIN\n"
                         "# Exit 0 for success. Lines starting with \"trakcrate: \" are highlighted in the log.\n\n"
                         "echo \"trakcrate: running on $TRAKCRATE_LIBRARY\"\n").arg(name);
    f.write(body.toUtf8());
    f.close();
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    if (path) *path = file;
    emit changed();
    return true;
}

QString ScriptManager::trakcrateBin() const
{
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/trakcrate");
    if (QFileInfo(beside).isExecutable()) return beside;
    return QStandardPaths::findExecutable(QStringLiteral("trakcrate"));
}

bool ScriptManager::run(const trakcrate::ScriptInfo& s, const QString& trackDir, bool dryRun, QString* err, int deck, long long positionMs)
{
    trakcrate::ScriptRun r;
    r.libraryRoot = root_.toStdString();
    r.trackDir = trackDir.toStdString();
    r.dryRun = dryRun;
    r.deck = deck;
    r.positionMs = positionMs;
    r.trakcrateBin = trakcrateBin().toStdString();
    if (s.kind == trakcrate::ScriptKind::Track && trackDir.isEmpty()) { if (err) *err = QStringLiteral("no track"); return false; }
    return runner_->start(s, r, ScriptRunner::Options(), err);
}

bool ScriptManager::undoLast(const QString& trackDirOrEmpty, QString* err)
{
    const trakcrate::fs::path where = trackDirOrEmpty.isEmpty() ? trakcrate::fs::path(root_.toStdString()) : trakcrate::fs::path(trackDirOrEmpty.toStdString());
    const trakcrate::fs::path snap = trakcrate::lastSnapshot(where);
    if (snap.empty()) { if (err) *err = QStringLiteral("no script snapshot to restore"); return false; }
    std::string e;
    if (!trakcrate::restoreSnapshot(snap, where, &e)) { if (err) *err = QString::fromStdString(e); return false; }
    emit changed();
    return true;
}

bool ScriptManager::openInEditor(const trakcrate::ScriptInfo& s, QString* err)
{
    const QString file = QString::fromStdString(s.path.string());
    if (!QMimeDatabase().mimeTypeForFile(file).inherits(QStringLiteral("text/plain"))) {
        if (err) *err = QStringLiteral("this script is a binary: use Show in file manager");
        return false;
    }
    QString exec = config_ ? config_->loadValue(QStringLiteral("trakcrate"), QStringLiteral("script_editor")) : QString();
    if (exec.isEmpty()) {
        const QList<DesktopApp> apps = scanDesktopApps(defaultApplicationDirs(), QStringLiteral("text/plain"));
        if (apps.isEmpty()) { if (err) *err = QStringLiteral("no text editor found (sudo apt install gedit)"); return false; }
        exec = apps.first().exec;
    }
    if (!launchEditor(exec, file, err)) return false;
    if (config_) config_->saveValue(QStringLiteral("trakcrate"), QStringLiteral("script_editor"), exec);
    return true;
}

QString ScriptManager::logHtml() const
{
    QString h;
    for (int i = log_.size() - 1; i >= 0; --i) {
        const ScriptResult& r = log_[i];
        const QString status = r.ok ? QStringLiteral("ok") : r.timedOut ? QStringLiteral("timed out") : r.cancelled ? QStringLiteral("cancelled")
                             : !r.error.isEmpty() ? QStringLiteral("not started: ") + r.error.toHtmlEscaped() : QStringLiteral("failed (exit %1)").arg(r.exitCode);
        h += QStringLiteral("<p><b>%1 %2-script-%3</b> on %4 &mdash; <span style=\"color:%5\">%6</span>, %7 ms<br>")
                 .arg(r.when.toString(QStringLiteral("hh:mm:ss")), r.kind, r.script.toHtmlEscaped(), r.target.toHtmlEscaped(),
                      r.ok ? QStringLiteral("green") : QStringLiteral("red"), status).arg(r.durationMs);
        QStringList lines = r.output.toHtmlEscaped().split(QLatin1Char('\n'));
        for (QString& l : lines)
            if (l.startsWith(QLatin1String("trakcrate:"))) l = QStringLiteral("<b>%1</b>").arg(l);
        h += QStringLiteral("<pre>%1%2</pre>").arg(lines.join(QLatin1Char('\n')), r.truncated ? QStringLiteral("\n[output truncated]") : QString());
        if (!r.snapshot.isEmpty()) h += QStringLiteral("snapshot: %1 (Undo last script restores it)").arg(r.snapshot.toHtmlEscaped());
        h += QStringLiteral("</p>");
    }
    return h.isEmpty() ? QStringLiteral("<p>No scripts have run in this session.</p>") : h;
}

// ---- menus ---------------------------------------------------------------------------------------------------------------------------------

void showScriptOutput(const ScriptResult& r, QWidget* parent)
{
    auto* dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QStringLiteral("%1-script-%2").arg(r.kind, r.script));
    auto* l = new QVBoxLayout(dlg);
    auto* t = new QPlainTextEdit(r.output.isEmpty() ? QStringLiteral("(no output)") : r.output);
    t->setReadOnly(true);
    l->addWidget(t);
    dlg->resize(640, 360);
    dlg->show();
}

void showScriptsLog(ScriptManager* manager, QWidget* parent)
{
    auto* dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QStringLiteral("Scripts log"));
    auto* l = new QVBoxLayout(dlg);
    auto* t = new QTextBrowser;
    t->setHtml(manager->logHtml());
    l->addWidget(t);
    QObject::connect(manager, &ScriptManager::ran, t, [t, manager] { t->setHtml(manager->logHtml()); });
    dlg->resize(720, 420);
    dlg->show();
}

namespace {

// Review gate: a new or changed script must be looked at before it can run.
bool reviewed(ScriptManager* m, const trakcrate::ScriptInfo& s, QWidget* parent)
{
    if (s.state != trakcrate::ScriptState::New && s.state != trakcrate::ScriptState::Changed) return s.runnable();
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Review script"),
                    QStringLiteral("%1 is %2. A script can do anything you can do. Open it in an editor and read it before it runs.\n\n%3")
                        .arg(QString::fromStdString(s.name), s.state == trakcrate::ScriptState::New ? QStringLiteral("new") : QStringLiteral("changed since you reviewed it"),
                             QString::fromStdString(s.path.string())),
                    QMessageBox::NoButton, parent);
    QPushButton* open = box.addButton(QStringLiteral("Open with editor"), QMessageBox::ActionRole);
    QPushButton* ok = box.addButton(QStringLiteral("I have reviewed it"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == open) { QString err; if (!m->openInEditor(s, &err)) QMessageBox::information(parent, QStringLiteral("trakcrate"), err); return false; }
    if (box.clickedButton() != ok) return false;
    m->review(s);
    return true;
}

} // namespace

void populateScriptsMenu(QMenu* menu, ScriptManager* m, trakcrate::ScriptKind kind, QWidget* parent, std::function<QString()> trackDir)
{
    menu->clear();
    const bool track = kind == trakcrate::ScriptKind::Track;
    const auto scripts = m->scripts(kind);
    for (const trakcrate::ScriptInfo& s : scripts) {
        QString title = QString::fromStdString(s.name);
        if (!s.runnable()) title += QStringLiteral("  [%1]").arg(QString::fromLatin1(trakcrate::scriptStateName(s.state)));
        QMenu* sub = menu->addMenu(title);
        sub->setToolTip(QString::fromStdString(s.description.empty() ? s.warning : s.description));
        if (!s.description.empty()) sub->setTitle(title + QStringLiteral(" - ") + QString::fromStdString(s.description));
        auto go = [=](bool dry) {
            trakcrate::ScriptInfo cur = s;
            if (!reviewed(m, cur, parent)) return;
            for (const auto& again : m->scripts(kind)) if (again.name == s.name) cur = again;
            const QString dir = track ? trackDir() : QString();
            if (track && dir.isEmpty()) { QMessageBox::information(parent, QStringLiteral("trakcrate"), QStringLiteral("Load a trakcrate track first.")); return; }
            if (!track && QMessageBox::question(parent, QStringLiteral("Run crate script"),
                    QStringLiteral("Run crate-script-%1 on the whole library?\n\nA snapshot of the metadata is taken first. Audio files are not snapshotted: a script that damages them cannot be undone.")
                        .arg(QString::fromStdString(s.name)), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
            QString err;
            if (!m->run(cur, dir, dry, &err)) QMessageBox::warning(parent, QStringLiteral("trakcrate"), err);
        };
        sub->addAction(QStringLiteral("Run"), parent, [go] { go(false); })->setEnabled(!m->runner()->busy());
        if (s.dryRun) sub->addAction(QStringLiteral("Run (dry run)"), parent, [go] { go(true); })->setEnabled(!m->runner()->busy());
        sub->addSeparator();
        sub->addAction(QStringLiteral("Open with editor..."), parent, [=] { QString err; if (!m->openInEditor(s, &err)) QMessageBox::information(parent, QStringLiteral("trakcrate"), err); });
        sub->addAction(QStringLiteral("Show in file manager"), parent, [=] { QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(s.path.parent_path().string()))); });
        sub->addAction(QStringLiteral("Copy path"), parent, [=] { QApplication::clipboard()->setText(QString::fromStdString(s.path.string())); });
        sub->addAction(QStringLiteral("Show last output"), parent, [=] {
            for (int i = m->log().size() - 1; i >= 0; --i)
                if (m->log()[i].script == QString::fromStdString(s.name) && m->log()[i].kind == (track ? QStringLiteral("track") : QStringLiteral("crate"))) { showScriptOutput(m->log()[i], parent); return; }
            QMessageBox::information(parent, QStringLiteral("trakcrate"), QStringLiteral("This script has not run in this session."));
        });
        sub->addAction(s.state == trakcrate::ScriptState::Disabled ? QStringLiteral("Enable") : QStringLiteral("Disable"), parent,
                       [=] { m->setDisabled(s, s.state != trakcrate::ScriptState::Disabled); });
    }
    if (scripts.empty()) menu->addAction(QStringLiteral("No %1 scripts in scripts/").arg(track ? "track" : "crate"))->setEnabled(false);
    menu->addSeparator();
    menu->addAction(QStringLiteral("New script..."), parent, [=] {
        bool ok = false;
        const QString name = QInputDialog::getText(parent, QStringLiteral("New %1 script").arg(track ? "track" : "crate"), QStringLiteral("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        QString path, err;
        if (!m->newScript(kind, name, &path, &err)) { QMessageBox::warning(parent, QStringLiteral("trakcrate"), err); return; }
        for (const auto& s : m->scripts(kind)) if (s.name == name.toStdString()) { QString e; m->openInEditor(s, &e); }
    });
    menu->addAction(QStringLiteral("Undo last script"), parent, [=] {
        QString err;
        const QString dir = track ? trackDir() : QString();
        if (track && dir.isEmpty()) return;
        if (!m->undoLast(dir, &err)) QMessageBox::information(parent, QStringLiteral("trakcrate"), err);
        else emit m->ran(ScriptResult());                      // the callers reload what the restore changed
    })->setEnabled(!m->runner()->busy());
    menu->addAction(QStringLiteral("Scripts log..."), parent, [=] { showScriptsLog(m, parent); });
    if (m->runner()->busy()) menu->addAction(QStringLiteral("Cancel running script"), parent, [=] { m->runner()->cancel(); });
}

} // namespace vdj
