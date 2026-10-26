#include "djtoolz_presets.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

namespace vdj {

DjtoolzState djtoolzStateFrom(const std::vector<std::shared_ptr<DjtoolzTool>>& tools, const QString& folder, double masterGain)
{
    DjtoolzState st;
    st.folder = folder;
    st.masterGain = masterGain;
    for (const auto& m : tools)
        st.tools.append(m->toVariant());
    return st;
}

std::vector<std::shared_ptr<DjtoolzTool>> djtoolzToolsFrom(const DjtoolzState& state, QStringList* warnings)
{
    std::vector<std::shared_ptr<DjtoolzTool>> out;
    int index = 0;
    for (const QVariant& v : state.tools) {
        ++index;
        if (v.typeId() != QMetaType::QVariantMap) {
            if (warnings) *warnings << QStringLiteral("tool %1 is not an object, skipped").arg(index);
            continue;
        }
        auto m = DjtoolzTool::fromVariant(v.toMap());
        if (!m) {
            if (warnings) *warnings << QStringLiteral("tool %1 has an unknown type '%2', skipped").arg(index).arg(v.toMap().value("type").toString());
            continue;
        }
        out.push_back(m);
    }
    return out;
}

DjtoolzPresets::DjtoolzPresets(const QString& baseDir)
    : base_(baseDir)
    , dir_(baseDir + QStringLiteral("/djtoolz-presets"))
{
}

QString DjtoolzPresets::sanitise(const QString& name)
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

QString DjtoolzPresets::presetPath(const QString& name) const
{
    return dir_ + QLatin1Char('/') + sanitise(name) + QStringLiteral(".json");
}

QString DjtoolzPresets::currentPath() const
{
    return base_ + QStringLiteral("/djtoolz-current.json");
}

QStringList DjtoolzPresets::list() const
{
    QStringList names;
    for (const QString& f : QDir(dir_).entryList(QStringList{ QStringLiteral("*.json") }, QDir::Files, QDir::NoSort))
        names << f.left(f.size() - 5);
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool DjtoolzPresets::exists(const QString& name) const
{
    return !sanitise(name).isEmpty() && QFile::exists(presetPath(name));
}

bool DjtoolzPresets::writeFile(const QString& path, const DjtoolzState& state)
{
    QJsonObject o;
    o["version"] = kVersion;
    o["folder"] = state.folder;
    o["masterGain"] = state.masterGain;
    o["columns"] = state.columns;
    o["controlMode"] = state.controlMode;
    o["tools"] = QJsonArray::fromVariantList(state.tools);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);                               // written to a temporary file and renamed: a crash keeps the old one
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool DjtoolzPresets::readFile(const QString& path, DjtoolzState* state, QString* error)
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
    DjtoolzState st;
    st.folder = o.value("folder").toString();
    st.masterGain = o.contains("masterGain") ? o.value("masterGain").toDouble(1.0) : 1.0;
    st.columns = o.value("columns").toInt(2) == 4 ? 4 : 2;
    st.controlMode = o.value("controlMode").toInt(0) == 1 ? 1 : 0;
    // files from before the rename call the tools "modules"
    st.tools = o.value(o.contains("tools") ? "tools" : "modules").toArray().toVariantList();
    if (state)
        *state = st;
    return true;
}

bool DjtoolzPresets::save(const QString& name, const DjtoolzState& state)
{
    if (sanitise(name).isEmpty())
        return false;
    return writeFile(presetPath(name), state);
}

bool DjtoolzPresets::load(const QString& name, DjtoolzState* state, QString* error) const
{
    if (sanitise(name).isEmpty()) {
        if (error) *error = QStringLiteral("empty preset name");
        return false;
    }
    return readFile(presetPath(name), state, error);
}

bool DjtoolzPresets::remove(const QString& name)
{
    return !sanitise(name).isEmpty() && QFile::remove(presetPath(name));
}

bool DjtoolzPresets::rename(const QString& from, const QString& to)
{
    if (sanitise(from).isEmpty() || sanitise(to).isEmpty() || exists(to) || !exists(from))
        return false;
    return QFile::rename(presetPath(from), presetPath(to));
}

bool DjtoolzPresets::saveCurrent(const DjtoolzState& state)
{
    return writeFile(currentPath(), state);
}

bool DjtoolzPresets::loadCurrent(DjtoolzState* state, QString* error) const
{
    return readFile(currentPath(), state, error);
}

} // namespace vdj
