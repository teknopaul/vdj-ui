#ifndef DJTOOLZ_ICONS_H
#define DJTOOLZ_ICONS_H

#include <QIcon>

namespace vdj {

// The DJ Tools's round buttons (resources.qrc, converted from img/button-*.svg): red = on, yellow = off, grey = disabled,
// green = play (the one shot / "1 bar" buttons), pink = half volume (a step of the DR-6S).
enum class DjtoolzIcon { Red, Yellow, Grey, Green, Pink };
QIcon djtoolzIcon(DjtoolzIcon which);

// The DR-6S voice buttons BD SD SY CL HH OH (voice 0..5), 24 px: lit = the voice being edited. A null icon for a voice out of range.
QIcon drumVoiceIcon(int voice, bool on);

} // namespace vdj

#endif // DJTOOLZ_ICONS_H
