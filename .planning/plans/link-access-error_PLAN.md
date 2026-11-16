# Link access error when a real deck edits a track from the exported virtual pendrive

Source: the user's report. A real deck that loaded a track from our exported virtual pendrive (LINK export,
`doc/link-export.md`) shows a **link access error** when the DJ changes something on that track (cue, loop, beat
grid, probably history). The Debug view's NFS window shows nothing. The deck's DISCOVERY packets are seen continuously
and our log line says `ip=?`, which looks like we answer wrongly.

This is a **debugging plan**: find the cause with evidence first, then fix. Every phase ends with a green `ctest`
(`vdj-ui-dbhandler-test`, `vdj-ui-dbserver-test`, `vdj-ui-linkexport-test`, `vdj-ui-integration-test` in particular).

## What the code and logs already show (do not re-derive)

Read from `~/.local/share/vdj-ui/linkexport/` (the user's own sessions, 2026-11-15) and the sources:

1. **The deck's edit traffic is dbserver (TCP 1051) traffic, not NFS.** `unknown-requests.log` (written by
   `DbServer::logUnknown`, `src/dbserver/db_server.cpp:186`) has requests that `DbHandler::handle`
   (`src/dbserver/db_handler.cpp:434`, `default: r = unknown(req)`) does not know. From two real decks
   (`169.254.231.210` and `169.254.177.253`), all on 2026-11-15, none of these exist in any of the repo's
   captures (`pcaps/*.pcap`, `/var/tmp/*.pcap`; only `0x2004` appears there):

| type                | args as logged                                                                                                | guess (UNVERIFIED)                                                                                                                                            |
|---------------------|---------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `0x2705`            | `[#1 DMST+type 0x1080301/0x4080301, #2 track id, #3 1..3, #4 0x4c, #5 blob(76)]`                              | **save a cue / loop** to the media: the 76-byte blob holds `e8 03` (1000), two times in ms (e.g. 33253 and 41058 = a loop), `ffffffff` fields. Sent 10x today |
| `0x3001`            | `[#1 DMST+type, #2 track id]` (about 1 min after a `0x2705`/track load)                                       | track finished / unloaded: **history** or play count, or "commit"                                                                                             |
| `0x2005`            | `[#1 DMST+type, #2 slot 3, #3 track id, #4 0, #5 blob(0), #6 0x384, #7 blob(900)]`                            | **write back the 900-byte preview waveform** (same size as our `0x4402` reply to `0x2004`)                                                                    |
| `0x2605` + `0x2804` | `[#1 .., #2 track id, #3 0xffffffda / 0 / 0xffffffd8]` then `[#1 .., #2 track id]`, in pairs, 4 times in 17 s | repeated parameter write (signed value: -38, 0, -40, 0: gain / trim? or beat-grid shift?) followed by a commit/refresh                                        |
| `0x0001`            | no arguments, `tx` counts up by 2, 279 of them, about 30 ms apart                                             | not a menu or metadata request; maybe a poll/keep-alive of the deck's db client, or the framing of a request we mis-parse                                     |

   The logged hex is complete for each (second line of each log entry), so these can be decoded offline.

2. **What we answer to them.** `DbHandler::unknown()` (`db_handler.cpp:87`) follows `UnknownPolicy`, default
   `EmptyOk` (`db_handler.h:103`), which sends `RepOk` (type `0x4000`, args `[requested type, 0]`). That is a *menu* success
   reply. A write request almost certainly expects a different reply, or none, so the deck treats the exchange as
   failed: **the likely origin of the link access error.** `LinkExport` does not override the policy (no `unknownPolicy`
   in `src/network/link_export.cpp`).

3. **NFS is not involved so far.** `link-nfs.log` has only MNT, LOOKUP, GETATTR, READ, UMNT and the EXPORT answer; no write
   procedure ever reached `w_unserved` (`references/libcdj/src/nfs/nfsd.c:125`), which would log
   `nfs procedure N asked for (not served)`. This matches the empty NFS window. **But the server's write handling has its own bug:**
   `dispatch_nfs` (`references/libcdj/src/nfs/nfs_server.c:349`) labels the NFSv2 procedure numbers one off (`case 7: /* WRITE */`,
   `8 CREATE`, `9 REMOVE`, `10 RENAME`, `13 MKDIR`, `14 RMDIR`; the real v2 numbers are WRITECACHE 7, WRITE 8, CREATE 9, REMOVE 10,
   RENAME 11, LINK 12, SYMLINK 13, MKDIR 14, RMDIR 15, READLINK 5). The behaviour (answer `NFSERR_ROFS`) happens to be right for
   all but **`RMDIR` (15) and none of 5/7 mattering**: 15 falls to `default:` and gets `PROC_UNAVAIL`. `nfs_client.h:11-22` carries the
   same off-by-one constants. A deck that writes (history file, ANLZ update) over NFS would get the wrong status for RMDIR and, more
   importantly, we have never checked what status a real deck's own read-only/failed write gives. Unknown whether this matters for
   the deck: audit it (phase 5), do not assume.

4. **Port 111.** `link-export.log` has four `portmap UDP 111 is not bound ... Permission denied` lines (`setcap` is lost when the binary
   is rebuilt) and `doc/link-export.md` warns that a deck that tries to mount without our portmapper "breaks and you have to reboot
   the deck before reading virtual pendrives works". Rule this out first when the user retests: check the last `link-export.log`
   lines show no such line for the session.

5. **The DISCOVERY log line is not a reply.** `onDiscovery` (`src/network/network_manager.cpp:131`) logs `p<pid> model=<m> ip=<ip>` for
   **every** discovery-port packet libcdj hands up: keep-alives, but also ID-use requests, id collisions and id-set requests
   (`parts/libcdj/src/src/c/vdj_discovery.c:361-436`). The `ip` is looked up in `v->backline->link_members[pid]`, only when
   `pid <= VDJ_MAX_BACKLINE` (32). It is `?` when (a) the packet is not a keep-alive (the member table is filled only for keep-alives,
   and the handler is called **before** the member exists for the ID-use / ID-set requests), or (b) the sender's player number is above 32
   (a DJM mixer is device 33). The line does not show the packet type, so "continuous DISCOVERY with ip=?" cannot be told from
   "a mixer's keep-alive every 1.5 s" or "a deck repeating an ID request because it does not like our answer". The only discovery
   replies we ever send: `id_use_resp` (when a deck claims **our** player number from another IP), `id_collision` (a keep-alive with
   our number from another IP), and `id_set_resp` (answer to a deck's ID-set request) (`vdj_discovery.c:380-432`).

## Open questions this plan must answer

- Q1: what do the real decks expect as a reply to `0x2705`, `0x3001`, `0x2005`, `0x2605`/`0x2804`, `0x0001`? (No capture of a deck answering these exists.)
- Q2: which of them (if any) is the one that raises the error, and do the decks also try something over NFS (which we have not seen)?
- Q3: what is the `ip=?` traffic: type, source IP, player number, and does our reply to it matter?

## Ground rules

- Never use git (the human does). Never use sudo; for libcdj install or `setcap` tell the human exactly what to run.
- Tool constraints: no `tshark`/`dumpcap`; `tcpdump` is available; scripts are stdlib-only Python 3. Captures are binary: summarise with scripts, never `cat` them.
- `references/libcdj` is a symlink to `/home/teknopaul/bzr_workspace/libcdj`; vdj-ui compiles libcdj's `src/nfs` sources itself (`CDJ_NFS_DIR`,
  `src/CMakeLists.txt:12`) but links libcdj's installed C library for the rest; libcdj changes need the human's agreement and a `sudo make install`
  by them, for libcdj `src/c` files. A copy of the libcdj C tree also lives in `parts/libcdj` (the snap build): leave it alone.
- Never send a real deck anything that was not seen from a real deck (the project's existing safety rule). Everything not seen in a capture is marked
  `// GUESS` in code and listed in the protocol note (phase 2).
- **USER STEP** marks work only the human can do (real decks, captures). Sonnet must stop, tell the human exactly what to do, and **not** run
  `geany-progress done` for that phase until the human confirms.
- The export stays **read-only** unless the human decides otherwise after phase 2: acknowledge the writes, do not apply them.

---

## Phase 1 — Observability: see every request the deck sends and what we answer

**Goal:** the next retest produces evidence for Q1-Q3 without anyone reading hex. No protocol behaviour changes.

### Files

- **edit** `src/network/network_manager.cpp` (`onDiscovery`, `network_manager.cpp:131-158`)
- **edit** `src/dbserver/db_server.cpp` / `db_server.h` (`logUnknown`, request/reply logging, `clientLog`)
- **edit** `src/network/link_export.cpp` / `.h` (forward the new log lines to the `logLine` signal, category LINK)
- **edit** `src/ui/main_window.cpp` (the Debug menu item for the raw TCP switch), `src/main.cpp` (`--link-tcp-raw`)
- **edit** `tests/test_db_server.cpp`, `tests/test_link_export.cpp`

### Changes

1. **DISCOVERY line.** Log the packet **type** (`cdj_packet_type(...)` is available on the packet the handler receives: add it to the
   log, hex and name: keep-alive 0x06, ID-use req/resp, collision, ID-set, stage1/2/3) and the **sender address from the datagram** if libcdj gives it (the member's
   `ip_addr`, else the `ip` field inside the packet: `cdj_discovery_packet_t::ip`, already decoded with `cdj_ip_decode`; use it when the
   member table has no entry so the `?` becomes a real address). Add `known=<0|1>` (is there a `link_members[pid]`) and `pid>32` as `pid=33(over backline)`.
   Rate-limit identical lines to one per 5 s per (pid, type) so "continuous" no longer floods the log, with a `xN` suffix of the number suppressed.
2. **Replies from us on the discovery sockets.** There is no log of what libcdj sends there. Without editing libcdj (not agreed), add the log line at the
   places vdj-ui can see: none. Instead write a stdlib script in phase 2 that lists UDP 50000/50001/50002 packets we send from a capture. For now note in the
   progress file that this is the gap.
3. **dbserver requests.** Every request is already logged per client in `dbserver-<ip>-<time>.log` (`RECV`, `REQ`, `REP` lines) but not in the
   Debug view. Add a `kind` tag to each REQ/REP line (`handled`, `unimplemented`, `unknown`; `HandlerResult::kind` already has it) and send **only
   the unknown/unimplemented ones, plus every type in the write set** (`0x2005, 0x2605, 0x2705, 0x2804, 0x3001, 0x3002, 0x0001`) as `logLine` text
   `DB: <ip> REQ <type> <decoded args>` and `DB: <ip> REP <what we sent>` to the LINK category of the Debug panel, so the deck's edit attempt is visible there.
   Keep one `unknown-requests.log` entry per request (it already is) but add a line with the reply we sent (`policy=EmptyOk -> 0x4000 [type 0]`).
4. **Raw TCP socket debug (the unreversed protocol).** What exists today (`db_server.cpp:92-245`): per client `dbserver-<ip>-<time>.log` and `.bin`; `.bin` has **only the bytes received**,
   no timestamps, no bytes we sent; the `RECV` line cuts each chunk at 64 bytes; nothing records why a socket ended, and the port **12523** (port query) socket has no per-client log at all. Add a
   switchable raw layer that does not depend on the message parser understanding anything (so it still works for `0x0001` and for framing the parser rejects):
   - **`dbserver-<ip>-<time>.raw.log`**, one line per socket event with a monotonic timestamp (ms, relative to the connect) and the wall clock on the first line:
     `+0.000 CONNECT peer ip:port local port`, `+12.4 RX 52 bytes <full hex>`, `+12.9 TX 33 bytes <full hex>` (**TX is new**: write the bytes of every reply as they go to `QTcpSocket::write`),
     `+30.1 RX 32 bytes ...`, `STATE <QAbstractSocket::SocketState>`, `ERROR <QAbstractSocket::SocketError> <errorString>`, `CLOSE by peer|by us (<reason>) after N rx / M tx bytes`, and `UNFRAMED n bytes left at close` when the parser holds a partial message.
     Full hex, no truncation; split the hex at 32 bytes per line under the header line so long blobs stay readable. A reconnect storm (the deck opening many sockets) gets one file per connection, as now.
   - **`.rx.bin` / `.tx.bin`** (rename of the existing `.bin` to `.rx.bin`, keeping the old name as a symlink or just updating `references/adj/tools/` users) so phase 3's script can replay each direction separately.
   - **Port 12523** (`DbServer::onQueryConnection`, the code at `db_server.cpp:92`): the same per-connection raw log (these sockets are short: a 1-byte-ish query and our answer; the deck's reaction to it is part of the link access error question).
   - **Parser view next to the raw view:** after each RX chunk, a line `PARSE consumed=<n> pending=<m> msgs=<k> state=<ok|error|need-more>` so a request that stays unparsed (`0x0001`?) is visible as pending bytes instead of silently waiting.
   - **A switch**, not always-on: `Debug > Log raw TCP of the dbserver` (default off) and `--link-tcp-raw`; checked at connect time. On top of that the Debug panel (category LINK) gets a **single summary line per socket event** (`DB-TCP: 169.254.231.210 RX 52 TX 33 ...`, hex cut at 24 bytes), so you can watch a live exchange without opening files.
   - Everything stays in the vdj-ui log directory (`~/.local/share/vdj-ui/linkexport/`), file names sort by time, and the raw logs are **not** deleted by the existing log housekeeping (check what that does).
5. **Counters.** `DbServer::stats_` already counts `unknownRequests`: show it with the NFS counters in the export's status line (`LinkExport` status text), e.g.
   `db: 14 unknown (last 0x2705)`.

### Tests

- `tests/test_db_server.cpp`: a client sends `0x2705` and `0x0001`; the server emits the new log line and the existing `unknownRequest` signal; the
  reply is unchanged (`EmptyOk`).
- `tests/test_link_export.cpp`: the status text includes the unknown counter.
- Raw TCP: against a real `QTcpSocket` client in `test_db_server.cpp`, with the switch on, a client sends `0x2705` and `0x0001` and half a message: the `.raw.log` contains the `RX` and `TX` hex lines **with the bytes of the actual reply**, the `PARSE ... pending=` line for the half message and `CLOSE by peer ... UNFRAMED`; with the switch off the file is not created. A port 12523 connection produces its own raw log.
- A small pure function for the discovery line (`formatDiscoveryLine(type, pid, known, ip, suppressed)`) with a unit test, so the formatting is testable without libcdj.

### Completion

```sh
geany-progress done 1 -r src/network/network_manager.cpp:131 -r src/dbserver/db_server.cpp:186 -r src/network/link_export.cpp -w "No behaviour change: replies to the deck are exactly as before; raw TCP logging is off unless switched on" -w "The discovery-socket replies we send from libcdj are still not logged (libcdj change not agreed)"
```

---

## Phase 2 — Decode the logged writes offline and write the protocol note

**Goal:** a document `references/adj/doc/link-media-writes.md` that says, for every type in the table above, what the arguments are, how they relate to the ANLZ/export data and
to the user's actions, and what is still unknown. A script for repeatability. No code changes in the app.

### Files

- **create** `references/adj/tools/dbserver-requests-summary.py` (stdlib only)
- **create** `references/adj/doc/link-media-writes.md`
- (reference reading) `src/dbserver/db_codec.{h,cpp}` (the message grammar: tags `0x10/0x11/0x14/0x26`, `describe()`), `src/dbserver/db_blobs.cpp` (how we build cue/waveform blobs),
  `references/adj/doc/tcp-protocol-notes.md`, `src/dbserver/rb_library.cpp` (what the ANLZ cue entries hold: `anlz_cue_t`)

### Work

1. The script parses `unknown-requests.log` (format: header line then `hex=` line) and every `dbserver-*.log` (`REQ`/`REP` lines), decodes each message with the same
   grammar as `db_codec.cpp` (4-byte magic `11 87 23 49 ae`, txid, type, argc, arg type list, args), and prints per request type: count, argument shapes, the distinct values of each
   argument, time deltas to the preceding request on the same connection, and the **reply we sent** (from the `REP` lines: they show the reply type and args).
2. Decode the **76-byte blob of `0x2705`** field by field. Compare with `anlz_cue_t` / a PCPT or PCP2 entry from the exported ANLZ files of the same track id (the exported library is the one the user was
   running; the track ids are in the log: `0x18f`, `0x40`, `0x9`, `0x142`, `0x790`, `0x794`). The first dword (`0x4c` = size 76), `01 00 02 00`... (type / hot-cue number?), `e8 03` (1000: a time base?), start and end
   in ms, colour, comment length. Mark every field as KNOWN / GUESS.
3. Decode the **900-byte blob of `0x2005`** and compare it with `waveformPreviewBlob()` for the same track (`db_blobs.cpp`): is it the preview waveform we served, or the deck's own re-analysed one?
   (The two decks that sent it: ask: is the track one the deck already knows? We know the track ids.)
4. For `0x2605`/`0x2804`/`0x3001`/`0x0001`: list what immediately precedes and follows each in the per-connection logs, so the likely trigger is visible (e.g. `0x2605 -38` right after `0x2102` for the same track id =
   auto-gain?). **Write down candidate meanings but do not implement anything yet.**
5. Record in the note: the set of deck models (the log's `model` or the keep-alive's name field), the player numbers, and that no capture exists of a real deck answering these.
6. Re-derive the `ip=?` question from a capture if one is at hand (`/var/tmp/prolink-broken-export-again.pcap` and the 4 boots captures): list UDP 50000 packets by (type, src ip, player number) and flag types above player 32. Add the
   result to the note under "Discovery traffic" (answers Q3 as far as the existing captures can).

### Completion

```sh
geany-progress done 2 -r references/adj/doc/link-media-writes.md -r references/adj/tools/dbserver-requests-summary.py -w "Meanings of 0x2705/0x3001/0x2005/0x2605/0x2804/0x0001 are inferences until phase 3's capture of a real deck answering them" -w "Phase 3 needs the human with two real decks"
```

---

## Phase 3 — USER STEP: capture a real deck answering these requests

**Goal:** ground truth for Q1/Q2. **Do not guess replies without it.** Sonnet prepares the tooling and instructions, then stops.

### Sonnet's preparation

- **create** `references/adj/tools/dbserver-pcap-requests.py`: stdlib-only. Reads a classic pcap (the `tcpdump -w` format, link type Ethernet or Linux cooked, see how
  `references/adj/tools/usb-state-summary.py` and `nfs-decode.py` read pcaps), reassembles TCP streams for ports 1051 and 12523 per direction, runs the phase 2 message decoder over each
  stream and prints a time-ordered conversation: `t  src->dst  REQ/REP  type  args`. Add `--nfs` to also print NFS RPC procedure numbers on UDP 2049 (reuse `nfs-decode.py` logic) and
  `--types 2705,3001,...` to filter. Test it on `pcaps/prolink-deck-to-deck-transfer.pcap` (known `0x2004` conversation).
- Make the script also accept the `.rx.bin`/`.tx.bin` files and `.raw.log` of phase 1 as an input (when the human has the raw TCP switch on, the dbserver's own view is a second source next to the pcap; compare them to spot bytes we never saw). Give the human this exact checklist (put it in the progress notes and print it in the reply):

  0. In vdj-ui turn on **Debug > Log raw TCP of the dbserver** before the vdj-ui run (step 3).
  1. Two real decks **A** and **B** on the link, a stick with analysed tracks in **B**. Capture on the PC with the link interface:
     `sudo tcpdump -i <iface> -n -w /var/tmp/prolink-edit-real.pcap` (the human runs it; unfiltered).
  2. **Reference, real to real:** on A, load a track from B's stick over LINK. Do these one at a time with 10 s of nothing between and **write down the clock time of each**:
     a) set a hot cue; b) delete it; c) save a memory cue; d) save a loop; e) change the beat grid (grid adjust, if the model has it); f) play the track for 20 s then eject it / load another (history);
     g) the "link access error" if the real deck shows one (it should not).
  3. Stop the capture. Repeat with a **fresh** capture `/var/tmp/prolink-edit-vdj.pcap` against **vdj-ui's exported virtual pendrive** (the same actions, with the times).
  4. Tell Sonnet the two filenames, the clock times and what each deck displayed (including any error text, which deck and when).

### Analysis (after the human returns)

- Run the script on both captures. Fill the "real deck replies" section of `link-media-writes.md`: for each action, the exact request and the **real reply** (type, args, blobs), including
  requests that now appear and that vdj-ui logged as unknown only because they were never seen before; and whether NFS procedures other than the read set ever appear (`--nfs`).
- Identify which request/reply pair the error coincides with in the vdj-ui capture (the first request in time order after which the deck's behaviour changes / retries / closes the TCP stream).
- If the real deck is silent on some request (no reply at all), that is the answer for that type: do not answer it.

### Completion (only after the human has confirmed the captures exist and the analysis is written)

```sh
geany-progress done 3 -r references/adj/doc/link-media-writes.md -r references/adj/tools/dbserver-pcap-requests.py -w "Replies in phase 4 must be copied from this capture, byte for byte where the data is not track-specific"
```

---

## Phase 4 — Answer the write requests the way a real deck does

**Goal:** the deck's edits to a track from our virtual pendrive no longer raise the link access error. Read-only data: accept and **discard** (or log), unless the human chose
otherwise after phase 3 (see the decision note below).

### Files

- **edit** `src/dbserver/rb_packets.h` / `db_codec.h` (request/reply type constants for the new types)
- **edit** `src/dbserver/db_handler.cpp` (`DbHandler::handle` switch, `db_handler.cpp:380-440`) and `db_handler.h`
- **edit** `src/dbserver/db_blobs.cpp` only if a reply needs a blob
- **edit** `tests/test_db_handler.cpp`, `tests/test_db_server.cpp`, `tests/test_link_export.cpp`
- **edit** `doc/link-export.md` (the "Known gaps" and a new "Edits from the decks" section)

### Changes

1. Add a `case` for each request type seen in phase 3 with the reply (or deliberate silence) the real deck gave. Reply construction reuses `reply(req, type, args)`; the txid is the request's. A track-specific
   field in the real reply (a count, a pointer-like "junk" field as in the waveform lesson, `doc/link-export.md` "What made the main waveform appear") is copied from the capture, not zeroed.
2. **No persistence.** The handler records the write in `Session` (`lastWrite{type, track, blob}`), emits a signal/log line `DB: <ip> would save <what> for track <id> (discarded: the export is read-only)`, and
   answers as the capture says. Unit tests send each captured request (hex from the capture) and compare our reply with the captured reply bytes.
3. `0x0001` (no args): once phase 2/3 say what it is, answer or ignore it as the real deck does; at the very least make sure the `unknown()` policy no longer sends a **menu-style `RepOk` to a request
   that is not a menu request** if the capture shows the real deck stays silent. Add a `UnknownPolicy` default for LinkExport (`Ignore`) only if the capture supports it, with a test.
4. Anything still not seen from a real deck is `// GUESS` and listed in `doc/link-export.md`.
5. Do not touch the NFS file service here.

**Decision note for the human (not blocking, record it in the progress notes):** applying the writes (hot cues, memory cues, loops) to the real export files is possible later: `src/trakcrate/cuemark_edit.*` and
`deck_edits.*` already write cue marks and grids into rekordbox exports from deck data (see `doc/trakcrate/` and `.planning/plans/deck-edits-copy-back_PLAN.md`). It is deliberately **out of scope** here
because it needs a file lock against the NFS reader and the ANLZ/PDB consistency work, and `.planning` has a separate plan for it.

### Tests

`ctest -R "dbhandler|dbserver|linkexport|integration"`; then the whole suite.

### USER STEP at the end

Retest on the real deck: the same actions as phase 3, against vdj-ui. Report: error gone? any deck message? Sonnet checks the LINK debug lines and `dbserver-*.log` for each action. Only then `geany-progress done 4`.

### Completion

```sh
geany-progress done 4 -r src/dbserver/db_handler.cpp -r tests/test_db_handler.cpp -r doc/link-export.md -w "Writes are acknowledged and discarded: the exported pendrive stays read-only" -w "Any reply field not in the capture is marked GUESS; re-capture if a deck model other than the tested ones shows the error"
```

---

## Phase 5 — Audit the NFS write path (libcdj) and add an NFS procedure test

**Goal:** if a deck ever does try an NFS write (history, ANLZ updates: not seen yet), we answer with the status a read-only server should, and we say so in the log. Fix the off-by-one labels.

### Files (libcdj is the human's repo, `references/libcdj`: agree before editing, the human installs)

- **edit** `references/libcdj/src/nfs/nfs_server.c` (`dispatch_nfs`, lines ~349-375)
- **edit** `references/libcdj/src/nfs/nfs_client.h` (procedure constants, lines 11-23)
- **edit** `references/libcdj/src/nfs/nfsd.c` (`w_unserved`: print the procedure **name**)
- **edit** `references/libcdj/src/test/nfsd_test.c`

### Changes

1. Fix `NFSPROC_*` to the NFSv2 numbers: `NULL 0, GETATTR 1, SETATTR 2, ROOT 3, LOOKUP 4, READLINK 5, READ 6, WRITECACHE 7, WRITE 8, CREATE 9, REMOVE 10, RENAME 11, LINK 12, SYMLINK 13,
   MKDIR 14, RMDIR 15, READDIR 16, STATFS 17`. Check every user of the old constants (`grep -rn "NFSPROC_" references/libcdj/src vdj-ui/src`); `NFSPROC_WRITE/CREATE/REMOVE/RENAME/MKDIR/RMDIR` in the
   **client** (`nfs_client.c`) must be checked against what the client really sends (the client only needs `NULL/GETATTR/LOOKUP/READ/READDIR/STATFS`; do not change behaviour that works).
2. `dispatch_nfs`: the write set is `SETATTR 2, WRITECACHE 7, WRITE 8, CREATE 9, REMOVE 10, RENAME 11, LINK 12, SYMLINK 13, MKDIR 14, RMDIR 15` -> `NFSERR_ROFS` (or the status phase 3's capture of a real deck
   refusing a write shows; if the capture shows none, keep `ROFS`); `ROOT 3` and `READLINK 5` -> a v2-correct reply (`READLINK` on a non-link: `NFSERR_INVAL` 22, not `ROFS`). Every one of them calls
   `ops->unserved` so the NFS window shows `nfs <PROCNAME> (not served, answered READ-ONLY FILESYSTEM)`.
3. `nfsd_test.c`: one datagram per procedure 0..17 against `nfsd`, asserting the accept status and the NFS status for each; plus the reply to a `WRITE` with a valid file handle.
4. Re-run `make nfsd-test` in libcdj, then vdj-ui `ctest -R "linkexport|integration"`.

### USER STEP

The human runs `sudo make install` in `references/libcdj` only if a `src/c` file changed (this phase only changes `src/nfs`, which vdj-ui compiles itself: **no install needed**), then rebuilds vdj-ui.

### Completion

```sh
geany-progress done 5 -r references/libcdj/src/nfs/nfs_server.c -r references/libcdj/src/nfs/nfs_client.h -r references/libcdj/src/test/nfsd_test.c -w "libcdj is the human's repo: edits there need their agreement" -w "No NFS write was ever seen from a deck so far: this phase is hardening, not the fix"
```

---

## Phase 6 — The DISCOVERY traffic and `ip=?`, and a final end-to-end check

**Goal:** Q3 answered, with a fix if our answers are wrong, and the whole thing verified on the decks.

### Files

- **edit** `src/network/network_manager.cpp` (`onDiscovery`, `knownMembers_`)
- possibly **edit** libcdj `src/c/vdj_discovery.c` (**needs the human's agreement and `sudo make install`**; prefer a vdj-ui side fix)
- **edit** `doc/link-export.md`, `doc/joining-the-link.md`

### Work

1. With phase 1's logging, collect a session (the human runs vdj-ui with decks, as in phase 4's retest): which packet **types** make the `ip=?` lines and from which player numbers.
   Expect one of: (a) a **mixer or device number above 32**: harmless, but `onDiscovery` ignores it (`link_members` table size 33): show it correctly, do not emit `deckJoined` for a mixer if that
   was happening, and log its address from the packet; (b) **ID-use / ID-set requests**: a deck is asking for a number: check that the numbers do not collide with ours (`VDJ_UI`'s player number) and that
   the `id_use_resp`/`id_set_resp` libcdj sends (the only replies from us) are only sent when the deck claims **our** number; a repeating request means the deck does not accept our answer: compare with
   `references/libcdj/src/c/cdj.c` packet builders and `doc/joining-the-link.md`; (c) **keep-alives of decks we have not upserted**: a bug in `vdj_new_link_member` (check it returns a member for every pid 1..32).
2. If (b) or (c) is the case and libcdj needs a change, write the fix as a patch description in the progress notes and ask the human to approve before editing libcdj.
3. Update the debug line, `doc/link-export.md` ("Logs and checks" and "Known gaps") and `doc/joining-the-link.md` with what was found.
4. **Final USER STEP:** the human repeats the phase 3 actions on the real decks against the exported virtual pendrive, with the discovery logging on, and confirms: no link access error, no repeating
   discovery lines, the NFS and LINK debug views show the deck's requests. If an error remains, return to phase 3 with a new capture of exactly that action.

### Completion

```sh
geany-progress done 6 -r src/network/network_manager.cpp:131 -r doc/link-export.md -w "If the error remains for one action only, capture that action alone (phase 3 method) before changing more code" -w "libcdj src/c edits, if any, need the human's sudo make install"
```
