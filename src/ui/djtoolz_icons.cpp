#include "djtoolz_icons.h"

static void initDjtoolzResources()
{
    Q_INIT_RESOURCE(resources);
}

namespace vdj {

QIcon djtoolzIcon(DjtoolzIcon which)
{
    initDjtoolzResources();
    switch (which) {
    case DjtoolzIcon::Red: return QIcon(QStringLiteral(":/img/button-red.png"));
    case DjtoolzIcon::Yellow: return QIcon(QStringLiteral(":/img/button-yellow.png"));
    case DjtoolzIcon::Grey: return QIcon(QStringLiteral(":/img/button-grey.png"));
    case DjtoolzIcon::Green: return QIcon(QStringLiteral(":/img/button-green.png"));
    case DjtoolzIcon::Pink: return QIcon(QStringLiteral(":/img/button-pink.png"));
    }
    return QIcon();
}

QIcon drumVoiceIcon(int voice, bool on)
{
    static const char* const kNames[] = { "BD", "SD", "SY", "CL", "HH", "OH" };
    if (voice < 0 || voice > 5)
        return QIcon();
    initDjtoolzResources();
    return QIcon(QStringLiteral(":/img/drum-%1-%2.png").arg(QLatin1String(kNames[voice]), on ? QStringLiteral("on") : QStringLiteral("off")));
}

} // namespace vdj
