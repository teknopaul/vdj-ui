#include "edit_prefs.h"

#include "config/config_manager.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace vdj {

namespace {
const QString kSection = QStringLiteral("edit");
}

QString EditPrefs::get(const char* key) const
{
    return config_ ? config_->loadValue(kSection, QLatin1String(key)) : QString();
}

void EditPrefs::set(const char* key, const QString& v)
{
    if (config_) config_->saveValue(kSection, QLatin1String(key), v);
}

QString EditPrefs::format() const
{
    return get("format") == QLatin1String("mp3") ? QStringLiteral("mp3") : QStringLiteral("wav");
}

void EditPrefs::setFormat(const QString& f) { set("format", f == QLatin1String("mp3") ? QStringLiteral("mp3") : QStringLiteral("wav")); }

int EditPrefs::mp3Kbps() const
{
    const int k = get("mp3_kbps").toInt();
    return (k == 128 || k == 192 || k == 256 || k == 320) ? k : 256;
}

void EditPrefs::setMp3Kbps(int kbps) { set("mp3_kbps", QString::number(kbps)); }

QString EditPrefs::author() const
{
    const QString a = get("author").trimmed();
    if (!a.isEmpty()) return a;
    const QString u = qEnvironmentVariable("USER");
    return u.isEmpty() ? QStringLiteral("user") : u;
}

void EditPrefs::setAuthor(const QString& a) { set("author", a.trimmed()); }

QString EditPrefs::defaultFolder(const QString& importedFrom)
{
    if (!importedFrom.isEmpty()) {
        const QString dir = QFileInfo(importedFrom).absolutePath();
        if (QFileInfo(dir).isDir()) return dir;
    }
    const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (!music.isEmpty() && QFileInfo(music).isDir()) return music;
    return QDir::homePath();
}

QString EditPrefs::folder(const QString& importedFrom) const
{
    const QString f = get("folder");
    return !f.isEmpty() && QFileInfo(f).isDir() ? f : defaultFolder(importedFrom);
}

void EditPrefs::setFolder(const QString& f) { set("folder", f); }

QString EditPrefs::loopFolder(const QString& importedFrom) const
{
    const QString f = get("loop_folder");
    return !f.isEmpty() && QFileInfo(f).isDir() ? f : folder(importedFrom);
}

void EditPrefs::setLoopFolder(const QString& f) { set("loop_folder", f); }

} // namespace vdj
