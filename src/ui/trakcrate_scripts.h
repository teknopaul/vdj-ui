#ifndef TRAKCRATE_SCRIPTS_UI_H
#define TRAKCRATE_SCRIPTS_UI_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

#include "trakcrate/scripts.h"

class QMenu;
class QWidget;
class QProcess;
class QTimer;

namespace vdj {

class ConfigManager;

// What one run produced.
struct ScriptResult {
    bool ok = false;                 // started, exited 0, not cancelled, no timeout
    bool started = false;
    int exitCode = -1;
    bool timedOut = false, cancelled = false, truncated = false;
    qint64 durationMs = 0;
    QString output;                  // stdout and stderr together, at most kMaxOutput bytes
    QString snapshot;                // the snapshot folder made before the run, empty if none
    QString error;                   // why it did not start
    QString script, kind, target;
    QDateTime when;
};

// Runs one script at a time, directly (no shell), as the user: own process group, stdin closed, nice 10, output capped, a timeout, a library lock
// (<library>/.trakcrate-lock). Never started by anything but a call to start().
class ScriptRunner : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxOutput = 1 << 20;
    static constexpr int kTrackTimeoutMs = 5 * 60 * 1000;

    explicit ScriptRunner(QObject* parent = nullptr);

    struct Options {
        int timeoutMs = -1;          // -1: 5 minutes for a track script, none for a crate script; 0 = none
        int killDelayMs = 5000;      // SIGTERM, then SIGKILL after this
        bool snapshot = true;        // taken first unless the script says "# no-snapshot"
    };

    bool busy() const { return proc_ != nullptr; }
    // False (err says why) when busy, the script is not Ready, or the library is locked. Otherwise finished() is emitted when it ends.
    bool start(const trakcrate::ScriptInfo& script, const trakcrate::ScriptRun& run, const Options& options, QString* err = nullptr);
    void cancel();                                   // SIGTERM to the group, SIGKILL after killDelayMs
    bool waitForFinished(int timeoutMs = 30000);
    const ScriptResult& lastResult() const { return last_; }

signals:
    void finished(const vdj::ScriptResult& result);

private:
    void finish(int exitCode, bool crashed);
    void killGroup(int signal);

    QProcess* proc_ = nullptr;
    QTimer* timeout_ = nullptr;
    QTimer* killTimer_ = nullptr;
    ScriptResult cur_, last_;
    QString lockPath_, logPath_;
    qint64 startedMs_ = 0;
    int killDelay_ = 5000;
    bool done_ = false;
};

// Scripts of the open library for the menus: who is reviewed or disabled (kept per user in the app config, never in the library, so a cloned
// library cannot arrive pre-approved), new script templates, undo of the last run, and the log.
class ScriptManager : public QObject {
    Q_OBJECT

public:
    explicit ScriptManager(ConfigManager* config, QObject* parent = nullptr);

    void setLibrary(const QString& root) { root_ = root; }
    QString library() const { return root_; }
    ScriptRunner* runner() const { return runner_; }

    std::vector<trakcrate::ScriptInfo> scripts(trakcrate::ScriptKind kind) const;
    void review(const trakcrate::ScriptInfo& s);                      // records the SHA-256 the user has seen
    void setDisabled(const trakcrate::ScriptInfo& s, bool disabled);
    // Writes a template that runs (executable, with the calling convention in comments). False if the name is taken or unsafe.
    bool newScript(trakcrate::ScriptKind kind, const QString& name, QString* path, QString* err = nullptr);
    // Runs on a track (kind Track) or the whole library; the snapshot is made first. Confirmation and review are the caller's job.
    bool run(const trakcrate::ScriptInfo& s, const QString& trackDir, bool dryRun, QString* err = nullptr, int deck = 0, long long positionMs = -1);
    bool undoLast(const QString& trackDirOrEmpty, QString* err = nullptr);
    const QList<ScriptResult>& log() const { return log_; }
    QString trakcrateBin() const;

    // "Open with editor": the editor remembered under script_editor, else the first text editor the desktop knows. Never xdg-open (it may run the
    // script). Binary scripts are not opened. False (with err) when nothing could be started.
    bool openInEditor(const trakcrate::ScriptInfo& s, QString* err = nullptr);
    QString logHtml() const;                              // the session's runs, newest first; "trakcrate:" lines highlighted, failures red

signals:
    void ran(const vdj::ScriptResult& result);            // after every run, so the UI can reload what changed
    void changed();                                       // the list of scripts or their state changed

private:
    QString key(const trakcrate::ScriptInfo& s) const;
    ConfigManager* config_;
    ScriptRunner* runner_;
    QString root_;
    QList<ScriptResult> log_;
};

// Fills a menu with the scripts of one kind (track scripts run on trackDir() , crate scripts on the whole library). Each script is a submenu: Run,
// Run (dry run) if it declares it, Review (new/changed scripts), Open with editor..., Show in file manager, Copy path, Show last output, Disable.
// Crate scripts ask first. Rebuilt every time the menu is about to show, so the list follows the folder.
void populateScriptsMenu(QMenu* menu, ScriptManager* manager, trakcrate::ScriptKind kind, QWidget* parent, std::function<QString()> trackDir);
void showScriptsLog(ScriptManager* manager, QWidget* parent);
void showScriptOutput(const ScriptResult& r, QWidget* parent);

} // namespace vdj

Q_DECLARE_METATYPE(vdj::ScriptResult)

#endif
