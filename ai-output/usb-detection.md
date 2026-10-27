# USB link detection: mini-plan

Goal: tell the user, before a transfer, that a drive is connected through a poor socket, cable or contact, with only `USB N.N` and a plain
warning. No speed figures and no speed tests (too many other factors). Find out by experiment what can be detected reliably from software
without root, using different devices in different sockets.

## What is already known (the KINGSTON DataTraveler 3.0 on the laptop dongle)

| Source (no root)                   | Value                                          | Meaning                                               |
|------------------------------------|------------------------------------------------|-------------------------------------------------------|
| `/sys/bus/usb/devices/<dev>/speed` | `480`                                          | negotiated link: USB 2.0                              |
| `.../bos_descriptors`              | contains capability type `0x03`                | the device is SuperSpeed (USB 3) capable              |
| `.../<dev>/port/peer/state`        | `not attached`                                 | the SuperSpeed half of the same socket sees no device |
| combination                        | capable, but 480 Mbit/s, SuperSpeed half empty | **downgraded**: USB 3 device on a USB 2.0 link        |

Not decidable from software: whether the socket is wired USB 2 only, the cable is bad, or the drive's SuperSpeed contacts are worn. All three look the same.
Hub devices show their USB 2 half at 480 Mbit/s and also list SuperSpeed capability: that is not a downgrade (`isHub` excludes it).

## The tool

`vdj-usb-watch` (built with the project, `build/src/vdj-usb-watch`, source `src/tools/usb_watch.cpp`). No root, polls sysfs every 200 ms.

```
CONNECT <device name> <vid:pid> USB N.N
DISCONNECT <device name>
```

Options: `--initial` (also list devices already plugged in), `--once` (list and exit), `--hubs`, `--verbose`
(appends `bus=<port path> mbit=<n> bcd=<n> ss=yes|no peer=<state>` and `DOWNGRADED` when a USB 3 device runs at USB 2.0 or slower).
The name may contain spaces: parse from the end. Labels: `USB 1.1`, `USB 2.0`, `USB 3.0`, `USB 3.2` (10 and 20 Gbit/s).

Run it in a terminal and plug things in:

```
build/src/vdj-usb-watch --verbose
```

## Experiment

For every device in every socket, note the CONNECT line (with `--verbose`) and whether it is what you expected.

Devices: the KINGSTON stick; a second USB 3 stick; a USB 2 only stick (if any); a USB 3 SSD or enclosure; a card reader; a keyboard or mouse (USB 1.1 / 2.0 reference).
Sockets: each dongle socket, the spare laptop socket, any front or rear port of other machines if handy, the same with a USB 3 cable or extension, and through a hub.

Record in a table:

| device | socket | label | mbit | ss | peer | DOWNGRADED? | what I expected |
|--------|--------|-------|------|----|------|-------------|-----------------|

Questions the table should answer:
1. Does a USB 3 device in a truly USB 2 socket look different from the same device in a USB 3 socket with a bad contact? (Probably not: if peer state differs, use it.)
2. Is the `peer` state reliable on every socket type (laptop ports, hubs)? Does it ever read `configured` while the device is on the USB 2 half?
3. Are there USB 3 devices that do not carry the SuperSpeed capability in `bos_descriptors`? (Then they would never be flagged.)
4. Any false alarms: devices reported DOWNGRADED that are fine (hub halves are handled; check docks, card readers).
5. Does unplugging and replugging the same stick change the result (worn contacts, partial insertion)? Try wiggling it gently.
6. Does heat or a long transfer change the link (a USB 3 stick may re-enumerate at USB 2 when it overheats)? Compare before and after.

## Decision

From the table, choose the wording shown next to a drive in the import and export dialogs (already implemented first version):

- `USB 3.0` / `USB 3.2`: nothing to say.
- `USB 2.0`, device not SuperSpeed capable: "USB 2.0" only.
- `USB 2.0` (or `USB 1.1`), device SuperSpeed capable: "supports USB 3 but is connected as USB 2.0: try another socket". Include the peer state only if the experiment shows it adds certainty.

## Implementation notes

- Detection code: `src/trakcrate/usb_speed.{h,cpp}` (`usbLinkOfDevice`, `usbLinkOfPath`, `bosHasSuperSpeed`, `UsbLink::downgraded()`); the dialogs use `usbLinkOfPath(mountPoint)`.
- The tool shares that code, so what it prints is exactly what the dialogs will use.
- Out of scope: measuring speed, writing to drives, USB-C alternate modes, Thunderbolt.
- Not registered in the geany-progress sidebar (by request).

## Findings so far

- 2026-10-27: the KINGSTON stick connected as USB 2.0 (480 Mbit/s, `DOWNGRADED`, SuperSpeed half of the port `not attached`) after a bad insert. Unplugging and plugging it in again in the same dongle socket gave USB 3.0 (5000 Mbit/s). The bad insert cost about an hour of import time, so the downgrade is now highlighted in red in the import and export dialogs.
- `vdj-usb-watch` reported the right state both times.
- The `peer` field is only meaningful for a device on the USB 2 side of a port. A healthy USB 3 connection reads `peer=not attached` too (the other half is simply empty).
