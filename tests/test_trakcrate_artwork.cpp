#include <QtTest/QtTest>

#include <QBuffer>
#include <QImage>
#include <QPainter>

#include "ui/trakcrate_artwork.h"

using namespace vdj;

namespace {

QByteArray encodeImage(const QImage& img, const char* format)
{
    QByteArray out;
    QBuffer b(&out);
    b.open(QIODevice::WriteOnly);
    img.save(&b, format);
    return out;
}

// 0xFF 0xC0 (baseline) or 0xFF 0xC2 (progressive) start-of-frame markers
bool hasMarker(const QByteArray& jpeg, uchar code)
{
    for (int i = 0; i + 1 < jpeg.size(); ++i)
        if (uchar(jpeg[i]) == 0xFF && uchar(jpeg[i + 1]) == code) return true;
    return false;
}

} // namespace

class TestTrakcrateArtwork : public QObject
{
    Q_OBJECT

private slots:
    void squareJpegFromPng();
    void centreCropNotStretch();
    void transparencyBecomesWhite();
    void baselineAndPlain();
    void deterministic();
    void formatsAndErrors();
};

void TestTrakcrateArtwork::squareJpegFromPng()
{
    QImage src(300, 200, QImage::Format_ARGB32);
    src.fill(QColor(200, 30, 30));
    QString err;
    QByteArray jpg = encodeArtworkJpeg(encodeImage(src, "PNG"), 240, &err);
    QVERIFY2(!jpg.isEmpty(), qPrintable(err));
    QCOMPARE(uchar(jpg[0]), uchar(0xFF));
    QCOMPARE(uchar(jpg[1]), uchar(0xD8));                                  // SOI
    QCOMPARE(uchar(jpg[jpg.size() - 2]), uchar(0xFF));
    QCOMPARE(uchar(jpg[jpg.size() - 1]), uchar(0xD9));                     // EOI
    QImage back = QImage::fromData(jpg, "JPEG");
    QCOMPARE(back.width(), 240);
    QCOMPARE(back.height(), 240);
    QColor c = back.pixelColor(120, 120);
    QVERIFY(c.red() > 180 && c.green() < 60 && c.blue() < 60);
    QByteArray small = encodeArtworkJpeg(encodeImage(src, "PNG"), 80, &err);
    QCOMPARE(QImage::fromData(small, "JPEG").size(), QSize(80, 80));
    QVERIFY(small.size() < jpg.size());
}

void TestTrakcrateArtwork::centreCropNotStretch()
{
    // 400 x 200: the left quarter red, the middle half green, the right quarter blue. A centre crop to 200 x 200 shows only green.
    QImage src(400, 200, QImage::Format_RGB32);
    QPainter p(&src);
    p.fillRect(0, 0, 100, 200, QColor(255, 0, 0));
    p.fillRect(100, 0, 200, 200, QColor(0, 255, 0));
    p.fillRect(300, 0, 100, 200, QColor(0, 0, 255));
    p.end();
    QByteArray jpg = encodeArtworkJpeg(encodeImage(src, "PNG"), 100);
    QImage back = QImage::fromData(jpg, "JPEG");
    QCOMPARE(back.size(), QSize(100, 100));
    for (QPoint pt : {QPoint(10, 50), QPoint(50, 50), QPoint(90, 50), QPoint(50, 5)}) {
        QColor c = back.pixelColor(pt);
        QVERIFY2(c.green() > 200 && c.red() < 60 && c.blue() < 60, qPrintable(QString("(%1,%2) is %3").arg(pt.x()).arg(pt.y()).arg(c.name())));
    }
    // a portrait image is cropped the same way
    QImage tall(200, 400, QImage::Format_RGB32);
    QPainter q(&tall);
    q.fillRect(0, 0, 200, 100, QColor(255, 0, 0));
    q.fillRect(0, 100, 200, 200, QColor(0, 255, 0));
    q.fillRect(0, 300, 200, 100, QColor(0, 0, 255));
    q.end();
    QImage backTall = QImage::fromData(encodeArtworkJpeg(encodeImage(tall, "PNG"), 100), "JPEG");
    QVERIFY(backTall.pixelColor(50, 5).green() > 200);
    QVERIFY(backTall.pixelColor(50, 95).green() > 200);
}

void TestTrakcrateArtwork::transparencyBecomesWhite()
{
    QImage src(64, 64, QImage::Format_ARGB32);
    src.fill(Qt::transparent);
    QPainter p(&src);
    p.fillRect(0, 0, 32, 64, QColor(0, 0, 0, 255));
    p.end();
    QImage back = QImage::fromData(encodeArtworkJpeg(encodeImage(src, "PNG"), 64), "JPEG");
    QVERIFY(back.pixelColor(8, 32).red() < 40);                            // opaque black stays black
    QVERIFY(back.pixelColor(56, 32).red() > 215 && back.pixelColor(56, 32).green() > 215);    // transparent becomes white
    QVERIFY(!back.hasAlphaChannel());
}

void TestTrakcrateArtwork::baselineAndPlain()
{
    QImage src(500, 500, QImage::Format_RGB32);
    for (int y = 0; y < 500; ++y)
        for (int x = 0; x < 500; ++x) src.setPixel(x, y, qRgb(x / 2, y / 2, (x + y) / 4));
    QByteArray jpg = encodeArtworkJpeg(encodeImage(src, "PNG"), 240);
    QVERIFY(hasMarker(jpg, 0xC0));                                         // baseline DCT
    QVERIFY(!hasMarker(jpg, 0xC2));                                        // not progressive
    QVERIFY(!hasMarker(jpg, 0xE1));                                        // no EXIF block
    QVERIFY(jpg.size() < 100000);
}

void TestTrakcrateArtwork::deterministic()
{
    QImage src(120, 90, QImage::Format_RGB32);
    src.fill(QColor(10, 120, 200));
    QByteArray png = encodeImage(src, "PNG");
    QCOMPARE(encodeArtworkJpeg(png, 80), encodeArtworkJpeg(png, 80));
    QVERIFY(encodeArtworkJpeg(png, 80, nullptr, 50).size() <= encodeArtworkJpeg(png, 80, nullptr, 95).size());
}

void TestTrakcrateArtwork::formatsAndErrors()
{
    QImage src(50, 50, QImage::Format_RGB32);
    src.fill(Qt::blue);
    for (const char* fmt : {"PNG", "JPEG", "BMP"}) {
        QString err;
        QByteArray jpg = encodeArtworkJpeg(encodeImage(src, fmt), 64, &err);
        QVERIFY2(!jpg.isEmpty(), fmt);
        QCOMPARE(QImage::fromData(jpg, "JPEG").size(), QSize(64, 64));     // a small source is enlarged, not refused
    }
    QString err;
    QVERIFY(encodeArtworkJpeg(QByteArray("this is not an image"), 64, &err).isEmpty());
    QVERIFY(!err.isEmpty());
    QVERIFY(encodeArtworkJpeg(QByteArray(), 64, &err).isEmpty());
    QVERIFY(encodeArtworkJpeg(encodeImage(src, "PNG"), 8, &err).isEmpty());
    QVERIFY(encodeArtworkJpeg(encodeImage(src, "PNG"), 100000, &err).isEmpty());
}

QTEST_MAIN(TestTrakcrateArtwork)
#include "test_trakcrate_artwork.moc"
