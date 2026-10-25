#ifndef SAMPLER_ICONS_H
#define SAMPLER_ICONS_H

#include <QIcon>

namespace vdj {

// The sampler's round buttons (resources.qrc, converted from img/button-*.svg): red = on, yellow = off, grey = disabled,
// green = play (the one shot / "1 bar" buttons).
enum class SamplerIcon { Red, Yellow, Grey, Green };
QIcon samplerIcon(SamplerIcon which);

} // namespace vdj

#endif // SAMPLER_ICONS_H
