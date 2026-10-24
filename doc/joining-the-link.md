# Joining the Pro DJ Link network cleanly

How vdj-ui (and the rekordbox PC mode) join a link with real XDJ players without the players showing an
"older firmware" warning, what was wrong, and how it was found. Captures were taken with a mirrored port and
compared byte for byte with the players' own packets. Date of the work: 2026-10-24.

## Result

A vdj-ui virtual player now joins two XDJ-700s with no warning, and the physical decks stay visible in the GUI.
The warning had two independent causes in our own traffic, plus one GUI regression introduced on the way.

## What the real players do

Join sequence of a player (all on UDP 50000, broadcast), about 0.3 s between packets:

1. hello, type `0x0a`, 3 packets
2. stage 1, type `0x00`, 3 packets (MAC)
3. stage 2, type `0x02`, 3 packets (player number proposed)
4. stage 3, type `0x04`, 3 packets (player number taken)
5. keep-alive, type `0x06`, immediately after stage 3 and then every 2 s. **No keep-alive before stage 3.**
6. status, type `0x0a` on UDP 50002, unicast to every other player, from about 0.1 to 0.6 s after the first keep-alive.

Other facts seen in the captures:

- A player already on the link answers a joiner's stage 3 at once with a unicast type `0x05` from port 50000
  (its own player number plus the counter it received). Only the player that has been up longest does this.
- Keep-alive byte 0x30 is the number of players on the link including the sender: 1, 2, 3 as they join.
- Every status packet goes to every peer at the same instant with one shared counter (bytes 0xc8..0xcb) that
  goes up by one per packet. The real players send two packets about 104 ms apart and skip a slot, repeating every
  304 ms. We send a flat 200 ms; the players do not seem to mind.
- An idle player sends a 284-byte status with nothing loaded: loaded-player, track type and track id zero,
  media slots `04`, beat number `ffffffff`, bpm `ffff`.
- A rekordbox PC claims differently from a player: stage 1 x3, then **36 stage-2 packets** cycling the numbers
  17, 18, 41, 42, 43, 44 (counter = round 1..6, 0.11 s apart), no hello and no stage 3. About 2 s after its
  keep-alives start a deck sends it a type `0x10` poll on UDP 50002; the PC answers with type `0x16`.

## What was wrong

| Where | Problem | Fix |
|---|---|---|
| libcdj managed discovery thread | started before the claim, so its first keep-alive (after 2 s) landed in the middle of stage 2 | `claiming` flag, keep-alives are silent until the claim is done, the first one goes out right after |
| vdj-ui main window | a second keep-alive timer (1 s) ran next to the library's | timer removed |
| libcdj `vdj_link_member_count` | counted `active`, which is the player's playing state and is overwritten by every status packet, so byte 0x30 flapped 3,2,1 | counts members that have not timed out (`!gone`) |
| libcdj stage-3 reply | the `0x05` acknowledgement went out late (the loop read the socket every 2 s) and from an ephemeral port | loop polls every 50 ms, reply sent from the discovery socket (port 50000) |
| vdj-ui status start | status waited for a master, or 5 s; an idle link has no master | starts on the first deck status (sync counter and master are known by then) |
| libcdj idle status | claimed a track loaded from ourselves. The players answered with an RPC portmapper `GETPORT` for mountd to our UDP 111 every second (we have no NFS server) and the warning followed | when not playing, report an empty player byte for byte like an idle XDJ-700 |
| rekordbox mode presence | single stage-2 number, a stage 3, no poll reply, constant peer count | follows the real PC (see above) and answers the `0x10` poll |

Not causes: the model name (CDJ-1000 against XDJ-1000 made no difference), the firmware string, the packet
sequencing of status.

## GUI changes in vdj-ui

- The join is shown in the status bar and the packet log: hello, MAC, "asking for player number N", "taking player
  number N", "Joined the link as player N". libcdj has a progress callback for it (`vdj_t.progress_h`).
- **Regression found and fixed:** the callback processes Qt events so the status bar repaints. That delivered the
  decks' "joined" events before `connected()`, and the `connected` handler then removed every non-VDJ row as a
  placeholder. `NetworkManager` now ignores deck events while joining (`isJoining()`); the next keep-alive or
  status announces them, within about 2 s.
- `VDJ_UI_MODEL=XDJ` announces an "XDJ-1000" instead of the default "CDJ-1000" (experiment only).
- `pickInterface` prefers the 169.254.x.x address of a named interface that also has a routed address
  (for example `enx00e04c68019a` with 10.10.10.11 for the router).

## Files

vdj-ui: `src/dbserver/rb_presence.*`, `rb_packets.*`, `rb_mode_options.h`, `rb_netif.cpp`,
`src/network/network_manager.*`, `src/ui/main_window.*`, `tests/test_rb_presence.cpp`,
`tests/test_rb_packets.cpp`, `tests/fixtures/dbserver/udp/deck_poll.bin`, `rb_poll_reply.bin`,
`tests/fixtures/dbserver/extract_udp_fixtures.py`, `tests/fixtures/dbserver/REPLY_LAYOUTS.md`.

libcdj (separate repository, installed with `sudo make install`): `src/c/vdj.h`, `vdj.c`, `vdj_discovery.c`,
`cdj.c`; correction section in `doc-internal/how-to-pose-as-rekordbox.md`.

## Checking a new capture

Capture unfiltered (`tcpdump -i <iface> -w file.pcap`), then in the capture look for, in this order:
no keep-alive from us before our stage 3; our `0x05` reply within a few ms of a deck's stage 3 from port 50000;
byte 0x30 of our keep-alive rising 1,2,3 steadily; our first status within a second of the join; **no UDP 111
traffic** to us. The tools `references/adj/tools/rbmode-summarize.sh` and the tests under `tests/fixtures/dbserver`
hold the packet layouts.

## Still open

- Playing: while playing, the status still claims a track loaded from us. The players will probably ask for our
  NFS (portmap, mountd) then; libcdj has a `vdj-nfs` server for that. A real rekordbox PC does not run NFS.
- The status packet is 212 bytes, subtype 3, where the XDJ-700 sends 284 bytes, subtype 4.
- Which deck acknowledges a joiner (only the longest-running one so far).
- The rekordbox PC mode was fixed from the captures but has not been run again on real decks since.
