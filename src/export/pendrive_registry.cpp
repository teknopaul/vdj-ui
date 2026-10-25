#include "pendrive_registry.h"

#include "config/config_manager.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

namespace vdj {

static const char kSection[] = "virtual_pendrives";

PendriveRegistry::PendriveRegistry(QObject* parent) : QObject(parent)
{
    connect(&watch_, &QTimer::timeout, this, [this]() {
        QStringList now;
        for (const auto& p : scanPhysical()) now << p.mountPoint + (p.hasRekordbox ? "+" : "-");
        if (now != lastPhysical_) {
            lastPhysical_ = now;
            emit physicalChanged();
        }
    });
}

bool PendriveRegistry::isRekordboxFolder(const QString& folder)
{
    return !folder.isEmpty() && QFileInfo(folder + QStringLiteral("/PIONEER/rekordbox/export.pdb")).isFile();
}

QString PendriveRegistry::defaultNameFor(const QString& folder)
{
    const QString n = QFileInfo(QDir::cleanPath(folder)).fileName();
    return n.isEmpty() ? QStringLiteral("VDJ-USB") : n;
}

QList<PhysicalPendrive> PendriveRegistry::scanPhysicalIn(const QString& root)
{
    QList<PhysicalPendrive> out;
    const QDir d(root);
    for (const QFileInfo& fi : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        PhysicalPendrive p;
        p.name = fi.fileName();
        p.mountPoint = fi.absoluteFilePath();
        p.hasRekordbox = isRekordboxFolder(p.mountPoint);
        const QStorageInfo st(p.mountPoint);
        if (st.isValid()) { p.totalBytes = st.bytesTotal(); p.freeBytes = st.bytesFree(); }
        out << p;
    }
    return out;
}

QList<PhysicalPendrive> PendriveRegistry::scanPhysical()
{
    const QString user = QString::fromLocal8Bit(qgetenv("USER"));
    QString name = user.isEmpty() ? QString::fromLocal8Bit(qgetenv("LOGNAME")) : user;
    if (name.isEmpty()) name = QDir::home().dirName();
    QList<PhysicalPendrive> out = scanPhysicalIn(QStringLiteral("/media/") + name);
    out += scanPhysicalIn(QStringLiteral("/run/media/") + name);
    return out;
}

int PendriveRegistry::indexOfName(const QString& name) const
{
    for (int i = 0; i < list_.size(); ++i)
        if (list_[i].name.compare(name, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

int PendriveRegistry::indexOfFolder(const QString& folder) const
{
    const QString f = QDir::cleanPath(folder);
    for (int i = 0; i < list_.size(); ++i)
        if (list_[i].folder == f) return i;
    return -1;
}

QString PendriveRegistry::add(const QString& nameIn, const QString& folderIn)
{
    const QString folder = QDir::cleanPath(folderIn);
    if (!isRekordboxFolder(folder))
        return QStringLiteral("%1 has no PIONEER/rekordbox/export.pdb").arg(folder);
    if (indexOfFolder(folder) >= 0) return QStringLiteral("that folder is already in the list");
    QString name = nameIn.trimmed();
    name.replace('\t', ' ').replace('\n', ' ');
    if (name.isEmpty()) name = defaultNameFor(folder);
    if (indexOfName(name) >= 0) return QStringLiteral("there is already a pendrive called \"%1\"").arg(name);
    list_.append({ name, folder });
    emit changed();
    return QString();
}

QString PendriveRegistry::rename(int index, const QString& nameIn)
{
    if (index < 0 || index >= list_.size()) return QStringLiteral("no such pendrive");
    QString name = nameIn.trimmed();
    name.replace('\t', ' ').replace('\n', ' ');
    if (name.isEmpty()) return QStringLiteral("the name is empty");
    const int other = indexOfName(name);
    if (other >= 0 && other != index) return QStringLiteral("there is already a pendrive called \"%1\"").arg(name);
    list_[index].name = name;
    emit changed();
    return QString();
}

bool PendriveRegistry::remove(int index)
{
    if (index < 0 || index >= list_.size()) return false;
    list_.removeAt(index);
    emit changed();
    return true;
}

QStringList PendriveRegistry::toLines() const
{
    QStringList l;
    for (const auto& p : list_) l << p.name + QLatin1Char('\t') + p.folder;
    return l;
}

void PendriveRegistry::fromLines(const QStringList& lines)
{
    list_.clear();
    for (const QString& line : lines) {
        const int t = line.indexOf('\t');
        if (t <= 0 || t == line.size() - 1) continue;
        const QString name = line.left(t), folder = QDir::cleanPath(line.mid(t + 1));
        if (indexOfName(name) >= 0 || indexOfFolder(folder) >= 0) continue;
        list_.append({ name, folder });                       // not validated here: a stick can be absent today and back tomorrow
    }
    emit changed();
}

void PendriveRegistry::load(ConfigManager* cfg)
{
    QStringList keys = cfg->keysOf(kSection);
    keys.sort();
    QStringList lines;
    for (const QString& k : keys) lines << cfg->loadValue(kSection, k);
    fromLines(lines);
}

void PendriveRegistry::save(ConfigManager* cfg) const
{
    cfg->clearSection(kSection);
    int i = 0;
    for (const QString& line : toLines())
        cfg->saveValue(kSection, QStringLiteral("p%1").arg(i++, 4, 10, QLatin1Char('0')), line);
}

void PendriveRegistry::startWatchingPhysical(int intervalMs)
{
    lastPhysical_.clear();
    for (const auto& p : scanPhysical()) lastPhysical_ << p.mountPoint + (p.hasRekordbox ? "+" : "-");
    watch_.start(intervalMs);
}

void PendriveRegistry::stopWatchingPhysical()
{
    watch_.stop();
}

} // namespace vdj
