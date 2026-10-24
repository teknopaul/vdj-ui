#include "config_manager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCoreApplication>

namespace vdj {

ConfigManager::ConfigManager(QObject* parent)
    : QObject(parent)
{
}

QString ConfigManager::configDir()
{
    QString system = QStringLiteral("/etc/vdj-ui");
    QDir sys(system);
    if (sys.exists() || sys.mkpath(QStringLiteral("."))) {
        // only keep /etc/vdj-ui when we can actually write into it
        QFile probe(system + QStringLiteral("/.probe"));
        if (probe.open(QIODevice::WriteOnly)) {
            probe.remove();
            return system;
        }
    }
    // fall back to a per-user dir when /etc/vdj-ui is not writable
    QString user = QDir::homePath() + QStringLiteral("/.config/vdj-ui");
    QDir(user).mkpath(QStringLiteral("."));
    return user;
}

bool ConfigManager::configDirWritable()
{
    QDir d(configDir());
    return d.exists();
}

QString ConfigManager::filePath(const QString& section) const
{
    return configDir() + QLatin1Char('/') + section + QStringLiteral(".conf");
}

QString ConfigManager::sectionPath(const QString& section) const
{
    return filePath(section);
}

QHash<QString, QString> ConfigManager::readFile(const QString& section) const
{
    QHash<QString, QString> entries;
    QFile f(filePath(section));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return entries;
    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        int eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        entries.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    return entries;
}

bool ConfigManager::writeFile(const QString& section,
                              const QHash<QString, QString>& entries) const
{
    QFileInfo fi(filePath(section));
    if (!QDir().mkpath(fi.absolutePath()))
        return false;
    QFile f(filePath(section));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    // stable output order
    QStringList keys = entries.keys();
    keys.sort();
    QByteArray out;
    out.append(QStringLiteral("# vdj-ui %1 config\n").arg(section).toUtf8());
    for (const QString& k : keys) {
        out.append(QByteArray(k.toUtf8()) + "=" + entries.value(k).toUtf8() + "\n");
    }
    return f.write(out) == out.size();
}

QString ConfigManager::loadValue(const QString& section, const QString& key,
                                 const QString& fallback) const
{
    return readFile(section).value(key, fallback);
}

bool ConfigManager::saveValue(const QString& section, const QString& key, const QString& value)
{
    QHash<QString, QString> entries = readFile(section);
    entries.insert(key, value);
    return writeFile(section, entries);
}

QStringList ConfigManager::keysOf(const QString& section) const
{
    return readFile(section).keys();
}

bool ConfigManager::saveJoystickMapping(const QString& device, const QString& action,
                                        const QString& control)
{
    return saveValue(QStringLiteral("joystick/") + device, action, control);
}

QString ConfigManager::loadJoystickMapping(const QString& device, const QString& action) const
{
    return loadValue(QStringLiteral("joystick/") + device, action);
}

QHash<QString, QString> ConfigManager::joystickMappings(const QString& device) const
{
    return readFile(QStringLiteral("joystick/") + device);
}

} // namespace vdj