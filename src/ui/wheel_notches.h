#ifndef WHEEL_NOTCHES_H
#define WHEEL_NOTCHES_H

#include <QWheelEvent>

namespace vdj {

// Mouse wheel events turned into whole notches (120 units of angleDelta). Many mice and touchpads send small deltas (15, 40 ...)
// per event, which add up to a notch: dividing every single event by 120 would give 0 and the wheel would do nothing.
// The remainder is kept for the next event and dropped when the wheel turns the other way. A sideways wheel counts too.
class WheelNotches {
public:
    int add(const QWheelEvent* event)
    {
        const QPoint d = event->angleDelta();
        const int delta = d.y() != 0 ? d.y() : d.x();
        if (delta == 0)
            return 0;
        if ((delta > 0) != (rest_ > 0) && rest_ != 0)
            rest_ = 0;
        rest_ += delta;
        const int notches = rest_ / 120;
        rest_ -= notches * 120;
        return notches;
    }

private:
    int rest_ = 0;
};

} // namespace vdj

#endif // WHEEL_NOTCHES_H
