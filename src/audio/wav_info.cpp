#include "wav_info.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <cstring>

namespace vdj {

static quint32 le32(const char* p)
{
    return quint32(quint8(p[0])) | quint32(quint8(p[1])) << 8 | quint32(quint8(p[2])) << 16 | quint32(quint8(p[3])) << 24;
}

double bpmFromFileName(const QString& fileName)
{
    static const QRegularExpression re(QStringLiteral("(\\d{2,3}(?:\\.\\d+)?)\\s*bpm"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(fileName);
    if (!m.hasMatch())
        return 0.0;
    const double v = m.captured(1).toDouble();
    return (v >= 40.0 && v <= 300.0) ? v : 0.0;
}

double wavBpmFromFile(const QString& path)
{
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        char hdr[12];
        if (f.read(hdr, 12) == 12 && !std::memcmp(hdr, "RIFF", 4) && !std::memcmp(hdr + 8, "WAVE", 4)) {
            char ch[8];
            while (f.read(ch, 8) == 8) {
                const quint32 size = le32(ch + 4);
                if (!std::memcmp(ch, "acid", 4)) {
                    if (size >= 24) {
                        char body[24];
                        if (f.read(body, 24) == 24) {
                            float tempo;
                            const quint32 bits = le32(body + 20);
                            std::memcpy(&tempo, &bits, 4);
                            if (tempo >= 40.0f && tempo <= 300.0f)
                                return double(tempo);
                        }
                    }
                    break;
                }
                if (!f.seek(f.pos() + qint64(size) + (size & 1)))
                    break;
            }
        }
    }
    return bpmFromFileName(QFileInfo(path).fileName());
}

QStringList wavsInFolder(const QString& folder)
{
    const QDir dir(folder);
    QStringList names = dir.entryList(QStringList{ QStringLiteral("*.wav"), QStringLiteral("*.WAV"), QStringLiteral("*.Wav") },
                                      QDir::Files | QDir::Readable, QDir::NoSort);
    names.removeDuplicates();
    names.sort(Qt::CaseSensitive);
    QStringList out;
    for (const QString& n : names)
        out << dir.filePath(n);
    return out;
}

QStringList siblingWavs(const QString& samplePath)
{
    return wavsInFolder(QFileInfo(samplePath).absolutePath());
}

QString stepSample(const QString& samplePath, int dir)
{
    const QStringList all = siblingWavs(samplePath);
    if (all.isEmpty())
        return QString();
    const QString self = QFileInfo(samplePath).absoluteFilePath();
    int i = all.indexOf(self);
    if (i >= 0) {
        if (dir > 0) i = qMin(i + 1, all.size() - 1);
        else if (dir < 0) i = qMax(i - 1, 0);
        return all.at(i);
    }
    // gone from the folder: the first file after (before) where it would sort
    int pos = 0;
    while (pos < all.size() && all.at(pos).compare(self, Qt::CaseSensitive) < 0)
        ++pos;
    if (dir > 0) return all.at(qMin(pos, all.size() - 1));
    return all.at(qMax(pos - 1, 0));
}

} // namespace vdj
