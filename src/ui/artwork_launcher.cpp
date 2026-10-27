#include "artwork_launcher.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace vdj {

bool parseDesktopEntry(const QString& id, const QByteArray& text, DesktopApp* out)
{
    bool inEntry = false;
    DesktopApp a;
    a.id = id;
    bool application = false, hidden = false;
    for (QByteArray raw : text.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        if (line.startsWith(QLatin1Char('['))) { inEntry = line == QLatin1String("[Desktop Entry]"); continue; }
        if (!inEntry) continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0) continue;
        const QString key = line.left(eq).trimmed(), value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("Type")) application = value == QLatin1String("Application");
        else if (key == QLatin1String("Name") && a.name.isEmpty()) a.name = value;
        else if (key == QLatin1String("Exec")) a.exec = value;
        else if (key == QLatin1String("MimeType")) a.mimes = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        else if ((key == QLatin1String("NoDisplay") || key == QLatin1String("Hidden")) && value == QLatin1String("true")) hidden = true;
    }
    if (!application || hidden || a.exec.isEmpty() || a.name.isEmpty()) return false;
    *out = a;
    return true;
}

QStringList defaultApplicationDirs()
{
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString& extra : {QStringLiteral("/usr/share/applications"), QStringLiteral("/usr/local/share/applications")})
        if (!dirs.contains(extra)) dirs << extra;
    return dirs;
}

QList<DesktopApp> scanDesktopApps(const QStringList& dirs, const QString& mime)
{
    QList<DesktopApp> found;
    QSet<QString> seen;
    for (const QString& d : dirs) {
        const QDir dir(d);
        for (const QString& name : dir.entryList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name)) {
            if (seen.contains(name)) continue;
            seen.insert(name);                                  // an earlier directory overrides a later one, even when it hides the entry
            QFile f(dir.filePath(name));
            if (!f.open(QIODevice::ReadOnly)) continue;
            DesktopApp a;
            if (parseDesktopEntry(name, f.read(1 << 16), &a) && a.mimes.contains(mime)) found.append(a);
        }
    }
    std::sort(found.begin(), found.end(), [](const DesktopApp& x, const DesktopApp& y) { return x.name.toLower() < y.name.toLower(); });
    return found;
}

// Splits a command line the Desktop Entry way: double quotes group, backslash escapes inside them.
static QStringList splitExec(const QString& s)
{
    QStringList out;
    QString cur;
    bool inQuote = false, have = false;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (inQuote) {
            if (c == QLatin1Char('\\') && i + 1 < s.size()) cur += s[++i];
            else if (c == QLatin1Char('"')) inQuote = false;
            else cur += c;
        } else if (c == QLatin1Char('"')) { inQuote = true; have = true; }
        else if (c.isSpace()) { if (have || !cur.isEmpty()) { out << cur; cur.clear(); have = false; } }
        else cur += c;
    }
    if (have || !cur.isEmpty()) out << cur;
    return out;
}

bool buildCommand(const QString& exec, const QString& file, QString* program, QStringList* args)
{
    const QStringList words = splitExec(exec);
    if (words.isEmpty()) return false;
    bool usedFile = false;
    QStringList out;
    for (const QString& w : words) {
        QString r;
        bool dropped = false;
        for (int i = 0; i < w.size(); ++i) {
            if (w[i] != QLatin1Char('%') || i + 1 >= w.size()) { r += w[i]; continue; }
            const QChar code = w[++i];
            if (code == QLatin1Char('%')) r += QLatin1Char('%');
            else if (code == QLatin1Char('f') || code == QLatin1Char('F') || code == QLatin1Char('u') || code == QLatin1Char('U')) { r += file; usedFile = true; }
            else dropped = true;                                // %i %c %k and the rest vanish
        }
        if (dropped && r.isEmpty()) continue;
        out << r;
    }
    if (out.isEmpty()) return false;
    if (!usedFile) out << file;
    *program = out.takeFirst();
    *args = out;
    return !program->isEmpty();
}

bool launchEditor(const QString& exec, const QString& file, QString* err)
{
    QString program;
    QStringList args;
    if (!buildCommand(exec, file, &program, &args)) {
        if (err) *err = QStringLiteral("empty command");
        return false;
    }
    if (!QProcess::startDetached(program, args)) {
        if (err) *err = QStringLiteral("could not start %1 (is it installed? try: sudo apt install gimp)").arg(program);
        return false;
    }
    return true;
}

} // namespace vdj
