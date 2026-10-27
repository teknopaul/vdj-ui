#ifndef TRAKCRATE_ARTWORK_H
#define TRAKCRATE_ARTWORK_H

#include <QByteArray>
#include <QString>

namespace vdj {

// Turns any image Qt can read (jpg, png, webp, gif, bmp ...) into the JPEG a player wants: a square of `size` pixels (centre crop, no
// stretching), 8 bit sRGB RGB without alpha (transparency becomes white), baseline (not progressive) encoding, no metadata. Empty result
// and *err on failure. Deterministic: the same input and size give the same bytes.
QByteArray encodeArtworkJpeg(const QByteArray& source, int size, QString* err = nullptr, int quality = 90);

} // namespace vdj

#endif
