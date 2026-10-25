#include "drum_kits.h"
#include "sampler_presets.h"
#include "swing.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace vdj {

DrumKit drumKitFromSettings(const ModuleSettings& s)
{
    DrumKit k;
    for (int v = 0; v < kDrumVoices; ++v)
        k.voices[v] = s.drums[v];
    k.swing = s.swing;
    return k;
}

void applyDrumKit(ModuleSettings* s, const DrumKit& kit)
{
    for (int v = 0; v < kDrumVoices; ++v)
        s->drums[v] = kit.voices[v];
    s->swing = kit.swing;
}

DrumKits::DrumKits(const QString& baseDir)
    : dir_(baseDir + QStringLiteral("/drum-kits"))
{
}

QString DrumKits::sanitise(const QString& name)
{
    return SamplerPresets::sanitise(name);
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
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool DrumKits::exists(const QString& name) const
{
    return !sanitise(name).isEmpty() && QFile::exists(kitPath(name));
}

bool DrumKits::save(const QString& name, const DrumKit& kit)
{
    if (sanitise(name).isEmpty())
        return false;
    QJsonArray voices;
    for (const DrumVoice& v : kit.voices)
        voices.append(QJsonObject::fromVariantMap(drumVoiceToVariant(v)));
    QJsonObject o;
    o["version"] = kVersion;
    o["swing"] = kit.swing;
    o["voices"] = voices;
    QDir().mkpath(dir_);
    QSaveFile f(kitPath(name));                    // a temporary file renamed over the old one: a crash keeps the old kit
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool DrumKits::load(const QString& name, DrumKit* kit, QString* error) const
{
    if (sanitise(name).isEmpty()) {
        if (error) *error = QStringLiteral("empty kit name");
        return false;
    }
    QFile f(kitPath(name));
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1").arg(kitPath(name));
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || !doc.isObject()) {
        if (error) *error = QStringLiteral("not a valid drum kit: %1").arg(doc.isNull() ? pe.errorString() : QStringLiteral("not an object"));
        return false;
    }
    const QJsonObject o = doc.object();             // unknown keys are ignored (a newer version may add some)
    DrumKit k;
    k.swing = swingClamp(o.value("swing").toInt(0));
    const QJsonArray voices = o.value("voices").toArray();
    for (int i = 0; i < kDrumVoices && i < voices.size(); ++i)
        drumVoiceFromVariant(voices[i].toObject().toVariantMap(), &k.voices[i]);
    if (kit)
        *kit = k;
    return true;
}

bool DrumKits::remove(const QString& name)
{
    return !sanitise(name).isEmpty() && QFile::remove(kitPath(name));
}

bool DrumKits::rename(const QString& from, const QString& to)
{
    if (sanitise(from).isEmpty() || sanitise(to).isEmpty() || exists(to) || !exists(from))
        return false;
    return QFile::rename(kitPath(from), kitPath(to));
}

} // namespace vdj
