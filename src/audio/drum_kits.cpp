#include "drum_kits.h"
#include "djtoolz_presets.h"
#include "swing.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

namespace vdj {

DrumKit drumKitFromSettings(const ToolSettings& s)
{
    DrumKit k;
    for (int v = 0; v < kDrumVoices; ++v)
        k.voices[v] = s.drums[v];
    k.swing = s.swing;
    return k;
}

void applyDrumKit(ToolSettings* s, const DrumKit& kit)
{
    for (int v = 0; v < kDrumVoices; ++v) {
        const bool muted = s->drums[v].muted;        // a kit does not touch the mix
        s->drums[v] = kit.voices[v];
        s->drums[v].muted = muted;
    }
    s->swing = kit.swing;
}

DrumKits::DrumKits(const QString& baseDir)
    : dir_(baseDir + QStringLiteral("/drum-kits"))
{
}

QString DrumKits::sanitise(const QString& name)
{
    return DjtoolzPresets::sanitise(name);
}

QString DrumKits::kitPath(const QString& name) const
{
    return dir_ + QLatin1Char('/') + sanitise(name) + QStringLiteral(".json");
}

QStringList DrumKits::list() const
{
    QStringList names;
    for (const QString& f : QDir(dir_).entryList(QStringList{ QStringLiteral("*.json") }, QDir::Files, QDir::NoSort))
        names << f.left(f.size() - 5);
    for (auto it = imported_.cbegin(); it != imported_.cend(); ++it)
        names << it.key();
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool DrumKits::exists(const QString& name) const
{
    return imported_.contains(name) || (!sanitise(name).isEmpty() && QFile::exists(kitPath(name)));
}

QByteArray DrumKits::kitJson(const DrumKit& kit, const QString& name, const QStringList* relativeSamples)
{
    QJsonArray voices;
    for (int i = 0; i < kDrumVoices; ++i) {
        QVariantMap m = drumVoiceToVariant(kit.voices[i], false);
        if (relativeSamples)
            m["sample"] = relativeSamples->value(i);
        voices.append(QJsonObject::fromVariantMap(m));
    }
    QJsonObject o;
    o["version"] = kVersion;
    if (!name.isEmpty())
        o["name"] = name;
    o["swing"] = kit.swing;
    o["voices"] = voices;
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

bool DrumKits::save(const QString& name, const DrumKit& kit)
{
    if (sanitise(name).isEmpty() || imported_.contains(name))
        return false;
    QDir().mkpath(dir_);
    QSaveFile f(kitPath(name));                    // a temporary file renamed over the old one: a crash keeps the old kit
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(kitJson(kit, QString(), nullptr));
    return f.commit();
}

bool DrumKits::loadJson(const QByteArray& json, const QString& baseDir, DrumKit* kit, QString* error) const
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (doc.isNull() || !doc.isObject()) {
        if (error) *error = QStringLiteral("not a valid drum kit: %1").arg(doc.isNull() ? pe.errorString() : QStringLiteral("not an object"));
        return false;
    }
    const QJsonObject o = doc.object();             // unknown keys are ignored (a newer version may add some)
    DrumKit k;
    k.swing = swingClamp(o.value("swing").toInt(0));
    const QJsonArray voices = o.value("voices").toArray();
    for (int i = 0; i < kDrumVoices && i < voices.size(); ++i) {
        drumVoiceFromVariant(voices[i].toObject().toVariantMap(), &k.voices[i]);
        QString& sp = k.voices[i].samplePath;
        if (!baseDir.isEmpty() && !sp.isEmpty() && QDir::isRelativePath(sp))
            sp = QDir(baseDir).absoluteFilePath(sp);       // an exported kit refers to its own copies
    }
    if (kit)
        *kit = k;
    return true;
}

bool DrumKits::load(const QString& name, DrumKit* kit, QString* error) const
{
    const auto imp = imported_.constFind(name);
    if (imp != imported_.cend())
        return loadKitFolder(imp.value(), kit, nullptr, error);
    if (sanitise(name).isEmpty()) {
        if (error) *error = QStringLiteral("empty kit name");
        return false;
    }
    QFile f(kitPath(name));
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1").arg(kitPath(name));
        return false;
    }
    return loadJson(f.readAll(), QString(), kit, error);
}

bool DrumKits::loadKitFolder(const QString& folder, DrumKit* kit, QString* name, QString* error)
{
    QFile f(QDir(folder).filePath(QLatin1String(kKitFile)));
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1").arg(f.fileName());
        return false;
    }
    const QByteArray json = f.readAll();
    if (name)
        *name = QJsonDocument::fromJson(json).object().value("name").toString();
    return DrumKits(QString()).loadJson(json, folder, kit, error);
}

QStringList DrumKits::kitFoldersIn(const QString& folder)
{
    QStringList out;
    const QDir d(folder);
    if (QFile::exists(d.filePath(QLatin1String(kKitFile))))
        out << d.absolutePath();
    for (const QString& sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (QFile::exists(QDir(d.filePath(sub)).filePath(QLatin1String(kKitFile))))
            out << QDir(d.filePath(sub)).absolutePath();
    return out;
}

void DrumKits::setImportFolders(const QStringList& folders)
{
    importFolders_ = folders;
    imported_.clear();
    for (const QString& folder : folders) {
        for (const QString& kitFolder : kitFoldersIn(folder)) {
            QString name;
            if (!loadKitFolder(kitFolder, nullptr, &name) )
                continue;
            if (name.isEmpty())
                name = QDir(kitFolder).dirName();
            name = sanitise(name);
            if (name.isEmpty())
                continue;
            QString unique = name;
            for (int n = 2; (imported_.contains(unique) || QFile::exists(kitPath(unique))) && n < 1000; ++n)
                unique = QStringLiteral("%1 (%2)").arg(name).arg(n);
            imported_.insert(unique, kitFolder);
        }
    }
}

bool DrumKits::exportKit(const QString& name, const DrumKit& kit, const QString& folder, QString* error)
{
    const auto fail = [error](const QString& m) { if (error) *error = m; return false; };
    QDir dir(folder);
    const bool existed = dir.exists();
    if (existed && !dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        return fail(QStringLiteral("%1 is not empty: choose an empty folder").arg(folder));
    if (!existed && !QDir().mkpath(folder))
        return fail(QStringLiteral("cannot create %1").arg(folder));
    const QString samples = dir.filePath(QStringLiteral("samples"));
    const auto cleanup = [&]() {
        QDir(samples).removeRecursively();
        QFile::remove(dir.filePath(QLatin1String(kKitFile)));
        if (!existed) QDir().rmdir(folder);
    };
    if (!QDir().mkpath(samples))
        return fail(QStringLiteral("cannot create %1").arg(samples));

    QStringList relative;
    QMap<QString, QString> copied;                  // original path -> relative name (a sample used by several voices is copied once)
    QSet<QString> used;
    for (int i = 0; i < kDrumVoices; ++i) {
        const QString src = kit.voices[i].samplePath;
        if (src.isEmpty()) { relative << QString(); continue; }
        if (copied.contains(src)) { relative << copied.value(src); continue; }
        const QFileInfo fi(src);
        if (!fi.isFile()) {
            cleanup();
            return fail(QStringLiteral("voice %1: the sample %2 is missing").arg(i + 1).arg(src));
        }
        QString base = fi.completeBaseName(), ext = fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix();
        QString file = base + ext;
        for (int n = 2; used.contains(file.toLower()); ++n)
            file = QStringLiteral("%1-%2%3").arg(base).arg(n).arg(ext);
        used.insert(file.toLower());
        if (!QFile::copy(src, QDir(samples).filePath(file))) {      // only reads the original
            cleanup();
            return fail(QStringLiteral("cannot copy %1 to %2").arg(src, samples));
        }
        const QString rel = QStringLiteral("samples/") + file;
        copied.insert(src, rel);
        relative << rel;
    }
    QSaveFile f(dir.filePath(QLatin1String(kKitFile)));
    if (!f.open(QIODevice::WriteOnly)) { cleanup(); return fail(QStringLiteral("cannot write %1").arg(f.fileName())); }
    f.write(kitJson(kit, name, &relative));
    if (!f.commit()) { cleanup(); return fail(QStringLiteral("cannot write %1").arg(f.fileName())); }
    return true;
}

bool DrumKits::remove(const QString& name)
{
    return !imported_.contains(name) && !sanitise(name).isEmpty() && QFile::remove(kitPath(name));
}

bool DrumKits::rename(const QString& from, const QString& to)
{
    if (sanitise(from).isEmpty() || sanitise(to).isEmpty() || exists(to) || !exists(from) || imported_.contains(from))
        return false;
    return QFile::rename(kitPath(from), kitPath(to));
}

} // namespace vdj
