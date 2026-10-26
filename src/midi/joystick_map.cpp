#include "joystick_map.h"

namespace vdj {

JoystickAction mapJoystickEvent(bool isAxis, int number, int value)
{
    JoystickAction a;
    if (isAxis) {
        if (value == 0)
            return a;
        switch (number) {
        case 0: a.kind = JoystickAction::TempoAdjust; a.value = value > 0 ? 0.1 : -0.1; break;
        case 1: a.kind = JoystickAction::TempoAdjust; a.value = value < 0 ? 1.0 : -1.0; break;   // stick up = faster
        case 2: a.kind = JoystickAction::Nudge; a.value = value > 0 ? 10 : -10; break;
        case 3: a.kind = JoystickAction::Nudge; a.value = value > 0 ? 1 : -1; break;
        default: break;
        }
        return a;
    }
    if (value != 1)
        return a;
    switch (number) {
    case 8: a.kind = JoystickAction::Toggle; break;               // top-left trigger
    case 9: a.kind = JoystickAction::Stop; break;                 // bottom-left trigger
    case 10:                                                      // top-right trigger
    case 11: a.kind = JoystickAction::QuantizedRestart; break;    // bottom-right trigger
    default: break;
    }
    return a;
}

} // namespace vdj
