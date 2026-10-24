# Exporting a pendrive as a LINK player

Status: **works on real hardware** (XDJ-700 as the browsing deck, 2026-10-24). The other deck lists the tracks, browses them, loads a track from our
machine and plays it, with the preview waveform, beat grid and the main (scrolling) waveform, exactly as with a deck that has a USB stick in it.
Still marked experimental: one browsing deck model was tested, and it needs a privileged port (see below).

Network > **Export the Loaded Database as a LINK Player** (a checkbox) makes the virtual player behave like a deck with a USB stick in it, for
the other decks in LINK (player) mode. It is **off by default** and does nothing to the normal player mode unless it is ticked.

## Using it

1. Make UDP port 111 usable (**required**, see `doc/privileged-port-111.md`).
2. File > Open Rekordbox Folder: the pendrive or export folder (the folder with `PIONEER/rekordbox/export.pdb` and `Contents/`).
3. Tick the Network menu checkbox. It exports the database that is open in the browser; opening another folder while it is ticked exports that one
   instead, opening a deck's database in the browser does not change it. The tick is remembered, the folder is not (open it again).
4. Connect. The export starts as soon as the join gave the virtual player its number (or at once when already connected) and stops with the
   connection. The other decks need a few seconds to notice: the stick appears in their LINK/media list.

`vdj-ui --link-export FOLDER` opens FOLDER in the browser and ticks the box. Do not use it together with the Rekordbox PC mode panel (same ports).

## What the other decks see (as with a real deck)

Reference captures: `pcaps/prolink-deck-to-deck-transfer.pcap` (two real decks), `pcaps/prolink-vdj-ui-as-link-exporter.pcap` (this export, working).
The order is the deck's, and every step needs the one before it:

1. **Status.** Our status says a stick is mounted: byte `0x6f` = 0 (04 without a stick), `0x74` = 1, and `0x6a` flips between 04 and 06 about every half second.
2. **Mount.** The deck asks our portmapper on **UDP 111** for mountd and nfs, then mounts `/C/`. Without this nothing else happens.
3. **Media query.** The deck sends a type `0x05` query for our USB slot (slot 3); we answer with a 192-byte description of the stick (track and playlist
   count, label, date, "1000", total and free bytes), sent from an ephemeral port as a real deck does.
4. **Library.** It opens TCP 12523 (port query) and 1051 (the dbserver). The dbserver answers its menu, metadata, artwork, preview waveform, beat grid and
   waveform requests from `export.pdb` and the ANLZ files, with our player number. The replies are built to match a real deck's byte for byte.
5. **Audio.** When a track loads, the deck reads the file over NFS (`LOOKUP` from the root down `Contents/...` without regard to case, then `READ`s of the
   whole file). The log shows each mount, lookup and a line per burst of reads.

Services: portmapper UDP 111 and 50111, mountd UDP 50112, nfs UDP 2049, dbserver TCP 12523 and 1051. All read-only.

## What made the main waveform appear

Everything worked except the main waveform until the 4-byte field at offset 16 of the waveform-detail blob (`0x2904` reply) was changed from 0 to the value a real
deck sends (`0x0cf233a0`, a pointer into the serving deck's memory, meaningless to a client but never zero on a real deck). The beat grid's head has the same
kind of field (`0x0ceea950`, `0x0ceeeab4` seen) and now also carries a non-zero value. Lesson: copy such "junk" fields from a capture instead of zeroing them.

## Logs and checks

- The Debug panel (category LINK) and `~/.local/share/vdj-ui/linkexport/`: `link-nfs.log` (mount, lookups, reads), `dbserver-<ip>-<time>.log/.bin`
  (every dbserver request and reply of a session), `unknown-requests.log`.
- A capture to compare with the references (unfiltered, `tcpdump -i <iface> -w file.pcap`, the browsing deck already running): the media query about a second
  after the mount, then TCP 12523 and 1051, then NFS reads when a track loads. `references/adj/tools/nfs-decode.py <pcap>` prints the NFS conversation.

## Known gaps

- Request `0x3100` (once per track load) is answered with a count of 0; a real deck answered with a per-track count (2 and 1 in the first capture). What it
  counts is unknown. Nothing visible depends on it so far.
- Cue points, loops and the colour/extended waveforms are served from the ANLZ files but only the monochrome waveform was verified on a deck.
- Only an XDJ-700 was used as the browsing deck; only a 7-track export with MP3 files.
- Port 111 needs a one-off privilege (below); there is no in-app way to get it yet.

## Not the same as the Rekordbox PC mode

The *Rekordbox PC mode* (`doc/rekordbox-mode.md`) pretends to be a computer running rekordbox (device 17). It was built from captures of a real rekordbox PC that only
carry UDP (no capture of a PC serving files or metadata exists), and the decks only get as far as mounting. LINK export copies a real deck's behaviour, for which we have
complete captures, and is the one that works.

## Code

`src/network/link_export.*` (the media answer, the dbserver, the file service), `NetworkManager::setLinkExportFolder` and the menu in `MainWindow`; the dbserver
in `src/dbserver/`; the file service in libcdj `src/nfs/` (`nfsd.c`, `nfs_dirfs.c`, `portmap_server.c`; test `make nfsd-test`); libcdj `vdj_t.usb_loaded` (status bytes)
and `raw_update_h` (media queries). Tests: `vdj-ui-linkexport-test`, `vdj-ui-integration-test`.
