#ifndef JOYSTICK_MAP_H
#define JOYSTICK_MAP_H

namespace vdj {

// What a joystick event does to the VDJ sequencer. The default map mirrors adj_js.c (Logitech gamepad). Pure: no device needed.
struct JoystickAction {
    enum Kind { None, Toggle, Stop, QuantizedRestart, Nudge, TempoAdjust };
    Kind kind = None;
    double value = 0.0;          // Nudge: ms, TempoAdjust: BPM delta
};

// isAxis: a movement of axis `number` to `value` (0 = back in the middle, ignored); otherwise button `number` with value 1 pressed, 0 released (ignored).
JoystickAction mapJoystickEvent(bool isAxis, int number, int value);

} // namespace vdj

#endif // JOYSTICK_MAP_H
