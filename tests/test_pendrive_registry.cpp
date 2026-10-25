#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "export/pendrive_registry.h"

using namespace vdj;

namespace {

void makeExport(const QString& folder)
{
    QVERIFY(QDir().mkpath(folder + QStringLiteral("/PIONEER/rekordbox")));
    QFile f(folder + QStringLiteral("/PIONEER/rekordbox/export.pdb"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
}

} // namespace

class TestPendriveRegistry : public QObject {
    Q_OBJECT

private slots:
    void addRenameRemoveAndValidate()
    {
        QTemporaryDir t;
        makeExport(t.path() + "/a");
        makeExport(t.path() + "/b");
        QDir().mkpath(t.path() + "/empty");

        PendriveRegistry r;
        QSignalSpy changed(&r, &PendriveRegistry::changed);
        QVERIFY(r.add("Stick A", t.path() + "/a").isEmpty());
        QVERIFY(r.add("", t.path() + "/b").isEmpty());                         // default name: the folder's
        QCOMPARE(r.virtualPendrives().at(1).name, QStringLiteral("b"));
        QVERIFY(!r.add("Other", t.path() + "/a").isEmpty());                   // duplicate folder
        QVERIFY(!r.add("stick a", t.path() + "/b/").isEmpty());                // duplicate name (any case), same folder
        QVERIFY(!r.add("Empty", t.path() + "/empty").isEmpty());               // no export.pdb
        QVERIFY(!r.add("Gone", t.path() + "/nope").isEmpty());
        QCOMPARE(r.virtualPendrives().size(), 2);
        QVERIFY(r.rename(1, "Stick B").isEmpty());
        QVERIFY(!r.rename(1, "STICK A").isEmpty());
        QVERIFY(!r.rename(1, "  ").isEmpty());
        QVERIFY(r.rename(0, "stick a").isEmpty());                              // itself, other case: fine
        QCOMPARE(r.indexOfFolder(t.path() + "/b"), 1);
        QVERIFY(r.remove(0));
        QVERIFY(!r.remove(5));
        QCOMPARE(r.virtualPendrives().size(), 1);
        QVERIFY(changed.count() >= 4);
    }

    void persistsAsLines()
    {
        PendriveRegistry r;
        r.fromLines({ "One\t/media/x/one", "Two words\t/srv/two", "broken line", "One\t/dup/name", "Three\t/srv/two" });
        QCOMPARE(r.virtualPendrives().size(), 2);                               // broken, duplicate name and duplicate folder dropped
        PendriveRegistry r2;
        r2.fromLines(r.toLines());
        QCOMPARE(r2.toLines(), r.toLines());
        QCOMPARE(r2.virtualPendrives().at(1).name, QStringLiteral("Two words"));
    }

    void scansAFakeMediaTree()
    {
        QTemporaryDir t;
        makeExport(t.path() + "/STICK1");
        QDir().mkpath(t.path() + "/PLAIN");
        QFile f(t.path() + "/file.txt");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        const auto l = PendriveRegistry::scanPhysicalIn(t.path());
        QCOMPARE(l.size(), 2);                                                  // only folders, sorted by name
        QCOMPARE(l.at(0).name, QStringLiteral("PLAIN"));
        QVERIFY(!l.at(0).hasRekordbox);
        QCOMPARE(l.at(1).name, QStringLiteral("STICK1"));
        QVERIFY(l.at(1).hasRekordbox);
        QVERIFY(PendriveRegistry::scanPhysicalIn(t.path() + "/missing").isEmpty());
    }

    void defaultName()
    {
        QCOMPARE(PendriveRegistry::defaultNameFor("/media/me/MY-STICK/"), QStringLiteral("MY-STICK"));
        QCOMPARE(PendriveRegistry::defaultNameFor("/"), QStringLiteral("VDJ-USB"));
    }
};

QTEST_MAIN(TestPendriveRegistry)
#include "test_pendrive_registry.moc"
