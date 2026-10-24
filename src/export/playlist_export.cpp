#include "playlist_export.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

namespace vdj {

static QString safeFileName(QString name)
{
    for (QChar& c : name)
        if (QStringLiteral("/\\:*?\"<>|").contains(c) || c.unicode() < 0x20) c = QLatin1Char('_');
    name = name.trimmed();
    return name.isEmpty() ? QStringLiteral("playlist") : name;
}

static QString uniqueName(const QDir& dir, const QSet<QString>& used, const QString& wanted)
{
    const QFileInfo fi(wanted);
    const QString base = fi.completeBaseName();
    const QString ext = fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix();
    QString name = wanted;
    for (int n = 2; used.contains(name.toLower()) || dir.exists(name); ++n)
        name = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(ext);
    return name;
}

ExportResult exportPlaylist(const QString& dirPath, const QString& playlistName,
                            const QList<ExportTrack>& tracks,
                            const std::function<bool(int, int)>& progress)
{
    ExportResult r;
    const QDir dir(dirPath);
    if (!dir.exists()) {
        r.error = QStringLiteral("%1 does not exist").arg(dirPath);
        return r;
    }
    if (!dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty()) {
        r.error = QStringLiteral("%1 is not empty").arg(dirPath);
        return r;
    }

    QHash<QString, QString> copiedAs;   // source path -> file name in dir
    QSet<QString> used;                 // lower-cased names already taken
    const int total = tracks.size();
    int done = 0;

    for (const ExportTrack& t : tracks) {
        if (progress && !progress(done, total)) {
            r.error = QStringLiteral("Cancelled");
            return r;
        }
        ++done;

        auto known = copiedAs.constFind(t.sourcePath);
        if (known != copiedAs.constEnd()) {       // same track listed twice
            r.files.append(known.value());
            continue;
        }
        if (!QFileInfo::exists(t.sourcePath)) {
            r.missing.append(t.sourcePath);
            continue;
        }
        const QString name = uniqueName(dir, used, QFileInfo(t.sourcePath).fileName());
        if (!QFile::copy(t.sourcePath, dir.filePath(name))) {
            r.failed.append(t.sourcePath);
            continue;
        }
        used.insert(name.toLower());
        copiedAs.insert(t.sourcePath, name);
        r.files.append(name);
        ++r.copied;
    }
    if (progress) progress(total, total);

    const QString listName = uniqueName(dir, used, safeFileName(playlistName) + QStringLiteral(".txt"));
    r.listFile = dir.filePath(listName);
    QFile list(r.listFile);
    if (!list.open(QIODevice::WriteOnly | QIODevice::Text)) {
        r.error = QStringLiteral("Cannot write %1").arg(r.listFile);
        return r;
    }
    for (const QString& f : r.files) list.write(f.toUtf8() + '\n');
    list.close();

    r.ok = true;
    return r;
}

} // namespace vdj
