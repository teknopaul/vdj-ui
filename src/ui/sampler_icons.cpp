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
    }
    return QIcon();
}

} // namespace vdj
