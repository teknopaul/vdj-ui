#include "rb_mode_options.h"

namespace vdj {

QString unknownPolicyName(db::UnknownPolicy p)
{
    switch (p) {
    case db::UnknownPolicy::Ignore: return QStringLiteral("ignore");
    case db::UnknownPolicy::Close:  return QStringLiteral("close");
    default:                        return QStringLiteral("empty");
    }
}

bool unknownPolicyFromName(const QString& s, db::UnknownPolicy* p)
{
    const QString l = s.toLower();
    if (l == QLatin1String("ignore")) *p = db::UnknownPolicy::Ignore;
    else if (l == QLatin1String("empty")) *p = db::UnknownPolicy::EmptyOk;
    else if (l == QLatin1String("close")) *p = db::UnknownPolicy::Close;
    else return false;
    return true;
}

bool parseRbModeArgs(const QStringList& args, RbModeOptions* out, QString* error)
{
    auto need = [&](int& i, const QString& opt, QString* value) {
        if (i + 1 >= args.size()) {
            if (error) *error = QStringLiteral("%1 needs a value").arg(opt);
            return false;
        }
        *value = args[++i];
        return true;
    };
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args[i];
        QString v;
        if (a == QLatin1String("--rekordbox-mode")) {
            if (!need(i, a, &out->exportRoot)) return false;
        } else if (a == QLatin1String("--rb-iface")) {
            if (!need(i, a, &out->iface)) return false;
        } else if (a == QLatin1String("--rb-name")) {
            if (!need(i, a, &out->computerName)) return false;
        } else if (a == QLatin1String("--rb-log-dir")) {
            if (!need(i, a, &out->logDir)) return false;
        } else if (a == QLatin1String("--rb-device")) {
            if (!need(i, a, &v)) return false;
            bool ok = false;
            const int n = v.toInt(&ok);
            if (!ok || n < 7 || n > 254) {
                if (error) *error = QStringLiteral("--rb-device must be a number from 7 to 254 (got \"%1\")").arg(v);
                return false;
            }
            out->device = n;
        } else if (a == QLatin1String("--rb-unknown-policy")) {
            if (!need(i, a, &v)) return false;
            if (!unknownPolicyFromName(v, &out->unknownPolicy)) {
                if (error) *error = QStringLiteral("--rb-unknown-policy must be ignore, empty or close (got \"%1\")").arg(v);
                return false;
            }
        }
    }
    return true;
}

} // namespace vdj
