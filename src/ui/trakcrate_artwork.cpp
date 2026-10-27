#include "trakcrate_artwork.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>

namespace vdj {

QByteArray encodeArtworkJpeg(const QByteArray& source, int size, QString* err, int quality)
{
    auto fail = [&](const QString& m) { if (err) *err = m; return QByteArray(); };
    if (size < 16 || size > 4096) return fail(QStringLiteral("bad size"));
    QBuffer in;
    in.setData(source);
    in.open(QIODevice::ReadOnly);
    QImageReader reader(&in);
    reader.setAutoTransform(true);                          // honour the orientation of camera photos
    QImage img = reader.read();
    if (img.isNull()) return fail(QStringLiteral("not an image Qt can read: %1").arg(reader.errorString()));
    if (img.width() < 1 || img.height() < 1) return fail(QStringLiteral("empty image"));

    const int side = qMin(img.width(), img.height());       // centre crop to a square
    QImage square = img.copy((img.width() - side) / 2, (img.height() - side) / 2, side, side);
    QImage scaled = square.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage rgb(size, size, QImage::Format_RGB32);
    rgb.fill(Qt::white);
    QPainter p(&rgb);
    p.drawImage(0, 0, scaled);                              // composes alpha over white
    p.end();

    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    QImageWriter writer(&buf, "jpeg");
    writer.setQuality(quality);
    writer.setProgressiveScanWrite(false);
    writer.setOptimizedWrite(false);
    if (!writer.write(rgb)) return fail(QStringLiteral("cannot encode: %1").arg(writer.errorString()));
    return out;
}

} // namespace vdj
