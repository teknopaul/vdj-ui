#include "djtoolz_ctl_map.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>

namespace vdj {

static const char* const kControlNames[] = { "mute", "solo", "rec", "select", "knob", "fader" };

static bool isAnalog(Control c) { return c == Control::Knob || c == Control::Fader; }

QString DjtoolzCtlMap::opName(ControlAction::Kind kind, Control control, int strip)
{
    switch (kind) {
        case ControlAction::TrackBack:    return QStringLiteral("track_back");
        case ControlAction::TrackForward: return QStringLiteral("track_fwd");
        case ControlAction::ModeToggle:   return QStringLiteral("mode_toggle");
        case ControlAction::Strip:
            return QStringLiteral("strip%1_%2").arg(strip).arg(QLatin1String(kControlNames[int(control)]));
        default: return QString();
    }
}

DjtoolzCtlMap DjtoolzCtlMap::fromAdjmm(const AdjmmMap& map)
{
    DjtoolzCtlMap m;
    m.directives_ = map.directives;
    static const QRegularExpression strip(QStringLiteral("^strip([1-8])_(\\w+)$"));
    for (const auto& kv : map.named) {
        Entry e{ ControlAction::None, Control::Mute, 0, kv.second };
        if (kv.first == QStringLiteral("track_back"))        e.kind = ControlAction::TrackBack;
        else if (kv.first == QStringLiteral("track_fwd"))    e.kind = ControlAction::TrackForward;
        else if (kv.first == QStringLiteral("mode_toggle"))  e.kind = ControlAction::ModeToggle;
        else {
            const auto mt = strip.match(kv.first);
            if (!mt.hasMatch()) continue;
            int c = -1;
            for (int i = 0; i < 6; ++i)
                if (mt.captured(2) == QLatin1String(kControlNames[i])) c = i;
            if (c < 0) continue;
            e.kind = ControlAction::Strip;
            e.strip = mt.captured(1).toInt();
            e.control = Control(c);
        }
        m.entries_.push_back(e);
    }
    return m;
}

bool DjtoolzCtlMap::flag(const QString& key, bool def) const
{
    if (!directives_.contains(key)) return def;
    const QString v = directives_.value(key).toLower();
    if (v.isEmpty()) return def;
    return !(v == QLatin1String("0") || v == QLatin1String("false") || v == QLatin1String("no") || v == QLatin1String("off"));
}

ControlAction DjtoolzCtlMap::decodeControl(int channel, int controller, int value) const
{
    for (const Entry& e : entries_) {
        const AdjmmOp& op = e.op;
        if (op.controller > 127 || op.controller != uint8_t(controller)) continue;
        if (op.channel != ADJMM_ANY_CHANNEL && op.channel != uint8_t(channel)) continue;
        const bool analog = e.kind == ControlAction::Strip && isAnalog(e.control);
        if (!analog) {
            if (op.value == ADJMM_ANY_VALUE) { if (value < 64) continue; }   // user error: a press is the upper half
            else if (op.value != uint8_t(value)) continue;
        }
        ControlAction a;
        a.kind = e.kind; a.control = e.control; a.strip = e.strip; a.value = value;
        return a;
    }
    return ControlAction();
}

ControlAction DjtoolzCtlMap::decodeNote(int channel, int note, int velocity, bool on) const
{
    if (!on || velocity <= 0) return ControlAction();
    for (const Entry& e : entries_) {
        const AdjmmOp& op = e.op;
        if (op.controller != ADJMM_NOTEON) continue;
        if (e.kind == ControlAction::Strip && isAnalog(e.control)) continue;
        if (op.channel != ADJMM_ANY_CHANNEL && op.channel != uint8_t(channel)) continue;
        if (op.value > 127 || op.value != uint8_t(note)) continue;
        ControlAction a;
        a.kind = e.kind; a.control = e.control; a.strip = e.strip; a.value = velocity;
        return a;
    }
    return ControlAction();
}

QString formatAdjmmOp(const QString& name, const AdjmmOp& op)
{
    const QString ch = QString::number(op.channel == ADJMM_ANY_CHANNEL ? 0 : op.channel);
    QString ctrl;
    if (op.controller == ADJMM_NOTEON)       ctrl = QStringLiteral("N");
    else if (op.controller == ADJMM_NOTEOFF) ctrl = QStringLiteral("O");
    else                                     ctrl = QString::number(op.controller);
    QString val;
    switch (op.value) {
        case ADJMM_ANY_VALUE:         val = QStringLiteral("*"); break;
        case ADJMM_WHEEL_PLUS_PLUS:   val = QStringLiteral("++"); break;
        case ADJMM_WHEEL_MINUS_MINUS: val = QStringLiteral("--"); break;
        case ADJMM_WHEEL_PLUS:        val = QStringLiteral("+"); break;
        case ADJMM_WHEEL_MINUS:       val = QStringLiteral("-"); break;
        default:                      val = QString::number(op.value);
    }
    return QStringLiteral("%1 %2 %3 %4").arg(name, -14).arg(ch, 3).arg(ctrl, 4).arg(val, 4);
}

bool writeDjtoolzOps(const QString& path, const std::map<QString, AdjmmOp>& ops,
                     const QHash<QString, QString>& directives, QString* error)
{
    QByteArray text;
    QFile in(path);
    if (in.exists()) {
        if (!in.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("cannot read ") + path;
            return false;
        }
        text = in.readAll();
        in.close();
    } else {
        text = "# adj midi map\n#\n# name           ch  ctrl  val   (ch 0 = any, N = note on, * = any value)\n";
    }

    std::map<QString, bool> opDone;
    QHash<QString, bool> dirDone;
    const bool endsNl = text.isEmpty() || text.endsWith('\n');
    QList<QByteArray> lines = text.split('\n');
    if (endsNl && !lines.isEmpty()) lines.removeLast();      // split leaves an empty tail after the final newline

    QByteArray out;
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    for (const QByteArray& raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        bool replaced = false, drop = false;
        if (line.startsWith(QStringLiteral("#!"))) {
            const QString key = line.mid(2).trimmed().split(ws, Qt::SkipEmptyParts).value(0).toLower();
            if (directives.contains(key)) {
                if (dirDone.value(key)) drop = true;
                else { out += QStringLiteral("#! %1 %2").arg(key, directives.value(key)).toUtf8() + '\n'; dirDone[key] = true; }
                replaced = true;
            }
        } else if (!line.startsWith('#')) {
            const QString name = line.split(ws, Qt::SkipEmptyParts).value(0);
            const auto it = ops.find(name);
            if (it != ops.end()) {
                if (opDone[name] || it->second.controller == ADJMM_UNSET) drop = true;        // an unset op removes the line
                else { out += formatAdjmmOp(name, it->second).toUtf8() + '\n'; opDone[name] = true; }
                replaced = true;
            }
        }
        if (!replaced && !drop) out += raw + '\n';
    }
    for (auto it = directives.constBegin(); it != directives.constEnd(); ++it)
        if (!dirDone.value(it.key()))
            out += QStringLiteral("#! %1 %2").arg(it.key(), it.value()).toUtf8() + '\n';
    for (const auto& kv : ops)
        if (!opDone[kv.first] && kv.second.controller != ADJMM_UNSET)
            out += formatAdjmmOp(kv.first, kv.second).toUtf8() + '\n';

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(out) != out.size() || !f.commit()) {
        if (error) *error = QStringLiteral("cannot write ") + path;
        return false;
    }
    return true;
}

} // namespace vdj
