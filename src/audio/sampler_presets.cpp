#include "sampler_presets.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

namespace vdj {

SamplerState samplerStateFrom(const std::vector<std::shared_ptr<SamplerModule>>& modules, const QString& folder, double masterGain)
{
    SamplerState st;
    st.folder = folder;
    st.masterGain = masterGain;
    for (const auto& m : modules)
        st.modules.append(m->toVariant());
    return st;
}

std::vector<std::shared_ptr<SamplerModule>> samplerModulesFrom(const SamplerState& state, QStringList* warnings)
{
    std::vector<std::shared_ptr<SamplerModule>> out;
    int index = 0;
    for (const QVariant& v : state.modules) {
        ++index;
        if (v.typeId() != QMetaType::QVariantMap) {
            if (warnings) *warnings << QStringLiteral("module %1 is not an object, skipped").arg(index);
            continue;
        }
        auto m = SamplerModule::fromVariant(v.toMap());
        if (!m) {
            if (warnings) *warnings << QStringLiteral("module %1 has an unknown type '%2', skipped").arg(index).arg(v.toMap().value("type").toString());
            continue;
        }
        out.push_back(m);
    }
    return out;
}

SamplerPresets::SamplerPresets(const QString& baseDir)
    : base_(baseDir)
    , dir_(baseDir + QStringLiteral("/sampler-presets"))
{
}

QString SamplerPresets::sanitise(const QString& name)
{
    QString out;
    for (const QChar c : name.trimmed()) {
        const bool ok = (c.isLetterOrNumber() && c.unicode() < 128) || c == QLatin1Char(' ') || c == QLatin1Char('_') || c == QLatin1Char('-') || c == QLatin1Char('.');
        out += ok ? c : QLatin1Char('_');
    }
    while (out.startsWith(QLatin1Char('.')))
        out.remove(0, 1);
    out = out.trimmed().left(64).trimmed();
    return out;
}

QString SamplerPresets::presetPath(const QString& name) const
{
    return dir_ + QLatin1Char('/') + sanitise(name) + QStringLiteral(".json");
}

QString SamplerPresets::currentPath() const
{
    return base_ + QStringLiteral("/sampler-current.json");
}

QStringList SamplerPresets::list() const
{
    QStringList names;
    for (const QString& f : QDir(dir_).entryList(QStringList{ QStringLiteral("*.json") }, QDir::Files, QDir::NoSort))
        names << f.left(f.size() - 5);
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool SamplerPresets::exists(const QString& name) const
{
    return !sanitise(name).isEmpty() && QFile::exists(presetPath(name));
}

bool SamplerPresets::writeFile(const QString& path, const SamplerState& state)
{
    QJsonObject o;
    o["version"] = kVersion;
    o["folder"] = state.folder;
    o["masterGain"] = state.masterGain;
    o["columns"] = state.columns;
    o["controlMode"] = state.controlMode;
    o["modules"] = QJsonArray::fromVariantList(state.modules);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);                               // written to a temporary file and renamed: a crash keeps the old one
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool SamplerPresets::readFile(const QString& path, SamplerState* state, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1").arg(path);
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || !doc.isObject()) {
        if (error) *error = QStringLiteral("not a valid preset: %1").arg(doc.isNull() ? pe.errorString() : QStringLiteral("not an object"));
        return false;
    }
    const QJsonObject o = doc.object();               // unknown keys are ignored (newer versions may add some)
    SamplerState st;
    st.folder = o.value("folder").toString();
    st.masterGain = o.contains("masterGain") ? o.value("masterGain").toDouble(1.0) : 1.0;
    st.columns = o.value("columns").toInt(2) == 4 ? 4 : 2;
    st.controlMode = o.value("controlMode").toInt(0) == 1 ? 1 : 0;
    st.modules = o.value("modules").toArray().toVariantList();
    if (state)
        *state = st;
    return true;
}

bool SamplerPresets::save(const QString& name, const SamplerState& state)
{
    if (sanitise(name).isEmpty())
        return false;
    return writeFile(presetPath(name), state);
}

bool SamplerPresets::load(const QString& name, SamplerState* state, QString* error) const
{
    if (sanitise(name).isEmpty()) {
        if (error) *error = QStringLiteral("empty preset name");
        return false;
    }
    return readFile(presetPath(name), state, error);
}

bool SamplerPresets::remove(const QString& name)
{
    return !sanitise(name).isEmpty() && QFile::remove(presetPath(name));
}

bool SamplerPresets::rename(const QString& from, const QString& to)
{
    if (sanitise(from).isEmpty() || sanitise(to).isEmpty() || exists(to) || !exists(from))
        return false;
    return QFile::rename(presetPath(from), presetPath(to));
}

bool SamplerPresets::saveCurrent(const SamplerState& state)
{
    return writeFile(currentPath(), state);
}

bool SamplerPresets::loadCurrent(SamplerState* state, QString* error) const
{
    return readFile(currentPath(), state, error);
}

} // namespace vdj
