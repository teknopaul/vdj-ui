#include "quantize_settings.h"
#include "config/config_manager.h"

namespace vdj {

QuantizeSettings::QuantizeSettings(ConfigManager* config, QObject* parent)
    : QObject(parent)
    , config_(config)
{
    if (!config_)
        return;
    switch (config_->loadValue(QStringLiteral("dj"), QStringLiteral("quantize")).toInt()) {
    case 4: res_ = Quarter; break;
    case 2: res_ = Half; break;
    default: res_ = Beat; break;
    }
}

void QuantizeSettings::setResolution(Resolution r)
{
    if (r == res_)
        return;
    res_ = r;
    if (config_)
        config_->saveValue(QStringLiteral("dj"), QStringLiteral("quantize"), QString::number(int(r)));
    emit changed(int(r));
}

QString QuantizeSettings::label(Resolution r)
{
    switch (r) {
    case Quarter: return QStringLiteral("1/4 beat");
    case Half: return QStringLiteral("1/2 beat");
    default: return QStringLiteral("1 beat");
    }
}

} // namespace vdj
