#include <QtTest/QtTest>

#include <QDir>
#include <QImage>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "trakcrate_wav.h"
#include "ui/artwork_launcher.h"
#include "ui/artwork_view.h"
#include "trakcrate/artwork_edit.h"
#include "trakcrate/importer.h"
#include "trakcrate/track.h"

namespace tc = trakcrate;
using namespace vdj;

class TestTrakcrateArtwork : public QObject
{
    Q_OBJECT

    tc::fs::path tmp_;
    tc::Library lib_;
    QString dir_;

    QString makeImage(const char* name, QColor c, const char* format)
    {
        QImage img(64, 64, QImage::Format_RGB32);
        img.fill(c);
        const QString p = QString::fromStdString((tmp_ / name).string());
        img.save(p, format);
        return p;
    }
    static std::string slurp(const tc::fs::path& p) { std::string s; tc::readFile(p, &s); return s; }

private slots:
    void init()
    {
        tmp_ = tc::makeTempDir("trakcrate-artwork");
        QVERIFY(!tmp_.empty());
        QVERIFY(tc::Library::create(tmp_ / "lib") && tc::Library::open(tmp_ / "lib", &lib_));
        tc::DecodedPcm p = tcwav::clicks(120, 0.25, 4);
        tcwav::writeWavMono(tmp_ / "src" / "a.wav", tcwav::mono(p), p.sampleRate);
        tc::ImportOptions io;
        QVERIFY(tc::importFolder(lib_, tmp_ / "src", io).ok);
        dir_ = QString::fromStdString(lib_.trackDir("src", "a").string());
    }
    void cleanup() { std::error_code ec; tc::fs::remove_all(tmp_, ec); }

    void desktopFilesAreParsed()
    {
        DesktopApp a;
        const QByteArray gimp = "[Desktop Entry]\nType=Application\nName=GIMP\nName[de]=GIMP de\nExec=gimp-2.10 %U\nMimeType=image/jpeg;image/png;\n[Desktop Action x]\nName=Other\nExec=nope\n";
        QVERIFY(parseDesktopEntry("gimp.desktop", gimp, &a));
        QCOMPARE(a.name, QString("GIMP"));
        QCOMPARE(a.exec, QString("gimp-2.10 %U"));
        QVERIFY(a.mimes.contains("image/png"));
        QVERIFY(!parseDesktopEntry("h.desktop", "[Desktop Entry]\nType=Application\nName=H\nExec=h\nNoDisplay=true\n", &a));
        QVERIFY(!parseDesktopEntry("l.desktop", "[Desktop Entry]\nType=Link\nName=L\nExec=l\n", &a));
        QVERIFY(!parseDesktopEntry("n.desktop", "[Desktop Entry]\nType=Application\nName=N\n", &a));
    }
    void scanFindsOnlyMatchingAppsOncePerId()
    {
        QTemporaryDir d1, d2;
        auto put = [](const QTemporaryDir& d, const char* file, const char* text) {
            QFile f(d.filePath(file));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
        };
        put(d1, "gimp.desktop", "[Desktop Entry]\nType=Application\nName=GIMP user\nExec=gimp-user %f\nMimeType=image/jpeg;\n");
        put(d2, "gimp.desktop", "[Desktop Entry]\nType=Application\nName=GIMP system\nExec=gimp %f\nMimeType=image/jpeg;\n");
        put(d2, "krita.desktop", "[Desktop Entry]\nType=Application\nName=Krita\nExec=krita %F\nMimeType=image/png;image/jpeg;\n");
        put(d2, "text.desktop", "[Desktop Entry]\nType=Application\nName=Text\nExec=ed %f\nMimeType=text/plain;\n");
        const QList<DesktopApp> apps = scanDesktopApps({d1.path(), d2.path()}, "image/jpeg");
        QCOMPARE(apps.size(), 2);
        QCOMPARE(apps[0].name, QString("GIMP user"));                  // the first directory wins
        QCOMPARE(apps[1].name, QString("Krita"));
    }
    void commandsAreBuiltWithoutAShell()
    {
        QString prog;
        QStringList args;
        QVERIFY(buildCommand("gimp %f", "/a b/art work.jpg;rm -rf x", &prog, &args));
        QCOMPARE(prog, QString("gimp"));
        QCOMPARE(args, QStringList({"/a b/art work.jpg;rm -rf x"}));      // one argument, whatever is in the name
        QVERIFY(buildCommand("\"/opt/my editor/ed\" --new %u %i %%", "/x.jpg", &prog, &args));
        QCOMPARE(prog, QString("/opt/my editor/ed"));
        QCOMPARE(args, QStringList({"--new", "/x.jpg", "%"}));
        QVERIFY(buildCommand("pinta", "/x.jpg", &prog, &args));           // no file code: appended
        QCOMPARE(args, QStringList({"/x.jpg"}));
        QVERIFY(!buildCommand("   ", "/x.jpg", &prog, &args));
        QString err;
        QVERIFY(!launchEditor("definitely-not-an-installed-program-xyz %f", "/x.jpg", &err));
        QVERIFY(!err.isEmpty());
    }
    void replaceKeepsOneBackupAndMarksEdited()
    {
        const tc::fs::path dir = dir_.toStdString();
        QVERIFY(tc::artworkOf(dir).empty());
        std::string err;
        QVERIFY(!tc::replaceArtwork(dir, tmp_ / "src" / "a.wav", &err));         // not an image
        QVERIFY(!err.empty());
        const QString red = makeImage("red.jpg", Qt::red, "JPG");
        const QString blue = makeImage("blue.png", Qt::blue, "PNG");
        QVERIFY(tc::replaceArtwork(dir, red.toStdString()));
        QCOMPARE(tc::artworkOf(dir).filename().string(), std::string("artwork.jpg"));
        const std::string redBytes = slurp(tc::artworkOf(dir));
        QVERIFY(!tc::fs::exists(dir / ".trakcrate-history"));                    // nothing to back up the first time
        tc::Track t;
        QVERIFY(tc::loadTrack(dir, &t));
        QCOMPARE(tc::origin(t.source, "artwork"), std::string("edited"));

        QVERIFY(tc::replaceArtwork(dir, blue.toStdString()));
        QCOMPARE(tc::artworkOf(dir).filename().string(), std::string("artwork.png"));
        QVERIFY(!tc::fs::exists(dir / "artwork.jpg"));                           // one artwork file only
        QVERIFY(slurp(dir / ".trakcrate-history" / "artwork.jpg") == redBytes);

        QVERIFY(tc::restoreArtwork(dir));                                        // the red one is back, the blue one is the backup
        QCOMPARE(tc::artworkOf(dir).filename().string(), std::string("artwork.jpg"));
        QVERIFY(slurp(tc::artworkOf(dir)) == redBytes);
        QVERIFY(tc::fs::exists(dir / ".trakcrate-history" / "artwork.png"));
        QVERIFY(!tc::fs::exists(dir / ".trakcrate-history" / "artwork.jpg"));

        QVERIFY(tc::removeArtwork(dir));
        QVERIFY(tc::artworkOf(dir).empty());
        QVERIFY(tc::fs::exists(dir / ".trakcrate-history" / "artwork.jpg"));
        QVERIFY(tc::removeArtwork(dir));                                         // nothing left: still fine
        QVERIFY(tc::restoreArtwork(dir));
        QVERIFY(!tc::artworkOf(dir).empty());
        QVERIFY(!tc::reextractArtwork(dir, &err));                               // a wav has no embedded cover
        QVERIFY(!err.empty());
        QVERIFY(!tc::artworkOf(dir).empty());                                    // and nothing was lost
    }
    void imageTypeComesFromTheBytes()
    {
        QCOMPARE(tc::imageExtensionOf(std::string("\xFF\xD8\xFF\xE0", 4)), std::string("jpg"));
        QCOMPARE(tc::imageExtensionOf(std::string("\x89PNG\r\n\x1a\nxx", 10)), std::string("png"));
        QCOMPARE(tc::imageExtensionOf("GIF89a"), std::string());
        QCOMPARE(tc::imageExtensionOf(""), std::string());
    }
    void viewReplacesRemovesAndNoticesAnExternalSave()
    {
        ArtworkView v;
        QSignalSpy changed(&v, &ArtworkView::artworkChanged);
        QString err;
        QVERIFY(!v.replaceWith("/nonexistent.jpg", &err));                       // no track yet
        v.setTrack(dir_);
        QVERIFY(!v.hasArtwork());
        QVERIFY(!v.openInEditor("gimp %f", &err));                               // nothing to edit
        QVERIFY(v.replaceWith(makeImage("one.jpg", Qt::green, "JPG")));
        QVERIFY(v.hasArtwork());
        QVERIFY(v.picture()->pixmap().width() > 0);
        QCOMPARE(changed.count(), 1);
        v.checkExternalChange();
        QCOMPARE(changed.count(), 1);                                            // our own change is not an external one

        // an editor saves over the file (write a temp file and rename it, like many editors)
        const tc::fs::path art = tc::artworkOf(dir_.toStdString());
        const QString tmpName = QString::fromStdString(art.string()) + ".tmp";
        QImage img(64, 64, QImage::Format_RGB32);
        img.fill(Qt::yellow);
        QVERIFY(img.save(tmpName, "JPG"));
        QFile::remove(QString::fromStdString(art.string()));
        QVERIFY(QFile::rename(tmpName, QString::fromStdString(art.string())));
        v.checkExternalChange();
        QCOMPARE(changed.count(), 2);
        tc::Track t;
        QVERIFY(tc::loadTrack(dir_.toStdString(), &t));
        QCOMPARE(tc::origin(t.source, "artwork"), std::string("edited"));
        v.checkExternalChange();
        QCOMPARE(changed.count(), 2);                                            // seen once

        QVERIFY(v.removeArtwork());
        QVERIFY(!v.hasArtwork());
        QVERIFY(v.restore());
        QVERIFY(v.hasArtwork());
        v.setTrack(QString());
        QVERIFY(!v.isEnabled());
    }
};

QTEST_MAIN(TestTrakcrateArtwork)
#include "test_trakcrate_artwork_edit.moc"
