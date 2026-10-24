#include <QtTest/QtTest>
#include "nfs/id3_tags.h"
#include "ui/track_browser.h"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QTreeWidget>
#include <QTextStream>

class TestTracks : public QObject
{
    Q_OBJECT

private slots:
    void id3v2Parsing();
    void id3v1Fallback();
    void trackBrowserScans();

private:
    static void writeFrame(QByteArray* out, const char* id, const QByteArray& text)
    {
        QByteArray body;
        body.append(char(3)); // UTF-8 encoding
        body.append(text);
        quint32 size = static_cast<quint32>(body.size());
        out->append(id, 4);
        out->append(char((size >> 24) & 0xff));
        out->append(char((size >> 16) & 0xff));
        out->append(char((size >> 8) & 0xff));
        out->append(char(size & 0xff));
        out->append(char(0)); // flags
        out->append(char(0));
        out->append(body);
    }
};

void TestTracks::id3v2Parsing()
{
    QByteArray tag("ID3");
    tag.append(char(3)).append(char(0)); // v2.3
    tag.append(char(0)); // flags

    QByteArray framesHeader;
    writeFrame(&framesHeader, "TIT2", "Acid Test");
    writeFrame(&framesHeader, "TPE1", "Someone");
    writeFrame(&framesHeader, "TKEY", "8A");
    writeFrame(&framesHeader, "TBPM", "133");

    // size is syncsafe in the tag header
    quint32 sz = static_cast<quint32>(framesHeader.size());
    tag.append(char((sz >> 21) & 0x7f));
    tag.append(char((sz >> 14) & 0x7f));
    tag.append(char((sz >> 7) & 0x7f));
    tag.append(char(sz & 0x7f));
    tag.append(framesHeader);

    QTemporaryDir dir;
    QFile f(dir.filePath("track.mp3"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(tag);
    f.write(QByteArray("MPEG garbage"));
    f.close();

    vdj::TrackMeta meta = vdj::readId3Tags(f.fileName());
    QCOMPARE(meta.title, QStringLiteral("Acid Test"));
    QCOMPARE(meta.artist, QStringLiteral("Someone"));
    QCOMPARE(meta.key, QStringLiteral("8A"));
    QCOMPARE(meta.bpm, QStringLiteral("133"));
}

void TestTracks::id3v1Fallback()
{
    QTemporaryDir dir;
    QFile f(dir.filePath("oldy.mp3"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(1024, '\x00'));

    QByteArray title(30, '\x00');
    title[0] = 'O'; title[1] = 'l'; title[2] = 'd';
    QByteArray artist(30, '\x00');
    artist[0] = 'A'; artist[1] = 'r'; artist[2] = 't';
    QByteArray album(30, '\x00');
    QByteArray year(4, '\x00');
    QByteArray comment(30, '\x00');
    QByteArray genre(1, '\x00');

    QByteArray tag("TAG");
    tag.append(title).append(artist).append(album).append(year).append(comment).append(genre);
    QCOMPARE(tag.size(), 128);

    f.write(tag);
    f.close();

    vdj::TrackMeta meta = vdj::readId3Tags(f.fileName());
    QCOMPARE(meta.title, QStringLiteral("Old"));
    QCOMPARE(meta.artist, QStringLiteral("Art"));
}

void TestTracks::trackBrowserScans()
{
    QTemporaryDir dir;
    QDir d(dir.path());
    d.mkpath("sub");

    QFile f(d.filePath("sub/beat.aiff"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray("not a real audio file but the extension matches"));
    f.close();

    QFile txt(d.filePath("notes.txt"));
    QVERIFY(txt.open(QIODevice::WriteOnly));
    txt.write("ignore me");
    txt.close();

    vdj::TrackBrowser browser;
    browser.setMount(dir.path());

    QTreeWidget* tree = browser.treeWidget();
    QCOMPARE(tree->topLevelItemCount(), 1); // only the "sub" directory
    if (tree->topLevelItemCount() == 1) {
        QTreeWidgetItem* sub = tree->topLevelItem(0);
        QCOMPARE(sub->text(0), QStringLiteral("sub"));
        QCOMPARE(sub->childCount(), 1); // only beat.aiff, notes.txt ignored
    }
    QVERIFY(true);
}

QTEST_MAIN(TestTracks)
#include "test_tracks.moc"