#include "id3_tags.h"
#include <QFile>
#include <QFileInfo>
#include <QByteArray>

namespace vdj {

static QString decodeId3Text(const QByteArray& raw, int encoding)
{
    if (encoding == 0) return QString::fromLatin1(raw);
    if (encoding == 3) return QString::fromUtf8(raw);
    // encodings 1 and 2 are UTF-16; prefer LE (encoding 1) or BE (encoding 2)
    if (encoding == 1) {
        return QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData()),
                                  raw.size() / 2);
    }
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData()),
                              raw.size() / 2);
}

static unsigned syncsafe32(const unsigned char b[4])
{
    return ((b[0] & 0x7f) << 21) | ((b[1] & 0x7f) << 14) |
           ((b[2] & 0x7f) << 7) | (b[3] & 0x7f);
}

static QHash<QString, QString> parseId3v2(const QByteArray& data)
{
    QHash<QString, QString> tags;
    if (data.size() < 10)
        return tags;
    if (data[0] != 'I' || data[1] != 'D' || data[2] != '3')
        return tags;

    int major = static_cast<unsigned char>(data[3]);
    unsigned tagSize = 10 + syncsafe32(reinterpret_cast<const unsigned char*>(data.constData() + 6));
    if (tagSize > static_cast<unsigned>(data.size()))
        tagSize = static_cast<unsigned>(data.size());

    int pos = 10;
    while (pos + 10 <= static_cast<int>(tagSize)) {
        QByteArray id = data.mid(pos, 4);
        unsigned size;
        if (major >= 4) {
            size = syncsafe32(reinterpret_cast<const unsigned char*>(data.constData() + pos + 4));
        } else {
            size = static_cast<unsigned char>(data[pos + 7])
                 | (static_cast<unsigned char>(data[pos + 6]) << 8)
                 | (static_cast<unsigned char>(data[pos + 5]) << 16)
                 | (static_cast<unsigned char>(data[pos + 4]) << 24);
        }
        if (size == 0)
            break;
        pos += 10; // skip id4 + size4 + flags2

        if (id.startsWith('T') && pos + static_cast<int>(size) <= static_cast<int>(tagSize)) {
            QByteArray body = data.mid(pos, size);
            int enc = body.isEmpty() ? 0 : static_cast<unsigned char>(body[0]);
            QByteArray text = body.mid(1);
            if (!text.isEmpty() && (enc == 1 || enc == 2)) {
                // strip BOM
                if (text.size() >= 2 && text[0] == '\xff' && text[1] == '\xfe')
                    text = text.mid(2);
                else if (text.size() >= 2 && text[0] == '\xfe' && text[1] == '\xff')
                    text = text.mid(2);
            }
            QString value = decodeId3Text(text, enc).trimmed();
            const char* name = nullptr;
            if      (id == "TIT2") name = "title";
            else if (id == "TPE1") name = "artist";
            else if (id == "TALB") name = "album";
            else if (id == "TBPM") name = "bpm";
            else if (id == "TKEY") name = "key";
            if (name && !value.isEmpty())
                tags.insert(QString::fromLatin1(name), value);
        }
        pos += size;
    }
    return tags;
}

static QString v1field(const char* p, int len)
{
    QString s = QString::fromLatin1(p, len);
    // ID3v1 pads with spaces or NULs
    while (!s.isEmpty() && (s.endsWith(QLatin1Char('\0')) || s.endsWith(QLatin1Char(' '))))
        s.chop(1);
    while (s.startsWith(QLatin1Char('\0')))
        s.remove(0, 1);
    return s.trimmed();
}

static TrackMeta fallbackId3v1(const QByteArray& data)
{
    TrackMeta meta;
    if (data.size() < 128)
        return meta;
    const char* tag = data.constData() + data.size() - 128;
    if (qstrncmp(tag, "TAG", 3) != 0)
        return meta;
    meta.title  = v1field(tag + 3, 30);
    meta.artist = v1field(tag + 33, 30);
    meta.album  = v1field(tag + 63, 30);
    int bpm = static_cast<unsigned char>(tag[125]);
    if (bpm > 0)
        meta.bpm = QString::number(bpm);
    return meta;
}

TrackMeta readId3Tags(const QString& filePath)
{
    TrackMeta meta;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return meta;

    QByteArray head = f.read(512 * 1024);
    f.close();

    const QHash<QString, QString> tags = parseId3v2(head);
    meta.title  = tags.value("title");
    meta.artist = tags.value("artist");
    meta.album  = tags.value("album");
    meta.bpm    = tags.value("bpm");
    meta.key    = tags.value("key");

    if (meta.title.isEmpty()) {
        TrackMeta v1 = fallbackId3v1(head);
        if (meta.title.isEmpty())  meta.title  = v1.title;
        if (meta.artist.isEmpty()) meta.artist = v1.artist;
        if (meta.album.isEmpty())  meta.album  = v1.album;
        if (meta.bpm.isEmpty())    meta.bpm    = v1.bpm;
    }

    if (meta.title.isEmpty()) {
        meta.title = QFileInfo(filePath).completeBaseName();
    }
    return meta;
}

} // namespace vdj