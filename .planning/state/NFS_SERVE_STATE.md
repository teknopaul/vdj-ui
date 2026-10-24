# NFS serving + rekordbox PC mode: handoff state (written 2026-10-24, ~22:45)

Read `doc/joining-the-link.md` (clean join of the link), `references/adj/doc/nfs-deck-transfer-notes.md` (real NFS
conversation) and `references/libcdj/doc-internal/how-to-pose-as-rekordbox.md` first. Rules: the user does ALL git; never run a
git command (also not `git -C`); never run sudo (the user runs `sudo make install` in references/libcdj, then asks for a rebuild);
build trees: `build/` (the user's) AND the repo root (mine), build both: `make -C build -j8; make -j8`; tests `cd build && ctest`.

## Done this session (all committed by the user)
- Clean join of the link as a virtual player (no "older firmware" popup): see doc/joining-the-link.md.
- libcdj: threads are joinable and joined by stop/destroy, sockets closed once, SO_RCVTIMEO 100 ms, progress callback,
  claim silence, `vdj_cycle_test`. vdj-ui: Disconnect resets the UI, toolbar removed, BPM follows the deck we are synced to.
- rekordbox PC mode (src/dbserver/*): presence matches the real PC (claim, 0x29 status every 100 ms, unicast name, 0x10 poll ->
  0x16 reply, peer count). NfsObserver logs RPC and listens on UDP 111 and **50111**, mountd fixed on **50112**, nfs 2049.

## What the decks do (captures)
- Deck -> PC (prolink-vdj-ui-as-rekordbox2.pcap): GETPORT(mountd) to our UDP **50111**, then mountd proc **5 EXPORT**; we answer
  PROC_UNAVAIL so the deck shows nothing and times out ("not responding"). The path it mounts after EXPORT is unknown yet.
- Deck -> deck (pcaps/prolink-deck-to-deck-transfer.pcap in the repo, decode with references/adj/tools/nfs-decode.py): portmap on UDP 111 of the serving
  deck, mountd port 48276 (ephemeral), `MNT "/C/"` (UTF-16LE), NFS v2 UDP 2049; per read burst LOOKUP from the root each time
  (Contents / intapol / UK Dembow / file.mp3, lower-case: names must match case-insensitively), GETATTR, READ last 160 bytes,
  READ 0..2048, then the whole file in 2048/8192 byte reads from several places at once over many client sockets.

## libcdj NFS server (references/libcdj/src/nfs, src/c/vdj_nfs.c): assessed
Solid: NFS v2 NULL/GETATTR/LOOKUP/READ/READDIR/STATFS, mount v1 NULL/MNT/EXPORT, callback struct `nfs_ops_t` (nfs_server.h),
`nfs_server_init/poll/destroy`, fh = fsid+inode (nfs_fh_make/parse). Gaps: (1) filesystem is a hard-coded fake tree
(/C/PIONEER/rekordbox/export.pdb); (2) exact UTF-16 name compare; (3) no portmap server (registers with system rpcbind, needs root);
(4) sources are NOT in libcdj.so and `vdj_nfs.h` is installed but has no implementation: the standalone `vdj-nfs` binary only.
vdj-ui `xdj-nfs` lib already compiles libcdj's NFS *client* sources from the libcdj tree (CDJ_NFS_DIR in src/CMakeLists.txt).

## Plan (the user: serve the real rekordbox folder/pendrive that the UI already loads; portmapper without root; libcdj work needed)
1. libcdj `src/nfs/nfs_dirfs.c/.h`: `nfs_ops_t` over a real directory (read-only; case-insensitive LOOKUP; path<->inode table; pread;
   READDIR; STATFS; export names "/C/" etc.; no path escapes). C test.
2. libcdj `src/nfs/portmap_server.c/.h`: tiny UDP portmap (NULL, GETPORT, DUMP) with a registration table; binds high ports (50111) without root,
   111 when allowed.
3. libcdj `src/nfs/nfsd.c` + installed header `cdj/vdj_nfsd.h`: `nfsd_start(cfg)` runs portmap(s)+mount+nfs on one thread (poll()), `nfsd_stop`;
   a log callback for MNT/LOOKUP/READ lines. Add to libcdj.so (Makefile OBJS, install). Replay test from the deck-to-deck calls.
4. vdj-ui: RbMode starts nfsd with the export root (the folder holding PIONEER/ and Contents/) instead of the logging-only observer
   (keep the observer's decoding as the log); panel option "Serve files over NFS". Answer EXPORT, see which path the deck mounts.
5. Then dbserver side: the deck gets the audio path from request 0x2102; check the whole flow on a real deck; document.

## Progress log (append below)
- [x] 1 nfs_dirfs (references/libcdj/src/nfs/nfs_dirfs.c/.h)  - [x] 2 portmap_server.c/.h  - [x] 3 nfsd.c/.h (+ Makefile targets `make nfsd-test`, part of `make test`;
  test src/test/nfsd_test.c passes under ASan, ports 53111/53112/53049)
  DECISION: the server is NOT put into libcdj.so (the .so has no NFS code at all, not even the client); vdj-ui compiles the libcdj/src/nfs sources itself, as
  its `xdj-nfs` lib already does for the client (CDJ_NFS_DIR in src/CMakeLists.txt): no `make install` needed for this work.
- [x] 4 DONE (vdj-nfsd lib in src/CMakeLists.txt, RbMode starts nfsd, RbModeOptions.serveNfs/altPortmapPort, --rb-no-serve, panel checkbox, test modeServesTheExportOverNfs, docs). Was: add nfs_server.c mount_server.c nfs_dirfs.c portmap_server.c nfsd.c to xdj-nfs (or a new lib); RbMode starts nfsd_start({root = the export
  folder the library was loaded from, log -> RbMode::forward}) instead of / next to NfsObserver; panel checkbox "Serve files over NFS"; CLI --rb-no-serve;
  doc/rekordbox-mode.md. nfsd defaults: portmap 50111 + 111, mount 50112, nfs 2049.
- [ ] 5 real deck test (NEXT: user runs build/src/vdj-ui with the mode and a deck; no setcap needed now, 50111 is unprivileged): the deck sends EXPORT first on the PC; watch the log for the MNT path it mounts; if the export name is wrong, add it to `exports`.

## Result of the first real-deck test of the file service (2026-10-24 22:52, /var/tmp/prolink-vdj-ui-as-rekordbox.pcap)
Deck 4: GETPORT(mount) to 50111 -> 50112 ok, mountd **EXPORT** -> we replied a valid list with `/C/` (UTF-16LE) -> the deck then sent NOTHING more (no MNT,
no RPC, no TCP), only its 0x10 polls every 2.5 s, and **its UI hung until power-cycled**. Before EXPORT was implemented it retried and timed out harmlessly.
Hypothesis: the PC's export names must match the absolute file paths of the pdb (the deck picks the export that is a prefix of the track path), `/C/` matches nothing.
Added: option/CLI/panel "NFS export names" (`--rb-nfs-exports`, default `/`). NEXT: try `/` (log should show MNT "/"), then others one at a time; each failure may need a deck reboot.
Idea if that fails: answer EXPORT with an empty list (see if the deck recovers), or export the track folders from the pdb file paths.

## Run 3 (23:00, /var/tmp/prolink-vdj-ui-as-rekordbox3.pcap): export name `/` instead of `/C/`: SAME result, the deck freezes right after a valid EXPORT list.
So the name is not the cause. Reply bytes verified correct XDR (1,len,name,0,0). Added a runtime switch for the EXPORT answer:
`--rb-nfs-export-style list|group|empty|unavailable` / panel combo (libcdj: nfsd_config_t.export_style, nfs_types.h export_entry_t.group,
mount_server: list_exports returning -2 = PROC_UNAVAIL). LADDER to try, one per run (reboot the deck when it freezes): `group`, `empty`, `unavailable` (known harmless).
If `group` or `empty` do not freeze but the deck never MNTs, the PC probably needs a different name or a TCP/dbserver step first; look at what the deck
sends after (the pcap), and check Beat Link / crate-digger notes for how a rekordbox computer is mounted.

## Run 4 (23:11): style `empty` -> the deck survives (no freeze) but does nothing more (no MNT: nothing to mount). So the freeze needs a non-empty list.
Best hypothesis: the deck's EXPORT parser dereferences the groups of a node; a node with NO group list (what we sent) hangs it, real servers always send one.
NEXT: style `group` (panel "a list, with the group * on each"). If it still freezes: other group names ("(everyone)", an IP), UTF-16 group name vs UTF-8.
(2023: mount_server.c now calls list_exports once per EXPORT call; the old double call is gone.)

## Run 5 (23:2x): style `group` -> the deck still hangs. Any non-empty EXPORT list hangs it; empty / unavailable are safe. Default style is now `empty`.
Hypothesis: in the real flow the media query (slot 4) and the dbserver TCP come BEFORE any NFS; our deck never sends a media query (0 in all pcaps) and
polls (0x10) every 2.5 s forever. The real PC sent an unsolicited 0x16 ~0.2 s before each media query (14.00 reply, 21.54, 38.48, 39.10). Added: RbPresence
announces 0x16 to each known deck every 3 s (--rb-announce-ms, panel-less). NEXT: run with empty+announce, check the pcap for the first type-5 (media query) and
TCP to 12523/1051; if they appear the dbserver conversation can start, then revisit non-empty EXPORT.

## NEW DIRECTION (user, 2026-10-24 23:3x): LINK mode export, NOT rekordbox PC mode
The rekordbox PC mode (src/dbserver/rb_*) is a different thing; the old rekordbox PC only talked UDP, no pcap of a Windows conversation exists, so the NFS/EXPORT
experiments against it were misguided (a deck has never been seen to do NFS with a PC). The REAL reference is **pcaps/prolink-deck-to-deck-transfer.pcap**: two decks in
player (LINK) mode, deck 1 serves its USB stick (metadata over dbserver TCP 12523/1051, files over NFS `/C/` via portmap UDP 111, mountd, nfs 2049), deck 4 browses/loads.
GOAL: in the normal Connect mode (virtual player) add an OPTION (checkbox, default OFF, so working player mode is not broken) "Export a pendrive/rekordbox folder as a LINK
player": the virtual player then behaves like deck 1: status claims USB loaded, answers media queries for its USB slot (type 0x05/0x06 on 50002), runs the dbserver
(DbServer + DbHandler, already replicate a real deck's replies byte for byte, built from the first tcp capture) with its OWN player number, and nfsd (export `/C/`,
portmap on 111 needs setcap cap_net_bind_service, 50111 also bound) for the files. REUSE: RbLibrary/DbHandler/DbServer (src/dbserver), nfsd (libcdj src/nfs, built into vdj-nfsd),
NetworkManager (virtual player, libcdj vdj_t). PLAN: (1) analyse what deck 1 sends/answers in the pcap besides what we have (status USB-slot bytes, media query/response for
slot 3, who connects where); (2) NetworkManager hooks: status USB flags + media responder + start/stop DbServer/nfsd; (3) UI checkbox + folder choice in Network menu or config,
default off, persisted; (4) tests; (5) real deck test. Do NOT touch the idle status shape when the option is off.

### Findings about a SERVING deck (pcaps/prolink-deck-to-deck-transfer.pcap, prolink-w-tcp.pcap, 2026-10-24), for the LINK export
Order in the real flow (automatic, no user action): serving deck claims + keepalives (t) -> ~1 s later the browsing deck sends a MEDIA QUERY (type 0x05, 48 bytes, UDP 50002):
`01 00 <asker dev> 00 0c <asker ip> 00 00 00 <target dev> 00 00 00 <slot>` (slot 3 = USB) -> serving deck answers type 0x06, 192 bytes, FROM AN EPHEMERAL PORT to asker:50002 (same layout as
our rb_packets mediaResponse(): 0x1f `01 00 <dev> 00 9c`, 0x24 dev u32, 0x28 slot u32, 0x2c name UTF-16BE (64 bytes; the stick's label, may be empty),
0x6c date "2025-04-11" UTF-16BE, then "1000" UTF-16BE (export version?), 0xa6 u16 track count (0x0896), 0xaa `01 01 00 00 00 78` (0x78 = playlists), then two u64-ish sizes
at 0xb4.. (total bytes 0x0e66fe8000, free bytes 0x06c8d98000)) -> in the SAME instant the asker opens TCP 12523 (port query) then TCP 1051 (dbserver session, ~260 packets).
Files later over NFS (portmap UDP 111 of the serving deck, mountd, nfs 2049, `MNT "/C/"`).
STATUS bytes of a serving deck (284-byte status): no USB: 0x6a=04 0x6b=04 0x6f=04 0x73=04 0x74=00; USB mounted/loaded: **0x6f=00**, **0x74=01** ("link"/media mounted; the other deck also sets 0x74=01 once it
sees a peer), 0x6a toggles 04<->06 about every 0.5 s while a stick is in (activity LED), 0x73=04 (SD empty). Loaded track: 0x28..0x2f = `01 03 01 00 00 00 00 <id>`
(loaded player = the serving deck, slot 3, type 1 rekordbox, track id), 0x7b play state.
libcdj notes: libcdj sets 0x75/0x77/0x68 oddly (the real flag is 0x74); the idle block I added in cdj.c sets 6a/6b/6f=04, 0x75/0x77=0.
IMPLEMENTATION SKETCH: (a) libcdj vdj_t gets `usb_loaded` (+ `usb_blink` tick) -> cdj_create_status_packet writes 0x6f=0,0x74=1,0x6a blink when set; (b) a raw handler for non-status
packets in vdj_handle_managed_update_datagram so vdj-ui can answer type 0x05 (reply from an ephemeral QUdpSocket); (c) NetworkManager owns, when the option is on and after the join gave us
a player number: RbLibrary(folder) + DbServer(deviceNumber=our id) + nfsd(root=folder, exports {"/C/"}); (d) UI: checkable Network menu item "Export a pendrive as a LINK player..." (folder
chooser, remembered in ConfigManager, DEFAULT OFF) + CLI --link-export DIR; (e) tests with the fixtures (export7) replaying a deck's media query + the captured dbserver client stream.

## LINK export: STATUS (2026-10-24 ~23:50)  -- built, tested in isolation, NOT yet run against real decks
DONE: rb_packets MediaInfo extended (fromPlayer, date, version, total/free bytes; test deckMediaResponseMatchesTheCapture, fixture udp/deck_media_response.bin);
libcdj vdj.h/vdj.c: `vdj_t.usb_loaded` (status 0x6f=0, 0x74=1, 0x6a blink) and `raw_update_h` (default case of vdj_handle_managed_update_datagram);
src/network/link_export.{h,cpp} (LinkExport: RbLibrary + DbServer(deviceNumber) + nfsd + media-query responder from an ephemeral socket);
NetworkManager: setLinkExportFolder(), startLinkExport() after connected(), stopLinkExport() in teardown(); MainWindow: checkable Network menu item
"Export a Pendrive as a LINK Player..." (config section link_export: enabled/folder, DEFAULT OFF), setLinkExport(), CLI --link-export; docs doc/link-export.md, man page;
test tests/test_link_export.cpp (vdj-ui-linkexport-test passes: media response, dbserver port, NFS mount+read via /C/).
BUILD NOTE: needs the new libcdj header+lib INSTALLED (the user runs `sudo make install` in references/libcdj) before vdj-ui builds/runs; until then compile with
CPLUS_INCLUDE_PATH=<scratchpad>/inc (a symlink dir cdj -> references/libcdj/src/c) and do NOT run tests that connect (struct size mismatch). Configure build/ with `cmake -S . -B build`
(NOT `cmake ..` from build/, that reconfigures the repo root).
NEXT: user installs libcdj, rebuild both trees, ctest; real test: Connect with the option on, a second deck should show the stick, browse, load a track; capture
unfiltered and compare with pcaps/prolink-deck-to-deck-transfer.pcap (media query at +1 s, TCP 12523 then 1051, NFS). Things likely to need tuning: media response fields,
status bytes (e.g. 0x28 loaded-track block, play-state), dbserver slot handling (the captured session was slot 3), nfs portmap 111 needs setcap.

### UPDATE 23:5x: the menu item is now a plain CHECKBOX "Export the Loaded Database as a LINK Player" (no folder chooser, user request): it exports TrackBrowser::exportRoot()
(the last local rekordbox folder opened in the GUI; signal databaseLoaded re-applies when another folder is opened). Config link_export/enabled only. setLinkExportEnabled()/applyLinkExport()
in MainWindow; --link-export DIR opens DIR in the browser and ticks the box. The user had tried the first version (folder chooser) and the deck did NOT show our stick:
libcdj 'usb_loaded' build was installed. The next step is the user's pcap of this version: look for (1) our status bytes 0x6f=0/0x74=1/0x6a blink, (2) a type-5 media query to us ~1 s after the
join (if none: the deck does not consider us a media source: compare our whole status packet with deck1's status in pcaps/prolink-deck-to-deck-transfer.pcap incl. 0x28.. loaded-track block,
0x7b, firmware 0x7c, model name), (3) TCP 12523/1051 to us, (4) NFS. Needs `setcap cap_net_bind_service=+ep` on the binary for portmap 111. Tests: 20/20 in build/.

### Result of the first LINK export pcap (/var/tmp/prolink-vdj-ui-as-link-exporter.pcap, 23:49)
Deck 4 reacted to our USB status: from +3 s after our join it sent GETPORT(mountd v1) to OUR UDP 111 once a second (21x); nothing was bound there (binary has no cap_net_bind_service,
getcap empty) -> ICMP port unreachable -> the deck never mounted, so NO media query and NO TCP. REAL ORDER (re-read from the reference pcap): portmap 111 -> MNT "/C/" -> media query (slot 3) -> TCP 12523 -> 1051 -> files.
FIX: vdj-ui now shows a warning box (NetworkManager::linkExportNeedsPort111) with `sudo sysctl -w net.ipv4.ip_unprivileged_port_start=111` (survives rebuilds) or setcap. NEXT: user applies one,
switch the export off/on (or reconnect), new pcap: expect GETPORT mount->50112, GETPORT nfs->2049, MNT "/C/" ok, then media query, TCP 12523, 1051.

## LINK export WORKS (2026-10-24 23:57, after port 111 was opened): the deck lists/browses/loads/plays our tracks exactly like a real deck. ONE gap: the MAIN (scrolling) waveform is not drawn while playing.
Checked: NFS clean (263 reads, 0 errors, whole file), dbserver TCP healthy (deck acks the whole 35 KB 0x2904 reply), our 0x2904 blob == the real deck's byte for byte except 4 bytes
(offset 16..19: a pointer into the serving deck's memory, real 0x0cf233a0, we sent 0). Same for the beat grid head (real 0x0ceea950/0x0ceeeab4, we sent 0).
CHANGE MADE: both pointers are now the real non-zero constants (db_blobs.cpp). UNVERIFIED hypothesis (a firmware "is it set" check). If the waveform still does not appear:
(1) OPEN: request 0x3100 (sent once per track load, args `<DMST menu 3 slot 3 type 1> <trackId> 0 0`) is answered by us with `4000 (3100, 0)`; the real deck answered with a COUNT that differed per track
(track 3 -> 2, track 2 -> 1 in tests/fixtures/dbserver/server_stream.bin); unknown what is counted (not memory cues: the ANLZ has 0 memory/3 hot cues for both; not play counts). Find it from the history/playlist/ANLZ data
of export7 or from more captures (two more skipped 0x3100 in the oracle test). (2) compare the deck's requests when it DOES draw the waveform on a real deck (play a track from a real deck with the unfiltered pcap and
list every dbserver request and reply in order, not only the first capture's). (3) the 0x2504 blob is 1604 bytes of zeros on the real deck too (two junk bytes) so it is not that.

## FINAL STATE (2026-10-25 ~00:30): LINK EXPORT WORKS ON A REAL XDJ-700 (browse, load, play, preview + beat grid + MAIN waveform)
The waveform appeared after the two head pointers were made non-zero (see doc/link-export.md "What made the main waveform appear"). Reference pcap in the repo:
pcaps/prolink-vdj-ui-as-link-exporter.pcap (+ pcaps/prolink-deck-to-deck-transfer.pcap). Docs written: doc/link-export.md, doc/privileged-port-111.md (sysctl permanent / setcap /
nft redirect / future in-app polkit idea), notes corrected in references/adj/doc/nfs-deck-transfer-notes.md. The user asked for NO more code changes that night.
TODO (not started): (1) in-app solution for port 111 (polkit + setcap of the real binary, a Grant button in the warning box, check getcap at start; or a helper); (2) request 0x3100 count;
(3) test other browsing deck models (XDJ-1000, CDJ-2000) and colour/extended waveforms, cues, loops, artwork; (4) the rekordbox PC mode (src/dbserver/rb_*) is a separate, unproven pretence:
decide whether to keep it; (5) commit decisions are the user's (nothing here was committed by the agent; build trees: build/ and repo root, configure with `cmake -S . -B build`).
