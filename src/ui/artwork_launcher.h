#ifndef ARTWORK_LAUNCHER_H
#define ARTWORK_LAUNCHER_H

#include <QList>
#include <QString>
#include <QStringList>

namespace vdj {

// Finding and starting an image editor (doc/trakcrate/track-editor.md, "Open in a native graphical editor"). No UI. Applications come from the
// freedesktop .desktop files; the command is split into a program and arguments, never run through a shell.
struct DesktopApp {
    QString id;            // the .desktop file name, "gimp.desktop"
    QString name;
    QString exec;          // the Exec= line as written
    QStringList mimes;
};

// One file in .desktop format. False for hidden entries, non applications, entries without Exec or Name.
bool parseDesktopEntry(const QString& id, const QByteArray& text, DesktopApp* out);

// Applications in `dirs` that handle `mime` (image/jpeg), sorted by name, each id once (the first directory wins).
QList<DesktopApp> scanDesktopApps(const QStringList& dirs, const QString& mime);
QStringList defaultApplicationDirs();                  // /usr/share/applications, ~/.local/share/applications, ...

// program + arguments for an Exec= line (or a custom command with %f) and a file. Field codes %f %F %u %U become the file, %% a percent sign, other
// codes (%i %c %k ...) vanish. A line without a file code gets the file appended. Quotes follow the Desktop Entry rules. False for an empty command.
bool buildCommand(const QString& exec, const QString& file, QString* program, QStringList* args);

// Starts it detached. False (with err) if the program cannot be started.
bool launchEditor(const QString& exec, const QString& file, QString* err = nullptr);

} // namespace vdj

#endif
