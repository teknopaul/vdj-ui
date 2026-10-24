# "Pretend to be a rekordbox PC" mode (dbserver + presence) in vdj-ui

Goal: a mode **inside the existing vdj-ui GUI** (a dock panel plus a `--rekordbox-mode` command-line shortcut) where the machine looks like a rekordbox PC (device 17) on the Pro DJ Link network and acts as a
**dbserver** that answers a real deck's library browsing from a local rekordbox export (pendrive/`PIONEER` folder made by
rekordbox PC, so the data is known valid). We then connect a real deck and observe what it asks for next:

- does it ask for the **audio over NFS** (portmap 111 / mountd / nfs 2049)?
- or does it send an **unknown request type** over TCP?

Whatever it asks, we then replay that request **backwards** to a real player (a deck with a USB stick) to see the
real answer, and document it. The deck is the oracle; we do not need to guess.

Why now: the user's rekordbox PC is too old to serve hardware (a deck sent 8 SYNs to port 12523, the PC never answered),
so a software server of our own is the only way to see this exchange.

## Ground rules (read before every phase)

- Background (read, don't re-derive): `references/libcdj/doc-internal/how-to-pose-as-rekordbox.md` (what a PC announces
  and what the decks do), `references/adj/doc/tcp-protocol-notes.md` (dbserver framing, captured request types).
  Protocol reference pages: <https://djl-analysis.deepsymmetry.org/djl-analysis/track_metadata.html> and `menus.html`.
  **Section "Protocol cheat sheet" below is a condensed copy so you do not have to refetch.**
- Directories: vdj-ui is `/home/teknopaul/bzr_workspace/vdj-ui`. libcdj is `references/libcdj`
  (symlink). **git: see Version control below; `make install` needs sudo - ask the human.** To avoid needing installs, vdj-ui compiles libcdj
  C sources directly (see how `src/CMakeLists.txt` builds `xdj-nfs` from `${CDJ_SOURCE_DIR}/src/nfs/*.c`). Do the same for
  `pdb.c`, `utf16.c` and `src/anlz/anlz.c`. Do not modify libcdj unless a phase says so; if it does, the change must not
  require an install for vdj-ui to build.
- New protocol code lives in `src/dbserver/` (new static lib `vdj-dbserver`, linked into `vdj-cdj` and so into `vdj-ui`). Keep the **core (codec, library model,
  request handlers, packet builders) free of Qt Widgets, and the codec/library/handlers free of Qt entirely**, so they can be unit tested without a GUI. Sockets use QtNetwork.
  GUI code is limited to **one new panel class** (`src/ui/rekordbox_mode_panel.h/.cpp`, Phase 7) plus a small hook in `src/ui/main_window.cpp` and `src/main.cpp`.
  The GUI work is going on in parallel: keep edits to shared files (`main_window.*`, `main.cpp`, both `CMakeLists.txt`) minimal and self-contained (a menu action, a dock, one
  CLI branch; CMake additions in a clearly marked `# rekordbox mode` block), and put all behaviour in the new files.
- **Version control:** the human does all git operations. **Do not run any git command in vdj-ui or libcdj** (no add, commit, stash, checkout, reset, etc.). Just edit files; other uncommitted GUI work may be present in the tree, so touch only the files this plan names.
- Only shared build files with GUI work: `src/CMakeLists.txt` and `tests/CMakeLists.txt`.
- Safety: this mode talks to real DJ hardware. Never send anything to a deck that was not seen from a real device or
  described in the docs, until Phase 9. All servers are read-only. The mode is **opt-in** (menu action or `--rekordbox-mode`, never automatic), and exclusive
  (it must not run next to the GUI's normal virtual CDJ connection or ADJ: same UDP ports; the panel enforces this).
- Build: `cd build && cmake .. && make -j` (existing build dirs `build`, `build-asan`, `build-debug`). Run tests with `ctest`.
- Record unknowns honestly. Any field we guess is marked `// GUESS` in code and listed in the final doc.

## Protocol cheat sheet (condensed from djl-analysis)

Field tags: `0f` u8, `10` u16, `11` u32, `14` blob (u32 length + bytes), `26` string (u32 char count + UTF-16BE incl. NUL).
Message: `11 872349ae` magic, `11 TxID`, `10 type`, `0f argcount`, `14 0000000c <12 arg-tag bytes>` (`02` string, `03` blob,
`06` u32), then args. **A blob arg is preceded by a number arg repeating its length; if that length is 0 the blob bytes are
omitted entirely.** Streams: boundaries from lengths, not packets (several messages per packet, one message across packets).

Connection: client connects to 12523, sends `00 00 00 0f "RemoteDBServer\0"` (19 bytes), server replies 2 bytes = big-endian port
(1051 = `04 1b`) and closes. Client connects to that port; both send `11 00 00 00 01`. Client setup: type `0000`, TxID
`fffffffe`, arg = requester device number D (must be 1-4, on the network). Reply `4000` (arg1 = `0000`, arg2 = **server's own
device number**, 17 for us). Disconnect: type `0100`, TxID `fffffffe`, no args. TxID counts 1.. and replies echo it.

DMST arg (u32, 4 bytes): D requester, M menu (01 main, 02 sub, 03 metadata preview, 08 graphical/binary), S slot (3 USB, 4
rekordbox collection), T track type (01 rekordbox).

Menu flow: request `1xxx` -> reply `4000` (arg1 echoes request type, arg2 = item count). Then client sends `3000`
(DMST, offset, limit, 0, total, 0) one or more times; reply = `4001` header, N x `4101` items, `4201` footer (no args).
`4101` has 12 args: 1 parent id, 2 main id, 3 len-bytes label1, 4 label1 (string), 5 len label2, 6 label2, 7 item type,
8 flags, 9 artwork id, 10 playlist position, 11 unknown, 12 unknown. Metadata request `2002` (DMST, track id) -> `4000`
count, then `3000` render returns items in order: Title `0004` (arg1 artist id, arg2 track id, arg9 artwork id),
Artist `0007` (arg2 artist id), Album `0002`, Duration `000b` (arg2 secs), Tempo `000d` (arg2 bpm*100), Comment `0023`,
Key `000f`, Rating `000a` (0-5), Color `0013`-`001b`, Genre `0006`, Date added `002e` ("yyyy-mm-dd").
(Exact arg usage per item: **mirror what real decks send; Phase 3 extracts those layouts from the captured replies.**)

Menu requests (args DMST, sort, [ids]): `1000` root (DMST, sort, `00ffffff`), `1001` genre, `1002` artist, `1003` album,
`1004` track (sort values: 00/01 title `0704`, 02 artist, 03 album `0204`, 04 bpm `0d04`, 05 rating, 06 genre, 07 comment,
08 time, 09 remixer, 0a label, 0b original artist, 0c key `0f04`, 0d bitrate, 10 play count, 11 date added `2e04`), `1006` bpm,
`1007` rating, `1008` year, `100a` label, `100d` color, `1010` time, `1011` bitrate, `1012` history, `1013` filename, `1014` key,
`1302` original artist, `1602` remixer; filtered: `1101` artists for genre, `1102` albums for artist, `1103` tracks for album,
`1107` tracks for rating, `1108` years for decade, `110a` artists for label, `110d` tracks for color, `1110` tracks for time,
`1112` tracks for history, `1114` keys distance, `1201` albums for genre+artist, `1202` tracks for artist+album,
`1206` tracks for bpm +/- %, `1208` decade+year, `120a`, `1214`, `1301`, `130a`, `1402`, `1502`, `1702`, `1802`;
`1105` playlist/folder (DMST, sort, id, 0 playlist / 1 folder; root id 0 folder 1); `1300` search (DMST, sort, string byte length,
uppercase UTF-16 string, 0); `2006` folder menu. Root menu replies list the menus (item types `0080`-`0095`, e.g. `0080` genre
menu... **take exact type codes from the captured root-menu reply**, not from memory). Item types: `0001` folder, `0002` album,
`0004` title, `0006` genre, `0007` artist, `0008` playlist, `000a` rating, `000b` duration, `000d` tempo, `000f` key,
`0010` bit rate, `0011` year, `000e` label, `0013` colour none, `0014`-`001b` colours, `0023` comment, `0024` history playlist,
`0028` original artist, `0029` remixer, `002e` date added; combined `0704` title+artist, `0204` title+album, `2304`, `2e04`.

Binary data (DMST M=08 unless stated; replies are `4xxx` with arg1 echo, arg2 0, arg3 length, arg4 blob omitted when 0):
`2003` art (DMST, artwork id [,1 for 240px]) -> `4002`; `2204` beat grid (DMST, track id) -> `4602`, entries 16 bytes (u16 LE beat in
bar, u16 LE bpm*100, u32 LE ms), entry data starts at blob offset 0x14; `2004` waveform preview (DMST, 3 or 4, track id, 0) -> `4402`,
900 bytes (400 x 2 bytes + 100 byte tiny); `2904` waveform detail (DMST M=01, track id, 0) -> `4a02`, 1 byte/segment, 150/s;
`2c04` analysis tag (DMST M=01, track id, reversed tag e.g. `PWV4`/`PWV5`/`PWV6`/`PWV7`/`PWVC`/`PSSI`, reversed ext `EXT`/`2EX`) -> `4f02`
blob starts at byte 34; `2104` basic cues (DMST, track id) -> `4702` (entry 0x24 bytes, numhot, numcue); `2b04` extended cues
(DMST, track id, 0) -> `4e02`. Seen from decks but **undocumented**: requests `3e03`, `3d03`, `2102`; reply `4b02`.

---

## Phase 1 - Wire codec and captured-stream fixtures (Qt-free)

Context to load: `references/adj/doc/tcp-protocol-notes.md` (Correction section), this plan's cheat sheet, `src/nfs/xdj_nfs.h` for style.

1. Create `src/dbserver/db_codec.h/.cpp`, namespace `vdj::db`:
   - `struct Field { enum Kind {U8,U16,U32,Blob,String}; uint32_t num; std::vector<uint8_t> blob; std::u16string str; }`
   - `struct Message { uint32_t txid; uint16_t type; std::vector<Arg> args; }` where `Arg` is number / string / blob.
   - `encode(const Message&) -> std::vector<uint8_t>`: writes magic, txid, type, argcount, the 12-byte arg-tag blob, args; for blob args
     writes the length-number arg and omits blob bytes when the length is 0 (check how the number+blob pair is represented: the
     length number is a separate arg preceding the blob).
   - `class StreamParser { void feed(const uint8_t*, size_t); bool next(Message&); bool failed() const; }` incremental, handles partial
     data and several messages per feed, handles the 5-byte greeting `11 00 00 00 01` as a distinct event, and sets `failed` with an
     offset on a framing error (never throws, never reads out of bounds, caps string/blob lengths at a sane limit, e.g. 16 MB).
   - `std::string describe(const Message&)` one-line hex/decoded dump for logs (strings decoded, blobs shown as length + first 16 bytes).
2. Extract fixtures with a throwaway script (put it in `tests/fixtures/dbserver/extract_fixtures.py`, stdlib only; you can reuse the TCP
   reassembly in `references/adj/tools/tcp-stream-dump.py` if Phase 1 of the tcp-debugging plan created it, otherwise write a small reader)
   from `/var/tmp/prolink-w-tcp.pcap` (flow `169.254.177.253:1054 -> 169.254.231.210:1051`): 
   - `client_stream.bin` (client -> server bytes, ~3 KB) and `server_stream.bin` (server -> client, ~76 KB), plus a 12523 exchange 
     (`port_query_req.bin` 19 bytes, `port_query_resp.bin` 2 bytes). If the pcap is gone, stop and ask the human.
3. Tests `tests/test_db_codec.cpp` (`vdj-ui-dbcodec-test`):
   - parse `client_stream.bin` completely: 27 messages, last one is the waveform preview request `2004` whose 5th arg is an omitted blob
     (this was the cause of an earlier mis-parse, keep it as a named regression test)
   - parse `server_stream.bin` completely: 209 messages; the type histogram matches (`4000` 27, `4001` 24, `4101` 124, `4201` 24, ...)
   - **round-trip**: `encode(parse(x))` is byte-identical to the original bytes for every message of both streams
   - feed byte-by-byte and in random chunk sizes gives the same messages
   - truncated / garbage input sets `failed` and never crashes (run the test under `build-asan` once)
4. Add `src/dbserver/` library target and the test to CMake (library name `vdj-dbserver`).

Output: codec + 100% round-trip over real captures. Everything later depends on this being exact.

```sh
geany-progress done 1 -r src/dbserver/db_codec.cpp -r tests/test_db_codec.cpp -r tests/fixtures/dbserver/ -w "Round-trip over captured streams is the correctness oracle; check the omitted-blob rule"
```

---

## Phase 2 - Library model from a rekordbox export (Qt-free)

Context to load: `references/libcdj/src/nfs/pdb.h` (all of it), `references/libcdj/src/anlz/anlz.h` (headers only), `src/ui/track_browser.cpp`
(how vdj-ui already loads a pdb and finds analysis files; reuse its approach, do not duplicate blindly), `libcdj/src/test/fixtures/` listing.

1. CMake: compile libcdj `pdb.c`, `utf16.c`, `anlz/anlz.c` into `vdj-dbserver` from `${CDJ_SOURCE_DIR}` (same pattern as `xdj-nfs`).
2. `src/dbserver/rb_library.h/.cpp`, class `RbLibrary`:
   - `bool load(const std::string& exportRoot, std::string* err)`; `exportRoot` contains `PIONEER/rekordbox/export.pdb` and
     `PIONEER/USBANLZ/...`; `Contents/` is where audio lives (record the root, serve nothing yet).
   - In-memory tables: tracks (`pdb_track_t` copied into std::string members), artists, albums, genres, labels, keys, colours, artwork,
     playlists (tree) and playlist entries. Index maps by id.
   - Query helpers that the handlers need, returning lightweight row structs: all tracks sorted by any sort in the cheat sheet; tracks
     by artist/album/genre/label/key/rating/colour; albums by artist; artists by genre; playlist/folder children; search by substring
     (case-insensitive over title/artist/album); history list (empty is fine).
   - `trackAnalysis(trackId)`: lazy-open `anlz_track_open` using the track's `analyze_path` and cache; accessors for beat grid, cues, waveforms
     (wrapping the libcdj calls; no format conversion yet). Return "absent" cleanly when the files are missing.
   - `artworkBlob(artworkId)`: read the file named by the pdb artwork row under the export root (a path like `/PIONEER/Artwork/...`);
     missing file -> empty.
3. Fixtures: copy `libcdj/src/test/fixtures/export.pdb` and a few of the committed `anlz/*` pairs that belong to tracks in it into
   `tests/fixtures/dbserver/export/PIONEER/...` mirroring the real layout (update a `MANIFEST.text` and `SHA256SUMS` like libcdj does). 
   If the fixture pdb's tracks do not point at the fixture ANLZ files, build the tree so at least one track resolves (document how);
   do not hand-edit binary files, write the fixture builder script instead.
4. Tests `tests/test_rb_library.cpp` (`vdj-ui-rblibrary-test`): load fixture export; counts match `pdb` truth (compute expected values with the existing
   libcdj parser in the test, not magic numbers where avoidable); each sort order is a total order; filter helpers return consistent ids;
   missing analysis returns "absent"; search finds a known title case-insensitively.

```sh
geany-progress done 2 -r src/dbserver/rb_library.cpp -r tests/test_rb_library.cpp -w "Reads real pdb/ANLZ through libcdj sources compiled into vdj-ui (no sudo install)"
```

---

## Phase 3 - Request handlers I: setup, menus, render, track metadata

Context to load: `src/dbserver/db_codec.h`, `src/dbserver/rb_library.h`, cheat sheet, and the **captured server stream** summarised by a script
(see step 1); do not read the 76 KB stream raw.

1. Write `tests/fixtures/dbserver/dump_stream.py` or a small C++ debug helper (`tools/dbdump` target using `describe()`) that prints, per reply
   message type, the arg layout seen in the real deck's replies (e.g. every `4101` item: args 1-12 with types, which values
   are constant). Use it to **fill the unknowns in the cheat sheet**: root menu item type codes and labels (`ARTIST`, `ALBUM`, `TRACK`, `KEY`,
   `PLAYLIST`, `HISTORY`, `SEARCH` wrapped in `U+FFFA...U+FFFB`), flags value (arg 8), args 11/12, label2 usage, what the `4000` count means for each
   request, playlist/folder item layouts. Save the findings as comments in the header and in `tests/fixtures/dbserver/REPLY_LAYOUTS.md`.
2. `src/dbserver/db_handler.h/.cpp`, class `DbHandler { std::vector<Message> handle(const Message& req, Session& s); }` with a per-connection `Session`
   (requester device number, pending menu result set: the list of items produced by the last menu/metadata request, which `3000` then pages through).
   Implement:
   - `0000` setup -> `4000(0000, our device number)`; `0100` disconnect -> close indication.
   - Every documented menu request in the cheat sheet that the library can answer: the 15 top-level menus with their sorts, filtered menus,
     `1105` playlists/folders, `1300` search, `2006` ignore-with-empty-result. Each stores its items in the session and replies `4000(type, count)`.
   - `3000` render: `4001` header, items `offset..offset+limit`, `4201` footer. Offsets beyond the count -> header + footer only. Limit > 64 -> clamp to 64? (docs say
     larger fails on real players; we are the server so just honour it, but log it).
   - `2002` track metadata: `4000(2002, 11)` then on `3000` the 11 items in documented order, filled from the pdb (title, artist, album, duration, tempo,
     comment, key, rating, colour, genre, date added). Unknown track id -> `4000(2002, 0xffffffff)`.
   - **Unknown request type:** do not guess. Log the whole message (`describe()`), keep a counter, and reply with a configurable policy 
     (`Policy::Ignore`, `Policy::EmptyOk` = `4000(type, 0)`, `Policy::Close`); default `EmptyOk`. The very point of the experiment is to see these.
3. Item type codes, flag values and the rest must come from step 1, not memory. Where the deck data does not cover a case, use the documented value and mark `// GUESS`.
4. Tests `tests/test_db_handler.cpp` (`vdj-ui-dbhandler-test`) using the fixture library:
   - setup reply has our number
   - root menu -> count equals the number of root items the deck sent (from the captured reply)
   - **structural oracle:** replay `client_stream.bin` messages that this phase handles (`0000`, `1000`, `1002`, `1003`, `1004`, `1014`, `1105`, `3000` paging)
     against the fixture library and assert that for every request the *reply message types, argument counts and argument tag types* equal those in
     `server_stream.bin` (content differs because the libraries differ, shape must not)
   - paging: concatenating `3000` pages with limit 3 gives the same items as one big page
   - metadata for a fixture track equals the pdb values; unknown id -> `ffffffff`
   - unknown request type -> logged and the configured policy applies

```sh
geany-progress done 3 -r src/dbserver/db_handler.cpp -r tests/test_db_handler.cpp -r tests/fixtures/dbserver/REPLY_LAYOUTS.md -w "Item layouts come from real deck replies; anything not covered is marked GUESS in code"
```

---

## Phase 4 - Request handlers II: art, waveforms, beat grid, cues

Context to load: `src/dbserver/db_handler.*`, `src/dbserver/rb_library.h`, `references/libcdj/src/anlz/anlz.h` (waveform and cue structs),
`references/libcdj/doc-internal/rekordbox-anlz-format.text` (only the sections for PWAV/PWV2/PWV4/PQTZ/PCOB), cheat sheet.

1. Binary reply helper: `4xxx` with args (echo type, 0, length, blob) and the omitted-blob-on-zero rule; reuse the codec.
2. `2003` album art: `4002`; unknown/missing art -> length 0.
3. `2204` beat grid: build the blob from `anlz_track_beatgrid`: 16-byte entries, little-endian (beat in bar, bpm*100, time ms), entries starting at 0x14;
   fill the 0x14-byte head with zeros unless the captured deck reply (from a deck serving a real track, if the pcap contains one) shows otherwise. Check:
   does the captured stream contain a `4602` reply? If so, copy its header bytes and mark the rest `// GUESS`.
4. `2004` waveform preview (900 bytes) and `2904`/`2c04` detail/colour tags: check which libcdj waveform kinds exist (`anlz_wave_kind_t`). The monochrome
   400x2 preview corresponds to the DAT waveform tag (PWAV) and the 100-byte tiny to PWV2; if libcdj does not expose PWAV, **add a minimal raw-tag accessor in the vdj-ui layer using
   `anlz_find_tag`** (do not change libcdj). For `2c04` return the raw EXT/2EX tag body for tags we have (PWV4/PWV5 if present), otherwise length 0.
5. `2104`/`2b04` cues: build basic (24-byte entries: loop flag, cue flag, hot cue slot, cue seconds LE, loop seconds LE) and extended entries from
   `anlz_track_cues`. Comments/colours are on hold (the user's decks do not support them): emit empty.
6. Captured stream contains `2004` (waveform preview), so assert its reply *shape* equals the deck's (`4402`, 4 args, length 900 blob) in the test.
7. Tests extend `test_db_handler.cpp`: for a fixture track with ANLZ data, parse the produced beat-grid blob back and compare to `anlz_track_beatgrid`; hot cue
   count equals libcdj's; waveform preview blob length 900; missing analysis -> zero-length replies, not errors.

```sh
geany-progress done 4 -r src/dbserver/db_handler.cpp -r tests/test_db_handler.cpp -w "Blob layouts for beat grid / cues are from docs; header bytes not seen from a real deck are GUESS"
```

---

## Phase 5 - TCP servers (12523 + 1051), logging, keep-alive

Context to load: `src/dbserver/db_codec.h`, `db_handler.h`, `src/nfs/nfs_mount_manager.*` (Qt object style), `src/session_log.h` (logging facility), `references/adj/src/adj_tcp_log.c`
(`conn_thread`, for the logging format the human is used to; do not copy its framing).

1. `src/dbserver/db_server.h/.cpp`, QObject with two `QTcpServer`s:
   - port-query server (default 12523): on connect read 19 bytes; if they match `00 00 00 0f RemoteDBServer\0` reply with the dbserver port as 2 bytes big-endian and close. 
     Anything else: log and close.
   - dbserver (default 1051): on connect send `11 00 00 00 01`, feed incoming bytes to `StreamParser`, send the greeting back if the client's greeting arrives, call
     `DbHandler::handle` per message, write replies in order. Framing error -> log hex around the error offset and close.
   - Ports are configurable (tests use ephemeral ports).
   - `SO_KEEPALIVE` with 10 s idle on accepted sockets (decks probe every 10 s; kernel answers anyway). Method `closeClientsFor(ip)` so the presence layer can close
     a connection ~10 s after a deck's UDP goes quiet (what a real deck does).
2. **Experiment logging** (this is what the whole plan is for): per connection, write a timestamped session log `<logdir>/dbserver-<ip>-<unixtime>.log` containing each received
   message (`describe()`), each reply, and the raw bytes of every receive in hex. Also append `<logdir>/unknown-requests.log` with one line per *unknown* request type
   (type, argcount, arg tags, full hex), so the answer to "what did the deck ask?" is one `grep` away. Also write the raw inbound byte stream to `.bin` per connection.
3. Signals for the UI: `clientConnected(QString ip)`, `clientClosed`, `unknownRequest(quint16 type, QByteArray raw)`, `requestHandled(...)`.
4. Integration test `tests/test_db_server.cpp` (`vdj-ui-dbserver-test`): start the server on ephemeral ports with the fixture library; an in-test client does the port
   query, connects, greets, sends setup + root menu + render + metadata + disconnect; replay of `client_stream.bin` end-to-end must produce a reply stream the codec parses fully
   with the expected type histogram; a request with an unknown type is logged to `unknown-requests.log` and answered per policy; two clients at once; half-sent message then
   close does not crash; very large `3000` limit works.

```sh
geany-progress done 5 -r src/dbserver/db_server.cpp -r tests/test_db_server.cpp -w "unknown-requests.log is the main experiment output; ports 12523/1051 need no root"
```

---

## Phase 6 - UDP presence as rekordbox device 17

Context to load: `references/libcdj/doc-internal/how-to-pose-as-rekordbox.md` (sections 2-5, 7), `references/libcdj/doc-internal/vdj-handshake-boot.md`,
`src/network/network_manager.h` (interface/IP handling), `src/network/auto_ip_manager.h`.

The packets to reproduce are in `/var/tmp/prolink-w-pc-rekordbox.pcap` (rekordbox PC at `169.254.51.135`). Extract the real packets as byte fixtures with a throwaway script and
build ours **by patching those templates** (MAC, IP, name, device number), not from scratch.

1. Fixtures (`tests/fixtures/dbserver/udp/`): first stage-1 claim (type `00`, 44 B), stage-2 claim (type `02`, 50 B), keep-alive (type `06`, 54 B), the name packet (type `11`, 296 B),
   media response (type `06` on 50002, 192 B), and the deck's media query (type `05`, 48 B, from the XDJ-700). Also the deck stage-3 claim (type `04`) from `prolink-w-boot-120s.pcap` as the
   template for stage 3, which the PC capture did not contain (do not skip stage 3; mark the shape `// GUESS` where only the deck's version is known).
2. `src/dbserver/rb_presence.h/.cpp` (Qt-free packet builders in `rb_packets.h/.cpp`, Qt socket/timer layer in `rb_presence`):
   - parameters: interface IP/MAC/broadcast, device number (default 17), device name (default `rekordbox`), computer name (default host name), slot-4 info (name, track count, playlist count from `RbLibrary`).
   - startup sequence: stage-1 x3, stage-2 x3, stage-3 x3 at the documented pacing (~0.3 s), then keep-alive every **2.0 s** on 50000 (broadcast), the name packet once (and again when a deck first appears),
     answer the deck's `05` query for device 17 / slot 4 with the `06` response (192 B) as a unicast to the asker's 50002.
   - listen (passively) on 50000/50001/50002 and **log every packet** type/source (Qt logging category) so unexpected deck behaviour is visible; keep counts per source/type.
   - collision handling: if another device claims 17 or defends it, log and pick 18 (configurable); do not spam.
   - track deck liveness: last-seen time per deck (from their keep-alives); expose `deckLost(ip)` after ~10 s of silence (see the 120 s capture: the survivor closed TCP about 9-10 s after the dead deck's last UDP); `DbServer::closeClientsFor` is connected to it.
   - Do **not** send `0x29` / `0x28` / status traffic in this phase. Add an option `--rb-status` stub only if trivial; the first experiment is about the dbserver and the media slot.
3. Tests `tests/test_rb_packets.cpp` (`vdj-ui-rbpackets-test`): every builder with the captured parameters reproduces the captured packet **byte for byte** (including name field, device number, type bytes
   0x21 = 03, 0x25 = 01); changing the IP/MAC only changes those bytes; media response for a library with N tracks encodes N at the documented offsets; the `05` query parser extracts asker IP, target device and slot.
4. Presence runs only in the opt-in mode (Phase 7). Do not start it from tests except for an in-process loopback test of the socket layer on unprivileged ports if feasible.

```sh
geany-progress done 6 -r src/dbserver/rb_packets.cpp -r src/dbserver/rb_presence.cpp -r tests/test_rb_packets.cpp -w "Stage-3 claim shape is only known from deck traffic; test on an isolated network first"
```

---

## Phase 7 - GUI panel in vdj-ui, interface selection, NFS observer

Context to load: `src/ui/main_window.h/.cpp` (menu construction around line 340-415, the `recordDock_` dock at ~1166 as the pattern to copy, and how `Connect`/`Disconnect` use `NetworkManager`),
`src/main.cpp`, `src/network/network_manager.h`, `auto_ip_manager.h`, `src/nfs/nfs_mount_manager.h`, `references/libcdj/src/nfs/rpc.h`, `xdr.h`,
`src/session_log.h`, `src/ui/recording_panel.h/.cpp` (a small dock widget to imitate), this plan's ground rules.

1. `src/ui/rekordbox_mode_panel.h/.cpp`, class `RekordboxModePanel : QWidget` (shown in a `QDockWidget`, same pattern as `recordDock_`):
   - Controls: export folder chooser (default: the folder last used by "Open Rekordbox Folder..." if `MainWindow` exposes it, else remembered via `ConfigManager`), interface combo
     (reuse the logic of `NetworkManager`/`AutoIpManager` for choosing the link-local interface/address; call the existing code, do not edit it), device number (default 17),
     display name (default host name), unknown-request policy (ignore / empty reply / close), log directory (default `~/.local/share/vdj-ui/rbmode`), Start/Stop button.
   - Live view (updated from `DbServer`/`RbPresence` signals, throttled): our IP/MAC/device number, decks seen (IP, name, last seen), TCP connections, request counters by type,
     a list of **unknown requests** (type, args, time; double-click shows full hex), NFS observer events, and a scrolling log. A "Copy unknown requests" button puts them on the clipboard.
   - Start: validates the export (loads `RbLibrary` in a worker thread, shows track count), starts `DbServer`, `RbPresence`, `NfsObserver`. Stop: stops them and closes sockets.
   - **Exclusive with the normal link connection.** If `NetworkManager::isConnected()` the Start button explains and refuses; while the mode runs, the Network -> Connect action is disabled
     (and re-enabled on stop). Also check at start that UDP 50000 is not already bound by someone else (e.g. ADJ) and say so.
   - Persist panel settings in `ConfigManager` (follow its existing key patterns).
2. Hook (minimal edits): in `main_window.cpp` add `Network -> Rekordbox PC Mode...` (checkable, toggles the dock) and create the dock lazily; in `main.cpp` add one branch
   `--rekordbox-mode <export-root>` (plus optional `--rb-iface`, `--rb-name`, `--rb-device`, `--rb-log-dir`, `--rb-unknown-policy ...`) that opens the dock, fills the fields and starts the mode after the window is shown
   (a call such as `window.startRekordboxMode(opts)` that just forwards to the panel). The argument parsing lives in `vdj-dbserver` (`RbModeOptions parseRbModeArgs(...)`) so it can be unit tested.
   With the flag, the GUI otherwise behaves normally (decks area stays empty because no virtual CDJ is connected).
3. Everything the panel shows must also be written to the log files from Phase 5, so a run without the GUI visible is still fully analysable.
4. **NFS observer.** The question is whether the deck asks for audio over NFS. Implement `src/dbserver/nfs_observer.h/.cpp`: UDP listeners on portmap 111 (if bindable;
   else log "needs root" and skip) and on configurable mountd/nfs ports, which only **decode and log** ONC-RPC calls (xid, program, version, procedure, first args e.g. the GETPORT program, MNT
   path, LOOKUP name, READ offset/count) using libcdj `rpc.c`/`xdr.c` (compile them in), answering **portmap GETPORT** with our mountd/nfs ports and replying to unknown programs with `PROG_UNAVAIL` so the deck does not hang. 
   It does **not** serve file data in this phase. TCP 111/2049 listeners also log and close. Report on stdout: "NFS observer: deck 169.254.x.y asked portmap GETPORT for MOUNT v1".
   Binding 111 needs root or `setcap cap_net_bind_service`; document the exact command, never run sudo yourself.
5. Docs: add `doc/rekordbox-mode.md` (usage, safety, how to read the logs, description of the panel) and a paragraph in `doc/vdj-ui.1`.
6. Tests: argument parsing unit test; a panel smoke test under `QT_QPA_PLATFORM=offscreen` (construct the panel with a fixture library, press Start with ephemeral ports through a test hook, assert the status labels, stop it) like `vdj-ui-integration-test`; NFS observer decodes a canned portmap GETPORT RPC call (build one with libcdj's xdr helpers) and replies correctly; observer never writes data replies for NFS READ.

```sh
geany-progress done 7 -r src/ui/rekordbox_mode_panel.cpp -r src/ui/main_window.cpp -r src/dbserver/nfs_observer.cpp -r doc/rekordbox-mode.md -w "Rekordbox mode is exclusive with the normal Network > Connect and with ADJ" -w "Small hooks in main_window.cpp/main.cpp are shared with ongoing GUI work: check for merge conflicts" -w "Portmap on 111 needs root or setcap; ask the human"
```

---

## Phase 8 - Live experiment with a real deck (human + analysis)

Context to load: `doc/rekordbox-mode.md`, `references/adj/doc/tcp-capture-runbook.md` (if the tcp-debugging plan created it), the logs from the run.

Human steps (Claude writes the runbook `references/adj/doc/rekordbox-mode-experiment.md`, human executes):
1. Network: one deck (XDJ-700 or XDJ-1000) and the laptop on the mirrored switch. **No other rekordbox/ADJ/vdj on the network. The old rekordbox PC is switched off.** Start `tcpdump -w /var/tmp/rb-mode-1.pcap` on the mirror port 
   before starting the mode. Start the mode: `vdj-ui --rekordbox-mode /var/tmp/rekordbox-example --rb-log-dir /var/tmp/rbmode-logs` (or Network > Rekordbox PC Mode...) (a real export from the user's pendrive).
2. On the deck: wait for the PC to appear in the source list ("DIRTBOX"-like name), open it, browse: root, a playlist, artists, albums, tracks, search, sort (one label per action, note times in `steps.txt` with `tcp-scenario-log.sh` if available),
   then **load a track** from it and press play.
3. Stop everything; collect the pcap and logs.

Analysis (Claude): produce a findings section in `references/adj/doc/tcp-protocol-notes.md` ("Deck <-> our PC server") answering, with evidence (log lines, pcap offsets):
   a. Did the deck connect to 12523 and 1051? Did setup succeed? Which browse requests were sent and did our replies render correctly on the deck (human reports what the screen showed)?
   b. Which requests were **unknown**: `unknown-requests.log` (decode their args against the DMST guess, any new `4xxx` reply codes expected).
   c. On loading a track: any NFS traffic (portmap GETPORT, MNT of which path, LOOKUP names, READ)? Any other TCP/UDP flow to our IP (list all flows to us in the pcap by port)? Any UDP packets from the deck that we have not seen before (types)?
   d. What path or id would be needed to serve the audio (e.g. the MNT export path, the file names looked up) and whether it matches pdb `file_path`.
   e. Liveness: with the deck powered off mid-session, when does it FIN, and does our `deckLost` close match.
   Decide the next step explicitly: (i) serve audio over NFS read-only from `Contents/` using libcdj's `nfs_server` (new plan), (ii) implement newly seen request types, or (iii) the deck never asks (explain why, e.g. needs a feature the pretend rekordbox does not offer).

```sh
geany-progress done 8 -r references/adj/doc/rekordbox-mode-experiment.md -r references/adj/doc/tcp-protocol-notes.md -w "Needs the human, a real deck and a mirrored switch; results decide Phase 9"
```

---

## Phase 9 - Replay backwards to a real player, compare, document

Context to load: Phase 8 findings, `src/dbserver/db_codec.h`, `db_server.h`, `references/libcdj/src/c/cdj_tcp.h`, the `Safety` ground rule.

1. Client tool `src/dbserver/db_client.h/.cpp` and a `Probe` section in the same panel and a CLI form `vdj-ui --db-probe <deck-ip> [--device N] [--send HEXFILE|--request ...]` (with `--no-gui` it prints to stdout and exits without showing the main window, otherwise the result appears in the panel): does the 12523 port query, connects to the returned port, greets, sends setup (requester number 1-4, **a number that is on the network and not the target**,
   see cheat sheet), then sends the requests given on the command line (documented names like `root-menu`, `track-metadata <id>` and the raw hex of an *unknown* request captured in Phase 8) and prints/decodes every reply with `describe()`. Always finishes with the disconnect message; global timeout; refuses to send more than a handful of unseen requests per run;
   prints "ONLY RUN ON AN IDLE DECK".
2. First, **self-validation against hardware**: run the same documented requests (root menu, artist/track menus, one track's metadata, waveform preview) against a real deck that has the **same pendrive** as our server's library, dump both replies, and diff them (`tools/db-diff` script): item order, flags, args 11/12, unknown fields, blob headers. Fix every difference in the handlers (Phase 3/4 code) and add the fixed cases as regression tests built from the real reply bytes (small excerpts only).
3. Then replay the **unknown request** found in Phase 8 to the deck that has the real USB and record the reply (a `4xxx` type, arg shapes). Add it to the handler with the real reply shape if our library can answer it; if it needs data we cannot produce, document it.
4. Update docs: `references/adj/doc/tcp-protocol-notes.md` (new request/reply types, now documented), `references/libcdj/doc-internal/how-to-pose-as-rekordbox.md` (what worked, what the deck required, final checklist), and write the first section of a libcdj dbserver design note (`references/libcdj/doc-internal/dbserver-notes.md`) listing what could move into libcdj. Do not implement libcdj code in this plan.
5. Tests for the client against our own `DbServer` on loopback (client talks to server, results equal the handler's own output).

```sh
geany-progress done 9 -r src/dbserver/db_client.cpp -r references/libcdj/doc-internal/how-to-pose-as-rekordbox.md -r references/adj/doc/tcp-protocol-notes.md -w "Sends requests to real hardware: only on an idle deck, only requests seen from a real device or documented" -w "libcdj implementation is a separate future plan"
```
