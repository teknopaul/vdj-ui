// vdj-usb-watch: prints a line when a USB device is plugged in or removed, for trying out sockets, cables and drives.
//
//   CONNECT <device name> <vid:pid> USB N.N
//   DISCONNECT <device name>
//
// Reads /sys/bus/usb/devices only (no root, no libusb, no udev) every 200 ms. Hubs are left out unless --hubs is given.
// A device name is "<manufacturer> <product>" (or just the product); it may contain spaces, so parse the line from the end.
//
//   --initial   also print CONNECT for the devices that are already plugged in
//   --hubs      include hubs
//   --verbose   append key=value details: bus=<port path> mbit=<speed> bcd=<bcdUSB> ss=yes|no peer=<state of the SuperSpeed half of the port>
//               and DOWNGRADED when the device supports USB 3 but is connected as USB 2.0 or slower
//   --once      print the current devices (like --initial) and exit
#include "trakcrate/fsutil.h"
#include "trakcrate/usb_speed.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>

using namespace trakcrate;

namespace {

std::string line1(const fs::path& p)
{
    std::string s;
    if (!readFile(p, &s)) return std::string();
    s = s.substr(0, s.find('\n'));
    s.erase(0, s.find_first_not_of(' '));
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
    return s;
}

struct Dev {
    std::string name, id, label, extra;
};

std::map<std::string, Dev> scan(bool hubs, bool verbose)
{
    std::map<std::string, Dev> now;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator("/sys/bus/usb/devices", ec)) {
        const fs::path dir = fs::canonical(e.path(), ec);
        if (ec || !fs::exists(dir / "idVendor", ec)) continue;                 // interfaces and unfinished devices have none
        const std::string busid = e.path().filename().string();
        if (busid.compare(0, 3, "usb") == 0) continue;                         // root hubs
        if (!hubs && line1(dir / "bDeviceClass") == "09") continue;
        const UsbLink l = usbLinkOfDevice(dir);
        if (!l.found) continue;
        Dev d;
        const std::string man = line1(dir / "manufacturer"), prod = line1(dir / "product");
        d.name = man.empty() ? prod : (prod.empty() ? man : man + " " + prod);
        if (d.name.empty()) d.name = "usb " + busid;
        d.id = line1(dir / "idVendor") + ":" + line1(dir / "idProduct");
        d.label = l.label;
        if (verbose) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%g", l.mbit);
            d.extra = " bus=" + busid + " mbit=" + buf + " bcd=" + line1(dir / "version") + " ss=" + (l.superSpeedCapable ? "yes" : "no") +
                      " peer=" + (l.peerState.empty() ? "?" : l.peerState) + (l.downgraded() ? " DOWNGRADED" : "");
        }
        now[busid] = d;
    }
    return now;
}

} // namespace

int main(int argc, char** argv)
{
    bool initial = false, hubs = false, verbose = false, once = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--initial")) initial = true;
        else if (!std::strcmp(argv[i], "--hubs")) hubs = true;
        else if (!std::strcmp(argv[i], "--verbose")) verbose = true;
        else if (!std::strcmp(argv[i], "--once")) once = initial = true;
        else {
            std::fprintf(stderr, "usage: %s [--initial] [--hubs] [--verbose] [--once]\n", argv[0]);
            return std::strcmp(argv[i], "--help") ? 2 : 0;
        }
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    std::map<std::string, Dev> known = scan(hubs, verbose);
    auto connect = [](const std::string&, const Dev& d) {
        std::printf("CONNECT %s %s %s%s\n", d.name.c_str(), d.id.c_str(), d.label.c_str(), d.extra.c_str());
    };
    if (initial)
        for (const auto& kv : known) connect(kv.first, kv.second);
    if (once) return 0;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const std::map<std::string, Dev> now = scan(hubs, verbose);
        for (const auto& kv : known)
            if (!now.count(kv.first)) std::printf("DISCONNECT %s\n", kv.second.name.c_str());
        for (const auto& kv : now)
            if (!known.count(kv.first)) connect(kv.first, kv.second);
        known = now;
    }
}
