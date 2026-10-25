#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <QObject>
#include <QString>
#include <QHash>
#include <QStringList>

namespace vdj {

class ConfigManager : public QObject {
    Q_OBJECT

public:
    explicit ConfigManager(QObject* parent = nullptr);

    static QString configDir();
    static bool configDirWritable();

    QString loadValue(const QString& section, const QString& key,
                      const QString& fallback = QString()) const;
    bool saveValue(const QString& section, const QString& key, const QString& value);

    QStringList keysOf(const QString& section) const;
    bool clearSection(const QString& section);          // forget every key of the section
    QString sectionPath(const QString& section) const;

    bool saveJoystickMapping(const QString& device, const QString& action,
                             const QString& control);
    QString loadJoystickMapping(const QString& device, const QString& action) const;
    QHash<QString, QString> joystickMappings(const QString& device) const;

private:
    QString filePath(const QString& section) const;
    QHash<QString, QString> readFile(const QString& section) const;
    bool writeFile(const QString& section, const QHash<QString, QString>& entries) const;
};

} // namespace vdj

#endif // CONFIG_MANAGER_H