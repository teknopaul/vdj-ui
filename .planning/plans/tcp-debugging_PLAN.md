# ProLink TCP protocol reverse engineering (port 1051)

Goal: understand the TCP traffic modern Pioneer decks (XDJ-700, XDJ-1000, CDJ-2000nexus, ...) exchange,
first from the existing pcap, then from live captures on a mirrored port, then by probing real decks from ADJ,
and finally document it and (only then) implement it in libcdj.

Source prompt: `ai-prompts/tcp-debugging.prompt.md` (including its "PC rekordbox analysis" section).

Two traffic families are investigated, in separate captures so they are not confused:
- **Deck <-> deck** TCP (player-to-player; the original question).
- **Deck <-> PC running rekordbox** (browsing a PC's library from the deck's screen). The deck UI differs from
  USB browsing, so this is probably a different protocol or a different use of the same one. Likely a rich source:
  the PC side is a real rekordbox, so it exercises browse/search/playlist/metadata features, not just track load.

## Ground rules (read before every phase)

- This is a **research** plan. Phases 1-4 never send anything new to real decks. Phase 5 does, and only
  after the human says go.
- Existing material (do not redo it, extend it):
  - `references/adj/doc/tcp-protocol-notes.md` - current notes (pcap analysis, message table, open questions)
  - `references/adj/doc/tcp-logging.md` - sample output of the ADJ logger on live decks
  - `references/adj/doc/LinkInfo-tcp.pcapng` (CDJ-2000nexus + DJM-2000nexus), also `LinkInfo.pcapng`, `LinkInfo2.pcapng`
  - `references/adj/tools/decode-tcp-frames.py`, `decode-tcp-pcap.sh` - existing decoders
  - `references/adj/src/adj_tcp_log.c/.h` - ADJ TCP logger (listens on 1051, 12523, 50003; connects to
    each discovered player on 1051; log in `/tmp/tcp-prolink.log`; enabled by a CLI option that sets `tcp_log_path`)
- `references/adj` and `references/libcdj` are symlinks to sibling projects. Edits there are plain local
  edits, **no git commands** (not a git repo here either; check `libcdj/CLAUDE.md`).
- `make install` of libcdj needs sudo; ask the human, do not try.
- Hardware: XDJ-700, XDJ-1000 (live), plus one PC running real rekordbox (with at least one deck for PC-mode tests). Port-mirror capable switch just purchased. `tcpdump` is installed;
  `tshark`/`dumpcap` are **not** - do not depend on them in scripts (Wireshark GUI is the human's tool).
  Python scripts must use only the stdlib (write a tiny pcapng reader if needed; check what
  `decode-tcp-frames.py` already does first).
- Keep scratch outputs in `references/adj/doc/tcp-captures/` (captures, human logs) and
  `references/adj/tools/` (scripts). Captures are binary: never `cat` them, summarise with scripts.
- Record every finding in `tcp-protocol-notes.md` with a **confidence** tag: `confirmed` (seen in >=2 captures /
  models), `likely`, `guess`.

## Key hypothesis to test first (from reading the notes)

The framing in the notes looks like the **dbserver / RemoteDB** wire format already documented by Deep Symmetry
(<https://djl-analysis.deepsymmetry.org/djl-analysis/track_metadata.html>), misparsed:

```
11 87 23 49 ae                magic (uint32 field, tag 0x11)
11 NN NN NN NN                transaction id (tag 0x11)            <- notes call this "player/session field"
10 TT TT                      message type (tag 0x10 = uint16)     <- notes: "type 1000 / 1040 / 1041 ..." are really
                                                                      tag 0x10 + type 0x0000 / 0x4000 / 0x4101 ...
0f AA                         argument count (tag 0x0f = uint8)    <- notes' "flags 000f" + "field count byte"
14 00 00 00 0c  <12 bytes>    arg-type blob (tag 0x14, len 12)     <- notes' "0x14 constant" + "06 06 02 ..." bytes
args...                       tag 0x11 uint32 / 0x26 string (UTF-16BE, NUL-terminated) / 0x14 blob
```

Under that reading `10 41 01` = 0x4101 menu item, `10 42 01` = 0x4201 menu footer, `10 40 01` = 0x4001 menu
header, `10 30 00` = 0x3000 render-menu request. Port 1051 would then be a (new?) dbserver-style endpoint
(perhaps what the CDJ-3000 / new XDJs use instead of or alongside the port discovered via UDP 12523 lookup),
and `djl-analysis` already names many message types and argument layouts. **Phase 1 proves or kills this.**
If it holds, most of the "reverse engineering" is mapping known dbserver messages and finding what is new.

### Second hypothesis: the PC-library path

Browsing a PC's library from a deck probably goes over TCP to the PC. Candidates to distinguish in the captures:
(a) the dbserver protocol on 12523 (as the nexus-era decks use for PC "rekordbox link"), (b) the 1051 protocol,
(c) something else (ports seen only in the PC capture; note the PC also serves audio/analysis files, possibly over
NFS/HTTP/SMB-like transfers, which the deck uses to *play* a PC track - identify those flows too). Phase 3 classifies every TCP/UDP flow
in the PC capture by port, direction (who connects) and framing (does `parse_fields` accept it?).

---

## Phase 1 - Re-parse the existing pcap with the dbserver framing hypothesis

Context to load: `tcp-protocol-notes.md`, `decode-tcp-frames.py`, `decode-tcp-pcap.sh`. Do **not** dump the pcap.

1. Read the two existing tools; reuse their pcap reading. Write `references/adj/tools/tcp-stream-dump.py`:
   - pcapng (and classic pcap) reader, stdlib only
   - reassemble each TCP flow (by 4-tuple, per direction, ordered by seq, tolerate retransmits)
   - for ports 1051 / 12523 / 50003 emit the reassembled byte stream, and a timestamped message list
2. Add a **tag-aware field parser** in the same script (`parse_fields(buf)`): tags `0x0f` u8, `0x10` u16, `0x11` u32,
   `0x14` blob(len u32 + bytes), `0x26` string (u32 char count + UTF-16BE incl. NUL). Parse a message as
   magic, txid, type, argcount, argtypes blob, args. Report any message where the parse does not consume exactly the
   stream (that is the signal the hypothesis is wrong for that message).
3. Run on `LinkInfo-tcp.pcapng` (and `LinkInfo2.pcapng` if it has TCP). Produce a table: message type (hex), direction,
   count, arg-type blob, arg count, and a one-line decoding of each distinct type.
4. Compare against the dbserver message types in djl-analysis (fetch the page with WebFetch; note only the
   type numbers/layouts relevant to what appears). State verdict: hypothesis **holds / partly / fails**.
5. Rewrite the "Message structure" and "Message types" sections of `tcp-protocol-notes.md` to the corrected framing;
   keep the old table under an "Earlier (superseded) reading" heading so history is not lost. Update the
   "Open questions" list. Check `adj_tcp_log.c` `decode_msg` / `compute_payload_len` against the new framing and
   list (do not yet fix) mismatches in the notes.
6. Use the `mdalign` skill on edited markdown tables.

Also make the script generic: `--ports` option and an "unknown stream" mode that prints the first N bytes of each
flow and whether it parses as tag-framed messages, since the PC capture may use ports not listed here.
Check whether `LinkInfo.pcapng` / `LinkInfo2.pcapng` already contain a rekordbox PC (look for non-player IPs
with TCP flows); if so, summarise them now as a head start.

Output: `tcp-stream-dump.py`, updated notes with a verdict, list of unexplained fields for the live capture to target.

```sh
geany-progress done 1 -r references/adj/tools/tcp-stream-dump.py -r references/adj/doc/tcp-protocol-notes.md -w "Verdict on the dbserver-framing hypothesis is the key result - check it before phase 2"
```

---

## Phase 2 - Live capture runbook and tooling (human-in-the-loop)

Context to load: notes (updated), `tcp-logging.md`. Claude writes the runbook and helper scripts; the **human**
performs the hardware steps.

1. Write `references/adj/doc/tcp-capture-runbook.md` covering:
   - Port mirroring: connect decks + switch + capture laptop; mirror the switch port(s) of the decks to the
     laptop port (note managed-switch config is switch-specific; ask the human for model if unclear). Mirror **both
     directions** (ingress+egress) of at least one deck so both sides of each TCP stream are visible.
   - Link-local network: decks use 169.254.x.x; the capture NIC needs no IP and should be in promiscuous mode.
   - Capture command: `sudo tcpdump -i IFACE -s 0 -w FILE.pcap 'tcp or udp port 50000 or udp port 50001 or udp port 50002'`
     (full snaplen, rotate with `-C 100`), plus a Wireshark alternative (pcapng).
   - **Verification steps**: (a) mirror works: `tcpdump -i IFACE -c 20 host DECK_IP` shows frames *not* addressed to the
     laptop; (b) both directions of a TCP stream visible (SYN and SYN-ACK from the same flow); (c) the file opens in
     Wireshark; (d) `tcp-stream-dump.py` reports reassembled streams with no gaps.
   - Importantly mention that capture must start **before** the decks are powered/linked (SYN visible) and that no ADJ/vdj-ui
     runs on the laptop during the *passive* sessions.
2. Write `references/adj/tools/tcp-capture-check.sh` implementing the verification checks above using tcpdump +
   `tcp-stream-dump.py` (no tshark). Exit non-zero with a clear message per failed check.
3. Write the **scenario script** the human follows, with a place to log wall-clock times. Create
   `references/adj/tools/tcp-scenario-log.sh` - a tiny interactive helper that prints each step, waits for Enter, and
   appends `HH:MM:SS.mmm step-name` to `tcp-captures/<session>.steps.txt`, so steps can be correlated to packets.
   Scenario (each step a separate, timestamped label):
   1. both decks off, capture running
   2. power deck A; wait for link (30 s)
   3. power deck B; wait for link (30 s)
   4. load track 1 on A (USB), then track 2 on A; load a track on B
   5. play/pause/cue on A (to see whether TCP carries anything realtime), play B
   6. change a track's tag/name-length case: load a track with a very long title if available
   7. make A master (SYNC/MASTER button); wait 20 s
   8. make B master; wait 20 s
   9. make A master again, then **power off A** (hard power-off, not eject); wait 60 s (observe TCP FIN/RST/keepalive and master takeover)
   10. power A back on; wait 30 s
   11. unplug the network cable of B for 30 s, replug (liveness/keepalive behaviour)
   12. stop capture
   Run the whole scenario once per hardware combination: XDJ-700+XDJ-1000, and (if possible) with only one deck.
4. Add `tcp-captures/README.md` describing naming: `YYYYMMDD-<decks>-<run>.pcap` + `.steps.txt` + human notes.
5. Do not run anything needing sudo; give the human the exact commands.

### PC rekordbox scenario (second capture series)

Set up: rekordbox PC + one deck (XDJ-700 or XDJ-1000) on the mirrored switch; mirror the **PC's port and the
deck's port** so both directions of the PC<->deck conversation are visible. Start capture before anything is powered/linked.
Document the rekordbox version, OS, and whether "LINK" / "Export" mode is used, in the session notes.
Steps (timestamp each with `tcp-scenario-log.sh`, which takes a scenario name argument):
1. capture running, rekordbox closed, deck on
2. start rekordbox, enable Link (wait 30 s) - does the PC appear on the deck?
3. on the deck: open the PC's library root (no further navigation), then wait 10 s
4. browse into one playlist; then into a second, larger playlist; then Artist / Album / Genre / Key views (one label each)
5. use the deck's search (type a known 3-letter string), then a long search string
6. sort the list (BPM / title) if the deck allows it
7. load a track from the PC onto the deck; wait for the waveform/analysis to appear; play 10 s; cue
8. load a second track while the first plays (are fetches pipelined?)
9. in rekordbox: edit a track's title to something very long, add a cue point; reload the track on the deck (is the change visible without re-export?)
10. add a new playlist / track in rekordbox while the deck is browsing (does the PC push updates?)
11. close rekordbox while the deck browses (TCP close behaviour); reopen
12. stop capture
Also run once with an empty rekordbox library (or a library with 1 playlist) to get the smallest possible conversation.

**Human checkpoint**: ask the human to run the verification checks and capture session 1, then continue with phase 3.

```sh
geany-progress done 2 -r references/adj/doc/tcp-capture-runbook.md -r references/adj/tools/tcp-capture-check.sh -w "Needs the human to do the port mirroring and capture before phase 3"
```

---

## Phase 3 - Offline analysis of live captures

Context to load: notes, `tcp-stream-dump.py`, the capture's `.steps.txt`. Work on **summaries**, not raw dumps
(a long capture can be huge; always pipe through the script and `head`).

1. Extend `tcp-stream-dump.py` with `--summary` and `--timeline`:
   - per flow: endpoints, port, SYN time, FIN/RST time and who sent it, bytes each way, whether TCP keepalive probes are
     seen (empty ACK with seq = next-1 at long intervals), TCP options (keepalive can't be seen directly: use probe timing)
   - per message type histogram per flow and per model (XDJ-700 vs XDJ-1000 vs 2000nexus; identify model from the UDP
     keep-alive/announce packets in the same capture)
   - `--timeline` merges `.steps.txt` labels into the message list so each step shows the messages seen during it
2. For each scenario step, answer in the notes (confidence-tagged):
   - Which message types appear at track load? Is the 0x4101-style item burst the metadata for the loaded track?
   - Does anything appear on play/pause/cue? (expect: nothing realtime)
   - Master handoff (steps 7-9): any TCP messages around the UDP master-change packets? Who initiates? What happens
     in the TCP flows when A is powered off (FIN vs RST vs silence + timeout)? Do the survivors declare themselves master?
   - Network-unplug step: how long until the TCP flow closes on the peer; is keepalive visible?
   - Long-title case: do strings exceed what the UDP/NFS/pdb paths carry?
   - Differences between XDJ-700 / XDJ-1000 / 2000nexus.
3. Build a **field table** per message type: arg index, tag, observed values, hypothesis (e.g. "track id", "slot", "BPM x100"),
   cross-checked against the same track's values in the rekordbox pdb (`libcdj` `pdb.h`, `references/libcdj`) and in UDP
   CDJ status packets. Where a value matches a pdb field, mark `confirmed`.
4. **PC rekordbox analysis** (separate section in the notes: "Deck <-> PC"):
   - classify all flows in the PC capture (port, initiator, framing accepted by `parse_fields`?, bytes each way)
   - state whether it is dbserver (12523), 1051, or something else; for dbserver-like flows map message types to the
     documented ones (menu requests: root menu, playlist, artist, album, genre, key, search, track metadata, waveform, cue/beat grid,
     art) and list undocumented ones
   - build a browse trace: deck UI action (from the steps file) -> request message -> reply size/shape; note paging
     (offset/limit args) and how large lists are fetched
   - how a PC track gets *played*: which flow carries the audio file, any analysis (ANLZ) files, and cover art; compare
     with the NFS path used for USB (`libcdj` nfs/`xdj-nfs`) - this matters directly for vdj-ui
   - how title changes / cue edits in rekordbox propagate; how PC disappearance is detected
   - decide whether PC-mode and deck-to-deck 1051 share framing (reuse one parser in libcdj) or not
5. Update the notes' model comparison table and open questions. Write a short "Next experiments" list: which
   unanswered questions need an active probe.

```sh
geany-progress done 3 -r references/adj/doc/tcp-protocol-notes.md -r references/adj/tools/tcp-stream-dump.py -w "Findings depend on the quality of the live capture; unanswered questions go to phase 4/5"
```

---

## Phase 4 - Make the ADJ logger a correct, safe passive-ish dumper

Context to load: `adj_tcp_log.c` (724 lines - read in two halves), `adj_tcp_log.h`, the call sites in `adj.c`/`adj_vdj.c`,
notes. Only needed for the "ADJ joins the network and drains the streams" part of the prompt.

1. Align the logger with the verified framing from phases 1/3: replace `compute_payload_len` / `decode_msg` guesses
   with the tag-aware parser (small C port of `parse_fields`), so message boundaries are always right, with a
   resync-or-raw-fallback when the stream does not parse (never drop bytes).
2. Add a **raw binary stream dump** per connection: `<log_dir>/tcp-<ip>-<port>-<dir>-<unixtime>.bin` containing the exact
   reassembled bytes plus a sidecar `.idx` of (timestamp, offset, length) per read. This is the ground truth for offline
   analysis and avoids relying on text hexdumps.
3. Make writes **explicit and bounded**: the logger currently sends handshake + SESSION_INIT/SUBSCRIBE/QUERY.
   Add a runtime option (e.g. `--tcp-passive` vs default replay) so the human can choose "only reply with what the
   pcap shows is required" or "listen only". Document which bytes are sent and why in the log header.
4. Log TCP socket events with timestamps: connect, accept, close (orderly/RST), `SO_KEEPALIVE` state, and enable
   `SO_KEEPALIVE` with short intervals on our side (config option) so the player's responses to probes show liveness
   support.
5. Behaviour on player disappearance: log time between UDP keep-alive timeout and TCP close.
6. Tests: unit-test the C parser against byte strings cut from the pcap (put fixtures in `references/adj/test`, follow its
   existing style - see the `snip` skill if the framework is unclear). No network in tests.
7. Build ADJ (`make` in `references/adj`); verify it compiles cleanly. The human runs it against the decks, compares the
   `.bin` with the tcpdump capture (should be byte-identical for the received direction).

```sh
geany-progress done 4 -r references/adj/src/adj_tcp_log.c -r references/adj/doc/tcp-logging.md -w "Changes what ADJ sends to real decks; review the handshake/probe bytes before running on hardware"
```

---

(If the PC protocol proves to be the same framing, extend the logger to also act as a client against the rekordbox
PC for the browse requests seen in the PC capture - read-only requests only - so phase 5 can reuse it.)

---

## Phase 5 - Active probing from vdj-ui (temporary code) - only on the human's go-ahead

Context to load: notes, `src/cdj/cdj_wrapper.cpp/.h`, `src/network/`, `references/libcdj` network headers.

Prerequisite: the notes have a field table with `confirmed` entries for session setup and the metadata query.

1. Add a clearly marked temporary module `src/debug/tcp_probe.cpp/.h` (+ CMake entry), enabled only via an explicit
   command-line flag, not part of normal startup.
2. It replays exactly the sequence proven safe in the captures (handshake, setup, one metadata query for a loaded
   track), and prints decoded replies, using the player id negotiated by libcdj.
3. PC-library experiment (read-only, safe against the real rekordbox PC): connect to the PC as a client with the
   negotiated player id and replay the captured root-menu / playlist-list / track-metadata requests; compare replies with
   what the deck received in the capture, then try pagination and a search. This is the least risky active test since
   a PC crash costs nothing on stage.
4. Deck experiments, each behind its own flag and each requiring human confirmation of a deck state first:
   a. replay and decode a track-metadata query for each loaded slot - compare with pdb data
   b. a title longer than UDP allows
   c. vary one query argument at a time (slot, track id, request type) to learn the argument meanings; **never send
      unseen message types to a deck that is playing to an audience** - test on idle decks
5. Watch for: deck reboots, hangs, display glitches. Stop at the first anomaly and report to the human.
6. Update notes with results; mark each field `confirmed` only after a deck answered as predicted.

```sh
geany-progress done 5 -r src/debug/tcp_probe.cpp -w "Sends packets to real hardware - only run on idle decks" -w "Temporary code, remove after libcdj implementation"
```

---

## Phase 6 - Document, then implement in libcdj

Only start when phase 5 results are confirmed (human decision). Context: notes, `libcdj/CLAUDE.md`, `libcdj/src/nfs/pdb.c`.

1. Write the protocol doc `libcdj/doc-internal/prolink-tcp-protocol.text` (next to `rekordbox-db-format.text`),
   from the notes: framing, tag types, handshake, message catalogue, sequences (setup, metadata, liveness / master
   behaviour), model differences, unknowns.
2. Implement in libcdj following the `pdb.c` style: small static helpers, no external deps, `//SNIP_` markers.
   Parser/encoder first (pure functions, unit tests from pcap fixtures in `src/test/fixtures/`), then the connection
   state machine (non-blocking, one thread or integrated into the existing event loop - decide after reading how
   libcdj does UDP).
3. Add to `OBJS` in the Makefile and ship headers as was done for `pdb.h`; tests via `make test` using only committed
   fixtures. Ask the human to run `sudo make install`.
4. Remove `src/debug/tcp_probe.*` from vdj-ui and consume the library API (e.g. long titles / richer track info in the UI).

```sh
geany-progress done 6 -r references/libcdj/doc-internal/prolink-tcp-protocol.text -w "Library API is new surface area - review header before vdj-ui depends on it"
```
