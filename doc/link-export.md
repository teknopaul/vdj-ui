# Exporting a pendrive as a LINK player

Status: **works on real hardware** (XDJ-700 as the browsing deck, 2026-10-24; switching pendrives with USB STOP / Insert on two XDJ-700, 2026-10-25). The other deck lists the tracks, browses them, loads a track from our
machine and plays it, with the preview waveform, beat grid and the main (scrolling) waveform, exactly as with a deck that has a USB stick in it.
Still marked experimental: one browsing deck model was tested, and it needs a privileged port (see below).

Network > **Export the Loaded Database as a LINK Player** (a checkbox) makes the virtual player behave like a deck with a USB stick in it, for
the other decks in LINK (player) mode. It is **off by default** and does nothing to the normal player mode unless it is ticked.

## Using it

1. Make UDP port 111 usable (**required**, see `doc/privileged-port-111.md`). When it cannot be opened vdj-ui offers to run `setcap` for you
   (polkit password prompt) and to restart itself.
2. File > Open Rekordbox Folder: the pendrive or export folder (the folder with `PIONEER/rekordbox/export.pdb` and `Contents/`).
3. Tick the Network menu checkbox. It exports the database that is open in the browser; opening another folder while it is ticked exports that one
   instead, opening a deck's database in the browser does not change it. The tick is remembered, the folder is not (open it again).
4. Connect. The export starts as soon as the join gave the virtual player its number (or at once when already connected) and stops with the
   connection. The other decks need a few seconds to notice: the stick appears in their LINK/media list.

`vdj-ui --link-export FOLDER` opens FOLDER in the browser and ticks the box. Do not use it together with the Rekordbox PC mode panel (same ports).

## What the other decks see (as with a real deck)

Reference captures: `pcaps/prolink-deck-to-deck-transfer.pcap` (two real decks), `pcaps/prolink-vdj-ui-as-link-exporter.pcap` (this export, working).
The order is the deck's, and every step needs the one before it:

1. **Status.** Our status says a stick is mounted: byte `0x6f` = 0 (04 without a stick), `0x74` and `0x77` = 1 (the stick-present flags, both follow the stick; which
   one a deck reads depends on its model), and `0x6a` flips between 04 and 06 about every half second.
2. **Mount.** The deck asks our portmapper on **UDP 111** for mountd and nfs, then mounts `/C/`. Without this nothing else happens.
3. **Media query.** The deck sends a type `0x05` query for our USB slot (slot 3); we answer with a 192-byte description of the stick (track and playlist
   count, label, date, "1000", total and free bytes), sent from an ephemeral port as a real deck does.
4. **Library.** It opens TCP 12523 (port query) and 1051 (the dbserver). The dbserver answers its menu, metadata, artwork, preview waveform, beat grid and
   waveform requests from `export.pdb` and the ANLZ files, with our player number. The replies are built to match a real deck's byte for byte.
5. **Audio.** When a track loads, the deck reads the file over NFS (`LOOKUP` from the root down `Contents/...` without regard to case, then `READ`s of the
   whole file). The log shows each mount, lookup and a line per burst of reads.

Services: portmapper UDP 111 and 50111, mountd UDP 50112, nfs UDP 2049, dbserver TCP 12523 and 1051. All read-only.

## Switching pendrives while running (USB STOP, Insert)

The drive viewer has a one-line tool bar: a list of your virtual pendrives (Network > *Exported Pendrive* > *Manage Virtual Pendrives...*) and the pendrives plugged into
this PC, **USB STOP** and **Insert**. Choosing a pendrive in the list only opens it in the viewer. **Insert** exports the chosen one to the decks, **USB STOP** unmounts the
exported one like pressing USB STOP on a deck. The one in the decks is marked "in the decks". USB STOP is shown dimmed when a deck is using the stick (it would be
refused); it can still be pressed, and then the decks show who is using it, as with a real deck. The popup has the same buttons (*Export to Decks (Insert)* and *Eject*).

What the decks see (reference captures `prolink-xdj-single-usb-eject-and-insert.pcap` for a real deck and `prolink-vdj-single-usb-eject-and-insert.pcap` for vdj-ui; details and
timings in `references/adj/doc/usb-switching-protocol.md`):

1. **USB STOP.** `0x6f` goes 00 -> 02 (about 1.5 s) -> 03 -> 04 and the stick-present flags go to 0. A deck refuses (back to 00 after about a second) when a player uses the stick.
2. **The decks unmount.** Within 10 ms of the 03 each deck that mounted us sends a mountd **UMNT** to our mountd (UDP 50112) and must get an empty *success* reply. Our mountd
   used to answer "procedure unavailable"; the decks then never mounted the slot again.
3. **Insert.** The stick-present flags go to 1, then about a second later `0x6f` goes to 00. A deck sees that, and about 0.9 s later sends portmap GETPORT, mountd MNT and the media
   query, then opens the dbserver, exactly as when the stick was first inserted.

**What it needs (found on the decks, 2026-10-25).** Run with `VDJ_UI_MODEL=XDJ`. In that mode vdj-ui announces itself the way an XDJ-700 does: keep-alive byte `0x25` = 01 (we sent 02,
which the decks seem to take for a computer, so they ignored our USB slot state), and the 284-byte status packet (0x11c, length field 0xf8) instead of the 212-byte CDJ-1000 one.
With the default CDJ-1000 identity the decks do not react to USB STOP at all. It was not tested which of the two XDJ-mode changes the decks need; both are in place.

Known gaps: the default (CDJ-1000) identity is unchanged and does not support switching; what a deck does when a stick is pulled while in use, or while browsing at the moment of
STOP, has not been captured (the "anomaly" tests in the plan); the decks sometimes show an "older firmware" warning for the virtual player.

## What made the main waveform appear

Everything worked except the main waveform until the 4-byte field at offset 16 of the waveform-detail blob (`0x2904` reply) was changed from 0 to the value a real
deck sends (`0x0cf233a0`, a pointer into the serving deck's memory, meaningless to a client but never zero on a real deck). The beat grid's head has the same
kind of field (`0x0ceea950`, `0x0ceeeab4` seen) and now also carries a non-zero value. Lesson: copy such "junk" fields from a capture instead of zeroing them.

## Logs and checks

- The Debug panel (category LINK) and `~/.local/share/vdj-ui/linkexport/`: `link-export.log` (what the export did and when, including every USB slot byte it sent: line it up
  with a capture), `link-nfs.log` (mount, **unmount**, lookups, reads), `dbserver-<ip>-<time>.log/.bin`
  (every dbserver request and reply of a session), `unknown-requests.log`.
- A capture to compare with the references (unfiltered, `tcpdump -i <iface> -w file.pcap`, the browsing deck already running): the media query about a second
  after the mount, then TCP 12523 and 1051, then NFS reads when a track loads. `references/adj/tools/nfs-decode.py <pcap>` prints the NFS conversation and
  `references/adj/tools/usb-state-summary.py <pcap> --ip <our ip>` the USB slot bytes, UMNT/MNT calls, media queries and TCP opens in time order (our own rows starred).

## Known gaps

- Request `0x3100` (once per track load) is answered with a count of 0; a real deck answered with a per-track count (2 and 1 in the first capture). What it
  counts is unknown. Nothing visible depends on it so far.
- Cue points, loops and the colour/extended waveforms are served from the ANLZ files but only the monochrome waveform was verified on a deck.
- Only an XDJ-700 was used as the browsing deck; only a 7-track export with MP3 files.
- Port 111 needs a one-off privilege (`setcap`, offered by the app); it is lost when the binary is rebuilt.

## Not the same as the Rekordbox PC mode

The *Rekordbox PC mode* (`doc/rekordbox-mode.md`) pretends to be a computer running rekordbox (device 17). It was built from captures of a real rekordbox PC that only
carry UDP (no capture of a PC serving files or metadata exists), and the decks only get as far as mounting. LINK export copies a real deck's behaviour, for which we have
complete captures, and is the one that works.

## Code

`src/network/link_export.*` (the media answer, the dbserver, the file service), `NetworkManager::setLinkExportFolder` and the menu in `MainWindow`; the dbserver
in `src/dbserver/`; the file service in libcdj `src/nfs/` (`nfsd.c`, `nfs_dirfs.c`, `portmap_server.c`; test `make nfsd-test`); libcdj `vdj_t.usb_loaded` (status bytes)
and `raw_update_h` (media queries). Tests: `vdj-ui-linkexport-test`, `vdj-ui-integration-test`.
