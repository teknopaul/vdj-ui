#include "sampler_icons.h"

static void initSamplerResources()
{
    Q_INIT_RESOURCE(resources);
}

namespace vdj {

QIcon samplerIcon(SamplerIcon which)
{
    initSamplerResources();
    switch (which) {
    case SamplerIcon::Red: return QIcon(QStringLiteral(":/img/button-red.png"));
    case SamplerIcon::Yellow: return QIcon(QStringLiteral(":/img/button-yellow.png"));
    case SamplerIcon::Grey: return QIcon(QStringLiteral(":/img/button-grey.png"));
    case SamplerIcon::Green: return QIcon(QStringLiteral(":/img/button-green.png"));
    case SamplerIcon::Pink: return QIcon(QStringLiteral(":/img/button-pink.png"));
    }
    return QIcon();
}

QIcon drumVoiceIcon(int voice, bool on)
{
    static const char* const kNames[] = { "BD", "SD", "SY", "CL", "HH", "OH" };
    if (voice < 0 || voice > 5)
        return QIcon();
    initSamplerResources();
    return QIcon(QStringLiteral(":/img/drum-%1-%2.png").arg(QLatin1String(kNames[voice]), on ? QStringLiteral("on") : QStringLiteral("off")));
}

} // namespace vdj
