#include "usb_speed.h"

#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <algorithm>
#include <cstdlib>

namespace trakcrate {

std::string usbLabel(double mbit)
{
    if (mbit <= 0) return std::string();
    if (mbit <= 12) return "USB 1.1";
    if (mbit <= 480) return "USB 2.0";
    if (mbit <= 5000) return "USB 3.0";
    return "USB 3.2";
}

bool bosHasSuperSpeed(const std::string& bos)
{
    if (bos.size() < 5 || uint8_t(bos[0]) != 5 || uint8_t(bos[1]) != 0x0f) return false;
    const size_t total = std::min<size_t>(bos.size(), size_t(uint8_t(bos[2])) | size_t(uint8_t(bos[3])) << 8);
    for (size_t at = 5; at + 3 <= total;) {
        const size_t len = uint8_t(bos[at]);
        if (len < 3 || at + len > total) break;
        if (uint8_t(bos[at + 1]) == 0x10 && uint8_t(bos[at + 2]) == 0x03) return true;      // device capability: SuperSpeed USB
        at += len;
    }
    return false;
}

static std::string firstLine(const fs::path& p)
{
    std::string s;
    if (!readFile(p, &s)) return std::string();
    size_t nl = s.find('\n');
    return nl == std::string::npos ? s : s.substr(0, nl);
}

UsbLink usbLinkOfDevice(const fs::path& p)
{
    UsbLink l;
    std::error_code ec;
    if (!fs::exists(p / "speed", ec) || !fs::exists(p / "idVendor", ec)) return l;
    const std::string speed = firstLine(p / "speed");
    char* end = nullptr;
    const double mbit = std::strtod(speed.c_str(), &end);
    if (end == speed.c_str() || mbit <= 0) return l;
    l.found = true;
    l.mbit = mbit;
    l.label = usbLabel(mbit);
    l.product = firstLine(p / "product");
    l.isHub = firstLine(p / "bDeviceClass") == "09";
    std::string bos;
    if (readFile(p / "bos_descriptors", &bos)) l.superSpeedCapable = bosHasSuperSpeed(bos);
    // the port the device hangs on has a "peer": the other half (USB 2 / SuperSpeed) of the same physical socket
    const fs::path peer = fs::weakly_canonical(p / "port" / "peer", ec);
    if (!ec) l.peerState = firstLine(peer / "state");
    return l;
}

UsbLink usbLinkFromSysfs(const fs::path& sysPath)
{
    std::error_code ec;
    fs::path p = fs::weakly_canonical(sysPath, ec);
    if (ec) return UsbLink();
    for (int depth = 0; depth < 24 && !p.empty() && p != p.root_path(); ++depth, p = p.parent_path()) {
        if (!fs::exists(p / "speed", ec) || !fs::exists(p / "idVendor", ec)) continue;
        return usbLinkOfDevice(p);
    }
    return UsbLink();
}

UsbLink usbLinkOfPath(const fs::path& path)
{
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return UsbLink();
    // /sys/dev/block/<major>:<minor> is a link to the partition (or disk); its parents lead up to the USB device
    const std::string node = "/sys/dev/block/" + std::to_string(major(st.st_dev)) + ":" + std::to_string(minor(st.st_dev));
    return usbLinkFromSysfs(node);
}

} // namespace trakcrate
