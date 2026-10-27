#ifndef TRAKCRATE_USB_SPEED_H
#define TRAKCRATE_USB_SPEED_H

#include "fsutil.h"

#include <string>

namespace trakcrate {

// The USB link a drive is connected through, from sysfs (no root, no writes). Used to warn before a transfer that a "blue" socket
// or a downgraded stick is running at USB 2.0 speed.
struct UsbLink {
    bool found = false;          // false: not a USB device (an internal disk, a network share) or not readable
    double mbit = 0;             // negotiated speed in Mbit/s: 1.5, 12, 480, 5000, 10000, 20000
    std::string label;           // "USB 1.1", "USB 2.0", "USB 3.0" or "USB 3.2" (see usbLabel)
    std::string product;         // the device's product string when it has one
    bool isHub = false;          // device class 09: a USB 3 hub also shows its USB 2 half at 480 Mbit/s, which is not a downgrade
    bool superSpeedCapable = false;   // the device's BOS descriptor lists a SuperSpeed capability (it is a USB 3 device)
    std::string peerState;       // state of the SuperSpeed half of the same hub port: "not attached", "configured", ... ("" if unknown)
    // A USB 3 device on a USB 2.0 link: a socket, cable or contact problem, not a slow drive.
    bool downgraded() const { return found && !isHub && superSpeedCapable && mbit <= 480; }
};

// 12 or less: USB 1.1, up to 480: USB 2.0, up to 5000: USB 3.0, above: USB 3.2 (the 10 and 20 Gbit/s links). Empty for 0.
std::string usbLabel(double mbit);
// True when a BOS descriptor (the bytes of sysfs "bos_descriptors") contains the SuperSpeed USB Device Capability (type 0x03).
bool bosHasSuperSpeed(const std::string& bos);
// Fills a UsbLink from a USB device directory in sysfs (the one holding "speed" and "idVendor"); found is false when it is not one.
UsbLink usbLinkOfDevice(const fs::path& usbDeviceDir);

// sysPath: a resolved sysfs path of a device or partition; the nearest ancestor holding "speed" and "idVendor" is the USB device.
UsbLink usbLinkFromSysfs(const fs::path& sysPath);
// The link of the drive holding path (a file or folder on a mounted filesystem).
UsbLink usbLinkOfPath(const fs::path& path);

} // namespace trakcrate

#endif
