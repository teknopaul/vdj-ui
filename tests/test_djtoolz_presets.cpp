#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "audio/djtoolz_presets.h"
#include "ui/sample_tree_model.h"

using namespace vdj;

class TestDjtoolzPresets : public QObject
{
    Q_OBJECT

private slots:
    void roundTripEveryToolType();
    void presetsListSaveLoadRemoveRename();
    void sanitisingNames();
    void forwardCompatibility();
    void badJson();
    void unknownToolsSkippedWithWarning();
    void missingSampleStaysInTheTool();
    void currentStateIsRestored();
    void currentStateSurvivesCrashLeftovers();
    void treeModelShowsFoldersAndWavsOnly();

private:
    static QByteArray readAll(const QString& p)
    {
        QFile f(p);
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    }
    static std::vector<std::shared_ptr<DjtoolzTool>> oneOfEach()
    {
        std::vector<std::shared_ptr<DjtoolzTool>> v;
        int n = 0;
        for (auto t : { DjtoolzTool::Bombomatic, DjtoolzTool::StabPlayer, DjtoolzTool::AcidLoop, DjtoolzTool::OneShot, DjtoolzTool::MidiMatic, DjtoolzTool::Dembomatik }) {
            auto m = DjtoolzTool::create(t);
            ToolSettings s = m->settings();
            s.name = QString("tool %1 é").arg(++n);       // non-ASCII survives
            s.enabled = (n % 2) == 1;
            s.samplePath = QString("/samples/kick%1.wav").arg(n);
            s.gain = 0.75f;
            s.offsetMs = n * 3.5 - 8;
            s.beatMask = quint8(n + 4);
            s.reverseSync = (n == 2);
            s.quantize = (n != 4);
            s.stretch = (n != 3);
            s.manualBpm = 93.5;
            s.midiDevice = "20:1";
            s.midiChannel = n + 2;
            s.midiNote = 30 + n;
            s.midiVelocity = 90 + n;
            m->setSettings(s);
            v.push_back(m);
        }
        return v;
    }
};

void TestDjtoolzPresets::roundTripEveryToolType()
{
    QTemporaryDir dir;
    DjtoolzPresets p(dir.path());
    const auto mods = oneOfEach();
    QVERIFY(p.save("Friday set", djtoolzStateFrom(mods, "/samples", 0.8)));
    DjtoolzState st;
    QVERIFY(p.load("Friday set", &st));
    QCOMPARE(st.folder, QString("/samples"));
    QCOMPARE(st.masterGain, 0.8);
    QStringList warnings;
    const auto back = djtoolzToolsFrom(st, &warnings);
    QVERIFY(warnings.isEmpty());
    QCOMPARE(int(back.size()), 6);
    for (size_t i = 0; i < back.size(); ++i) {
        QCOMPARE(back[i]->type(), mods[i]->type());
        QVERIFY(back[i]->id() != mods[i]->id());
        QCOMPARE(back[i]->toVariant(), mods[i]->toVariant());     // every setting survives
    }
}

void TestDjtoolzPresets::presetsListSaveLoadRemoveRename()
{
    QTemporaryDir dir;
    DjtoolzPresets p(dir.path());
    QVERIFY(p.list().isEmpty());
    DjtoolzState st;
    st.folder = "/a";
    QVERIFY(p.save("b set", st));
    QVERIFY(p.save("A set", st));
    QCOMPARE(p.list(), QStringList({ "A set", "b set" }));
    QVERIFY(p.exists("A set"));
    st.folder = "/changed";
    QVERIFY(p.save("A set", st));                              // overwrite
    DjtoolzState back;
    QVERIFY(p.load("A set", &back));
    QCOMPARE(back.folder, QString("/changed"));
    QVERIFY(!p.rename("A set", "b set"));                       // target exists
    QVERIFY(p.rename("A set", "C set"));
    QCOMPARE(p.list(), QStringList({ "b set", "C set" }));
    QVERIFY(p.remove("b set"));
    QVERIFY(!p.remove("b set"));
    QCOMPARE(p.list(), QStringList({ "C set" }));
    QVERIFY(!p.load("nope", &back));
    QVERIFY(!p.save("", st));
    QVERIFY(!p.save("...", st));
}

void TestDjtoolzPresets::sanitisingNames()
{
    QCOMPARE(DjtoolzPresets::sanitise("  my/set:1  "), QString("my_set_1"));
    QCOMPARE(DjtoolzPresets::sanitise("../../etc/passwd"), QString("_.._etc_passwd"));
    QCOMPARE(DjtoolzPresets::sanitise(".hidden"), QString("hidden"));
    QCOMPARE(DjtoolzPresets::sanitise("..."), QString());
    QCOMPARE(DjtoolzPresets::sanitise(QString(200, 'x')).size(), 64);
    QCOMPARE(DjtoolzPresets::sanitise("Fiesta-2_ok.v2"), QString("Fiesta-2_ok.v2"));
    QTemporaryDir dir;
    DjtoolzPresets p(dir.path());
    DjtoolzState st;
    QVERIFY(p.save("../escape", st));                           // cannot leave the preset folder
    QVERIFY(QFile::exists(p.presetDir() + "/_escape.json"));
    QCOMPARE(p.list().size(), 1);
    QVERIFY(!QFile::exists(dir.path() + "/escape.json"));
}

void TestDjtoolzPresets::forwardCompatibility()
{
    QTemporaryDir dir;
    DjtoolzPresets p(dir.path());
    QDir().mkpath(p.presetDir());
    QFile f(p.presetPath("future"));
    f.open(QIODevice::WriteOnly);
    f.write(R"({"version":7,"folder":"/x","masterGain":0.5,"someNewThing":[1,2,3],
              "tools":[{"type":"stab","name":"s","beatMask":5,"newKey":"ignored"}]})");
    f.close();
    DjtoolzState st;
    QVERIFY(p.load("future", &st));
    QCOMPARE(st.folder, QString("/x"));
    const auto mods = djtoolzToolsFrom(st);
    QCOMPARE(int(mods.size()), 1);
    QCOMPARE(mods[0]->settings().beatMask, quint8(5));
    QCOMPARE(mods[0]->settings().gain, 1.0f);                   // keys the file does not have: defaults
    // an old file without masterGain
    QFile g(p.presetPath("old"));
    g.open(QIODevice::WriteOnly);
    g.write(R"({"tools":[]})");
    g.close();
    QVERIFY(p.load("old", &st));
    QCOMPARE(st.masterGain, 1.0);
}

void TestDjtoolzPresets::badJson()
{
    QTemporaryDir dir;
    DjtoolzPresets p(dir.path());
    QDir().mkpath(p.presetDir());
    for (const char* text : { "", "{not json", "[1,2,3]", "42" }) {
        QFile f(p.presetPath("bad"));
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(text);
        f.close();
        DjtoolzState st;
        QString err;
        QVERIFY2(!p.load("bad", &st, &err), text);
        QVERIFY(!err.isEmpty());
    }
}

void TestDjtoolzPresets::unknownToolsSkippedWithWarning()
{
    DjtoolzState st;
    st.tools = QVariantList{ QVariantMap{ { "type", "bombomatic" } }, QVariantMap{ { "type", "theremin" } }, 42,
                               QVariantMap{ { "type", "midi" } } };
    QStringList warnings;
    const auto mods = djtoolzToolsFrom(st, &warnings);
    QCOMPARE(int(mods.size()), 2);
    QCOMPARE(warnings.size(), 2);
    QVERIFY(warnings[0].contains("theremin"));
}

void TestDjtoolzPresets::missingSampleStaysInTheTool()
{
    DjtoolzState st;
    st.tools = QVariantList{ QVariantMap{ { "type", "oneshot" }, { "sample", "/gone/forever.wav" } } };
    const auto mods = djtoolzToolsFrom(st);
    QCOMPARE(int(mods.size()), 1);
    QCOMPARE(mods[0]->settings().samplePath, QString("/gone/forever.wav"));
}

void TestDjtoolzPresets::currentStateIsRestored()
{
    QTemporaryDir dir;
    DjtoolzPresets p(dir.path());
    DjtoolzState st;
    QString err;
    QVERIFY(!p.loadCurrent(&st, &err));                         // first start: nothing yet
    const auto mods = oneOfEach();
    QVERIFY(p.saveCurrent(djtoolzStateFrom(mods, "/lib", 0.6)));
    QVERIFY(p.list().isEmpty());                                // the working state is not a preset
    DjtoolzPresets again(dir.path());                           // "next start"
    QVERIFY(again.loadCurrent(&st));
    QCOMPARE(st.folder, QString("/lib"));
    QCOMPARE(int(djtoolzToolsFrom(st).size()), 6);
}

void TestDjtoolzPresets::currentStateSurvivesCrashLeftovers()
{
    QTemporaryDir dir;
    DjtoolzPresets p(dir.path());
    QVERIFY(p.saveCurrent(djtoolzStateFrom(oneOfEach(), "/one", 1.0)));
    QVERIFY(p.saveCurrent(djtoolzStateFrom(oneOfEach(), "/two", 1.0)));
    DjtoolzState st;
    QVERIFY(p.loadCurrent(&st));
    QCOMPARE(st.folder, QString("/two"));
    // no stray temp files next to it
    QCOMPARE(QDir(dir.path()).entryList(QStringList{ "djtoolz-current*" }, QDir::Files).size(), 1);
}

void TestDjtoolzPresets::treeModelShowsFoldersAndWavsOnly()
{
    QTemporaryDir dir;
    QDir d(dir.path());
    d.mkpath("drums/kicks");
    d.mkpath("empty");
    for (const char* f : { "drums/kicks/k1.wav", "drums/kicks/K2.WAV", "drums/notes.txt", "drums/song.mp3", "top.wav" }) {
        QFile file(d.filePath(f));
        file.open(QIODevice::WriteOnly);
        file.write("x");
    }
    SampleTreeModel model;
    QSignalSpy loaded(&model, &QFileSystemModel::directoryLoaded);
    const QModelIndex root = model.setRootFolder(dir.path());
    QVERIFY(root.isValid());
    QCOMPARE(model.rootFolder(), QFileInfo(dir.path()).absoluteFilePath());
    auto wait = [&](const QString& path) {
        const QModelIndex i = model.indexForPath(path);
        for (int n = 0; n < 200 && model.canFetchMore(i); ++n) { model.fetchMore(i); QTest::qWait(10); }
        QTest::qWait(150);
    };
    wait(dir.path());
    wait(d.filePath("drums"));
    wait(d.filePath("drums/kicks"));
    QStringList rootNames;
    for (int r = 0; r < model.rowCount(root); ++r)
        rootNames << model.fileName(model.index(r, 0, root));
    rootNames.sort();
    QCOMPARE(rootNames, QStringList({ "drums", "empty", "top.wav" }));
    const QModelIndex drums = model.indexForPath(d.filePath("drums"));
    QCOMPARE(model.rowCount(drums), 1);                         // only kicks: notes.txt and song.mp3 are hidden
    const QModelIndex kicks = model.indexForPath(d.filePath("drums/kicks"));
    QCOMPARE(model.rowCount(kicks), 2);                         // k1.wav and K2.WAV (any case)
    QVERIFY(model.isSample(model.indexForPath(d.filePath("top.wav"))));
    QVERIFY(!model.isSample(drums));
    QCOMPARE(model.samplePath(model.indexForPath(d.filePath("top.wav"))), d.filePath("top.wav"));
    QVERIFY(model.samplePath(drums).isEmpty());
    QVERIFY(model.samplePath(QModelIndex()).isEmpty());
}

QTEST_MAIN(TestDjtoolzPresets)
#include "test_djtoolz_presets.moc"
