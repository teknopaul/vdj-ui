#include <QtTest/QtTest>

#include <QFile>
#include <fstream>
#include <QSignalSpy>

#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include "trakcrate/importer.h"
#include "trakcrate/scripts.h"
#include "trakcrate/sha256.h"
#include "trakcrate_wav.h"
#include "ui/trakcrate_scripts.h"

namespace tc = trakcrate;
using namespace vdj;

class TestTrakcrateScripts : public QObject
{
    Q_OBJECT

    tc::fs::path tmp_;
    tc::Library lib_;
    tc::fs::path track_;

    tc::fs::path put(const std::string& name, const std::string& body, mode_t mode = 0755)
    {
        const tc::fs::path p = tc::scriptsDir(tmp_ / "lib") / name;
        tc::fs::create_directories(p.parent_path());
        std::ofstream(p) << body;
        ::chmod(p.c_str(), mode);
        return p;
    }
    // a script that has been reviewed, found again by listScripts
    tc::ScriptInfo ready(tc::ScriptKind kind, const std::string& name)
    {
        std::map<std::string, std::string> rev;
        for (const auto& s : tc::listScripts(tmp_ / "lib", kind, {}, {})) rev[s.name] = s.sha256;
        for (const auto& s : tc::listScripts(tmp_ / "lib", kind, rev, {}))
            if (s.name == name) return s;
        return tc::ScriptInfo();
    }
    ScriptResult run(ScriptRunner& r, const tc::ScriptInfo& s, tc::ScriptRun run = tc::ScriptRun(), ScriptRunner::Options o = ScriptRunner::Options())
    {
        run.libraryRoot = tmp_ / "lib";
        if (s.kind == tc::ScriptKind::Track && run.trackDir.empty()) run.trackDir = track_;
        QString err;
        if (!r.start(s, run, o, &err)) { ScriptResult x; x.error = err; return x; }
        r.waitForFinished();
        return r.lastResult();
    }
    static std::string slurp(const tc::fs::path& p) { std::string s; tc::readFile(p, &s); return s; }

private slots:
    void init()
    {
        tmp_ = tc::makeTempDir("trakcrate-scripts");
        QVERIFY(!tmp_.empty());
        QVERIFY(tc::Library::create(tmp_ / "lib") && tc::Library::open(tmp_ / "lib", &lib_));
        tc::DecodedPcm p = tcwav::clicks(120, 0.25, 4);
        tcwav::writeWavMono(tmp_ / "src" / "a.wav", tcwav::mono(p), p.sampleRate);
        tc::ImportOptions io;
        QVERIFY(tc::importFolder(lib_, tmp_ / "src", io).ok);
        track_ = lib_.trackDir("src", "a");
        tc::fs::create_directories(tmp_ / "lib" / "scripts");
        ::chmod((tmp_ / "lib" / "scripts").c_str(), 0755);
    }
    void cleanup() { std::error_code ec; tc::fs::remove_all(tmp_, ec); }

    void discoveryOrderAndFiltering()
    {
        put("track-script-b-second", "#!/bin/sh\n# description: the second\n# dry-run: yes\n");
        put("track-script-a-first", "#!/bin/sh\n# description: the first\n# no-snapshot\n");
        put("track-script-c-plain", "#!/bin/sh\necho hi\n");
        put("track-script-notexec", "#!/bin/sh\n", 0644);
        put("README", "docs", 0755);
        put("lib/helper.sh", "#!/bin/sh\n", 0755);
        put("crate-script-all", "#!/bin/sh\n# selection: yes\n");
        put("track-script-", "#!/bin/sh\n");
        put("track-script-withdesc", "#!/bin/sh\n");
        std::ofstream(tc::scriptsDir(tmp_ / "lib") / "track-script-withdesc.desc") << "from the desc file\n";
        auto t = tc::listScripts(tmp_ / "lib", tc::ScriptKind::Track, {}, {});
        QStringList names;
        for (const auto& s : t) names << QString::fromStdString(s.name);
        QCOMPARE(names, QStringList({"a-first", "b-second", "c-plain", "withdesc"}));       // sorted; not executable, README, helper, empty name left out
        QCOMPARE(t[0].description, std::string("the first"));
        QVERIFY(t[0].noSnapshot);
        QVERIFY(t[1].dryRun && !t[0].dryRun);
        QCOMPARE(t[3].description, std::string("from the desc file"));
        auto c = tc::listScripts(tmp_ / "lib", tc::ScriptKind::Crate, {}, {});
        QCOMPARE(int(c.size()), 1);
        QVERIFY(c[0].selection);
        QVERIFY(tc::listScripts(tmp_ / "none", tc::ScriptKind::Track, {}, {}).empty());     // no scripts folder, no scripts
    }
    void reviewStatesAndWarnings()
    {
        const auto p = put("track-script-x", "#!/bin/sh\necho 1\n");
        auto s = tc::listScripts(tmp_ / "lib", tc::ScriptKind::Track, {}, {})[0];
        QCOMPARE(s.state, tc::ScriptState::New);
        QVERIFY(!s.runnable());
        std::map<std::string, std::string> rev{{"x", s.sha256}};
        QCOMPARE(tc::listScripts(tmp_ / "lib", tc::ScriptKind::Track, rev, {})[0].state, tc::ScriptState::Ready);
        std::ofstream(p, std::ios::app) << "echo 2\n";                                     // edited after the review
        QCOMPARE(tc::listScripts(tmp_ / "lib", tc::ScriptKind::Track, rev, {})[0].state, tc::ScriptState::Changed);
        rev["x"] = tc::listScripts(tmp_ / "lib", tc::ScriptKind::Track, {}, {})[0].sha256;
        QCOMPARE(tc::listScripts(tmp_ / "lib", tc::ScriptKind::Track, rev, {"x"})[0].state, tc::ScriptState::Disabled);
        ::chmod(p.c_str(), 0757);                                                          // world writable
        s = tc::listScripts(tmp_ / "lib", tc::ScriptKind::Track, rev, {})[0];
        QCOMPARE(s.state, tc::ScriptState::Warning);
        QVERIFY(!s.warning.empty());
        ::chmod(p.c_str(), 0755);
        ::chmod((tmp_ / "lib" / "scripts").c_str(), 0775);                                 // the folder is writable by the group
        QCOMPARE(tc::listScripts(tmp_ / "lib", tc::ScriptKind::Track, rev, {})[0].state, tc::ScriptState::Warning);
        ::chmod((tmp_ / "lib" / "scripts").c_str(), 0755);
        QCOMPARE(tc::listScripts(tmp_ / "lib", tc::ScriptKind::Track, rev, {})[0].state, tc::ScriptState::Ready);
        QCOMPARE(std::string(tc::scriptStateName(tc::ScriptState::Foreign)), std::string("foreign"));
    }
    void unreviewedAndWritableScriptsAreRefused()
    {
        put("track-script-new", "#!/bin/sh\ntouch ran\n");
        ScriptRunner r;
        auto s = tc::listScripts(tmp_ / "lib", tc::ScriptKind::Track, {}, {})[0];
        tc::ScriptRun run;
        run.libraryRoot = tmp_ / "lib";
        run.trackDir = track_;
        QString err;
        QVERIFY(!r.start(s, run, ScriptRunner::Options(), &err));
        QVERIFY(err.contains("new script"));
        QVERIFY(!tc::fs::exists(track_ / "ran"));
        QVERIFY(!tc::fs::exists(tmp_ / "lib" / ".trakcrate-lock"));                         // a refusal leaves no lock behind
    }
    void argumentsAndEnvironment()
    {
        put("track-script-env", "#!/bin/sh\n# no-snapshot\npwd\necho \"arg=$1\"\necho \"kind=$TRAKCRATE_SCRIPT_KIND name=$TRAKCRATE_SCRIPT_NAME api=$TRAKCRATE_API\"\n"
            "echo \"lib=$TRAKCRATE_LIBRARY\"\necho \"dir=$TRAKCRATE_TRACK_DIR\"\necho \"audio=$TRAKCRATE_TRACK_AUDIO\"\n"
            "echo \"pos=${TRAKCRATE_TRACK_POSITION_MS-none} deck=${TRAKCRATE_DECK-none} dry=${TRAKCRATE_DRY_RUN-none}\"\n"
            "read line; echo \"stdin=[$line]\"\n");
        ScriptRunner r;
        auto s = ready(tc::ScriptKind::Track, "env");
        ScriptResult a = run(r, s);
        QVERIFY2(a.ok, qPrintable(a.error + a.output));
        const QString dir = QString::fromStdString(track_.string());
        QVERIFY(a.output.contains(dir + "\n"));                                              // working directory
        QVERIFY(a.output.contains("arg=" + dir));
        QVERIFY(a.output.contains("kind=track name=env api=1"));
        QVERIFY(a.output.contains("lib=" + QString::fromStdString((tmp_ / "lib").string())));
        QVERIFY(a.output.contains("audio=" + QString::fromStdString(tc::audioFileOf(track_).string())));
        QVERIFY(!tc::audioFileOf(track_).empty());
        QVERIFY(a.output.contains("pos=none deck=none dry=none"));                          // absent, not empty
        QVERIFY(a.output.contains("stdin=[]"));                                              // stdin is closed
        tc::ScriptRun withDeck;
        withDeck.dryRun = true;
        withDeck.deck = 2;
        withDeck.positionMs = 1234;
        a = run(r, s, withDeck);
        QVERIFY(a.output.contains("pos=1234 deck=2 dry=1"));
        QVERIFY(a.durationMs >= 0);
        QVERIFY(!tc::fs::exists(tmp_ / "lib" / ".trakcrate-lock"));
    }
    void exitStatusAndLog()
    {
        put("track-script-bad", "#!/bin/sh\n# no-snapshot\necho oops >&2\nexit 3\n");
        ScriptRunner r;
        QSignalSpy spy(&r, &ScriptRunner::finished);
        ScriptResult a = run(r, ready(tc::ScriptKind::Track, "bad"));
        QVERIFY(a.started && !a.ok);
        QCOMPARE(a.exitCode, 3);
        QVERIFY(a.output.contains("oops"));                                                  // stderr is captured too
        QCOMPARE(spy.count(), 1);
        const std::string log = slurp(tc::scriptsDir(tmp_ / "lib") / ".log");
        QVERIFY(log.find("track-script-bad") != std::string::npos);
        QVERIFY(log.find("failed (3)") != std::string::npos);
    }
    void aScriptThatCannotStartIsReported()
    {
        put("track-script-nobang", "echo hi\n");                                              // no shebang: execve fails
        ScriptRunner r;
        ScriptResult a = run(r, ready(tc::ScriptKind::Track, "nobang"));
        QVERIFY(!a.ok);
        QVERIFY(!a.error.isEmpty() || a.exitCode != 0);
        QVERIFY(!r.busy());
        QVERIFY(!tc::fs::exists(tmp_ / "lib" / ".trakcrate-lock"));
    }
    void timeoutKillsTheWholeProcessGroup()
    {
        const std::string pidFile = (tmp_ / "child.pid").string();
        put("track-script-slow", "#!/bin/sh\n# no-snapshot\nsleep 60 &\necho $! > " + pidFile + "\nwait\n");
        ScriptRunner r;
        ScriptRunner::Options o;
        o.timeoutMs = 300;
        o.killDelayMs = 500;
        ScriptResult a = run(r, ready(tc::ScriptKind::Track, "slow"), tc::ScriptRun(), o);
        QVERIFY(a.timedOut);
        QVERIFY(!a.ok);
        QVERIFY(a.durationMs < 10000);
        const long pid = std::stol(slurp(pidFile));
        QTest::qWait(300);
        int tries = 0;
        while (::kill(pid_t(pid), 0) == 0 && tries++ < 40) {                                   // a zombie still answers kill 0: look at /proc
            QFile st(QString("/proc/%1/stat").arg(pid));
            if (st.open(QIODevice::ReadOnly) && st.readAll().contains(") Z ")) break;
            QTest::qWait(50);
        }
        QFile st(QString("/proc/%1/stat").arg(pid));
        const bool gone = !st.open(QIODevice::ReadOnly) || st.readAll().contains(") Z ");
        QVERIFY2(gone, "the child of the script is still running");
    }
    void cancelStopsAScript()
    {
        put("track-script-wait", "#!/bin/sh\n# no-snapshot\nsleep 60\n");
        ScriptRunner r;
        tc::ScriptRun run;
        run.libraryRoot = tmp_ / "lib";
        run.trackDir = track_;
        QString err;
        QVERIFY(r.start(ready(tc::ScriptKind::Track, "wait"), run, ScriptRunner::Options(), &err));
        QVERIFY(r.busy());
        QVERIFY(!r.start(ready(tc::ScriptKind::Track, "wait"), run, ScriptRunner::Options(), &err));      // one at a time
        QVERIFY(err.contains("already running"));
        QTest::qWait(100);
        r.cancel();
        QVERIFY(r.waitForFinished(15000));
        QVERIFY(r.lastResult().cancelled);
        QVERIFY(!r.lastResult().ok);
    }
    void outputIsCapped()
    {
        put("track-script-big", "#!/bin/sh\n# no-snapshot\nhead -c 3000000 /dev/zero | tr '\\0' 'x'\n");
        ScriptRunner r;
        ScriptResult a = run(r, ready(tc::ScriptKind::Track, "big"));
        QVERIFY(a.ok);
        QVERIFY(a.truncated);
        QCOMPARE(a.output.size(), int(ScriptRunner::kMaxOutput));
    }
    void snapshotAndUndoOfATrackScript()
    {
        put("track-script-wreck", "#!/bin/sh\necho wrecked > meta.txt\necho extra > newfile.txt\nrm -f beatgrid.txt\n");
        const std::string meta = slurp(track_ / "meta.txt");
        const bool hadGrid = tc::fs::exists(track_ / "beatgrid.txt");
        ScriptRunner r;
        ScriptResult a = run(r, ready(tc::ScriptKind::Track, "wreck"));
        QVERIFY2(a.ok, qPrintable(a.output));
        QVERIFY(!a.snapshot.isEmpty());
        QVERIFY(slurp(track_ / "meta.txt") == "wrecked\n");
        const tc::fs::path snap = a.snapshot.toStdString();
        QVERIFY(tc::fs::exists(snap / "meta.txt"));
        QVERIFY(tc::fs::exists(tc::audioFileOf(track_)));
        QVERIFY(!tc::fs::exists(snap / tc::audioFileOf(track_).filename()));                                            // the audio is never snapshotted
        QVERIFY(snap.string().find(".trakcrate-history") != std::string::npos);
        QCOMPARE(tc::lastSnapshot(track_).string(), snap.string());

        ScriptManager m(nullptr);
        m.setLibrary(QString::fromStdString((tmp_ / "lib").string()));
        QString err;
        QVERIFY2(m.undoLast(QString::fromStdString(track_.string()), &err), qPrintable(err));
        QVERIFY(slurp(track_ / "meta.txt") == meta);
        QCOMPARE(tc::fs::exists(track_ / "beatgrid.txt"), hadGrid);
        QVERIFY(tc::fs::exists(tc::audioFileOf(track_)));
        QVERIFY(m.undoLast(QString::fromStdString(track_.string()), &err));                  // harmless twice
        QVERIFY(!m.undoLast(QString::fromStdString((tmp_ / "src").string()), &err));         // nothing to restore there
    }
    void noSnapshotHeaderIsHonoured()
    {
        put("track-script-quiet", "#!/bin/sh\n# no-snapshot\ntrue\n");
        ScriptRunner r;
        ScriptResult a = run(r, ready(tc::ScriptKind::Track, "quiet"));
        QVERIFY(a.ok);
        QVERIFY(a.snapshot.isEmpty());
        QVERIFY(!tc::fs::exists(track_ / ".trakcrate-history"));
    }
    void crateScriptSnapshotsTheMetadataButNotAudioOrImages()
    {
        put("crate-script-touch", "#!/bin/sh\necho \"cwd=$(pwd)\"\necho x >> track-master-copy/src/a/meta.txt\n");
        std::ofstream(track_ / "artwork.jpg") << "jpegbytes";
        const std::string meta = slurp(track_ / "meta.txt");
        ScriptRunner r;
        tc::ScriptRun cr;
        ScriptResult a = run(r, ready(tc::ScriptKind::Crate, "touch"), cr);
        QVERIFY2(a.ok, qPrintable(a.error + a.output));
        QVERIFY(a.output.contains("cwd=" + QString::fromStdString((tmp_ / "lib").string())));
        const tc::fs::path snap = a.snapshot.toStdString();
        QVERIFY(tc::fs::exists(snap / "track-master-copy" / "src" / "a" / "meta.txt"));
        QVERIFY(!tc::fs::exists(snap / "track-master-copy" / "src" / "a" / tc::audioFileOf(track_).filename()));
        QVERIFY(!tc::fs::exists(snap / "track-master-copy" / "src" / "a" / "artwork.jpg"));
        QVERIFY(slurp(track_ / "meta.txt") != meta);
        ScriptManager m(nullptr);
        m.setLibrary(QString::fromStdString((tmp_ / "lib").string()));
        QVERIFY(m.undoLast(QString()));
        QVERIFY(slurp(track_ / "meta.txt") == meta);
    }
    void libraryLock()
    {
        put("track-script-t", "#!/bin/sh\n# no-snapshot\ntrue\n");
        const auto lock = tmp_ / "lib" / ".trakcrate-lock";
        std::ofstream(lock) << ::getpid() << "\n";                                           // held by a live process (this one)
        ScriptRunner r;
        tc::ScriptRun run;
        run.libraryRoot = tmp_ / "lib";
        run.trackDir = track_;
        QString err;
        QVERIFY(!r.start(ready(tc::ScriptKind::Track, "t"), run, ScriptRunner::Options(), &err));
        QVERIFY(err.contains("locked"));
        QVERIFY(tc::fs::exists(lock));                                                        // not removed: it is not ours
        std::ofstream(lock) << 999999999 << "\n";                                             // a dead process
        QVERIFY2(r.start(ready(tc::ScriptKind::Track, "t"), run, ScriptRunner::Options(), &err), qPrintable(err));
        QVERIFY(r.waitForFinished());
        QVERIFY(r.lastResult().ok);
        QVERIFY(!tc::fs::exists(lock));
    }
    void newScriptTemplateRuns()
    {
        ScriptManager m(nullptr);
        m.setLibrary(QString::fromStdString((tmp_ / "lib").string()));
        QString path, err;
        QVERIFY(!m.newScript(tc::ScriptKind::Track, "../evil", &path, &err));
        QVERIFY(!m.newScript(tc::ScriptKind::Track, "", &path, &err));
        QVERIFY(m.newScript(tc::ScriptKind::Track, "mine", &path, &err));
        QVERIFY(!m.newScript(tc::ScriptKind::Track, "mine", &path, &err));                    // taken
        QVERIFY(m.newScript(tc::ScriptKind::Crate, "all", &path, &err));
        auto s = m.scripts(tc::ScriptKind::Track);
        QCOMPARE(int(s.size()), 1);
        QCOMPARE(s[0].state, tc::ScriptState::New);                                           // a new file still has to be reviewed
        QVERIFY(!s[0].description.empty());
        tc::ScriptInfo ok = ready(tc::ScriptKind::Track, "mine");
        QVERIFY(ok.runnable());
        ScriptRunner r;
        ScriptResult a = run(r, ok);
        QVERIFY2(a.ok, qPrintable(a.error + a.output));
        QVERIFY(a.output.contains("trakcrate: running on"));
    }
};

QTEST_MAIN(TestTrakcrateScripts)
#include "test_trakcrate_scripts.moc"
