# Switching virtual pendrives at runtime (USB STOP / unmount / re-insert emulation)

Source prompt: `ai-prompts/switching-virtual-pendrivves.prompt.md`.

Goal: vdj-ui, acting as a LINK player that exports a rekordbox folder (`src/network/link_export.*`, slot 3), can
**change which folder/pendrive it exports while running**, the way a real deck lets you press USB STOP, pull the stick
and insert another. Plus a UI to manage a list of "virtual pendrives" (named folders), list physical sticks under
`/media/<user>`, load one into the existing track browser pane, and choose which one to export (or export an ad-hoc
folder with a default name). Existing File-menu items stay.

## Ground rules (read before every phase)

- **Research first.** The status-byte sequence for unmount/refusal is known from `prolink-usb-extracts.pcap`; what a remote deck does when a stick is removed while browsing/loading or yanked in use is not known, and is deliberately deferred to the anomaly-testing Phase 8. (Decks do not show stick names, so there is no name message to find.)
  Phases 1-3 produce captures and a protocol note; **no protocol is implemented before Phase 3's note exists**.
  Anything not seen in a capture is marked `// GUESS` in code and listed in the note.
- **Human steps are marked `USER STEP`.** Sonnet must stop and tell the human exactly what to do. The phase is not "done"
  (do not run `geany-progress done`) until the human confirms.
- **A first capture already exists:** `pcaps/prolink-usb-extracts.pcap` (3.4 MB) contains both unsuccessful extracts (stick in use) and successful
  ones. It has no timestamp notes, so Phase 1/2 must identify the extract events from the packets themselves. Extra captures (Phase 1 table) are
  only requested for gaps that this one leaves open.
- Safety: never send anything to a real deck that was not seen from a real deck. All servers read-only.
- Version control: the human does git. Do not run git commands. The tree has uncommitted work (`vdj-ui.geany`), touch only files a phase names.
- Keep core logic (registry, scanner, packet builders) free of Qt Widgets so it is unit-testable (`tests/`, see `tests/test_link_export.cpp`,
  `tests/test_rb_packets.cpp` for style). GUI work stays in new files plus small hooks in `src/ui/main_window.cpp`.
- Shared build files with parallel GUI work: `src/CMakeLists.txt`, `tests/CMakeLists.txt` - minimal, self-contained edits.
- libcdj (`references/libcdj`, symlink) changes need agreement with the human first, and `make install` needs sudo (ask). Prefer
  keeping new code in vdj-ui.
- Build: `cd build && cmake .. && make -j`; tests: `ctest`. Captures are binary: never `cat` them, summarise with scripts.
  `tcpdump` is installed; `tshark`/`dumpcap` are not (stdlib-only Python; reuse `references/adj/tools/decode-tcp-frames.py`).
- Existing material: `pcaps/prolink-deck-to-deck-transfer.pcap` (deck serving a stick, the model for LinkExport),
  `pcaps/prolink-vdj-ui-as-link-exporter.pcap`, `references/libcdj/doc-internal/how-to-pose-as-rekordbox.md`,
  `references/adj/doc/tcp-protocol-notes.md`, `src/dbserver/rb_packets.h` (`MediaInfo`, `mediaResponse`).

## Hypotheses to confirm or kill with the captures

1. **CONFIRMED by `pcaps/prolink-usb-extracts.pcap`** (see `references/adj/doc/usb-switching-protocol.md`): USB state is advertised in the **CDJ status packets
   (UDP 50002)**: byte 0x6f 00 loaded / 02 unloading / 03 removable / 04 empty, byte 0x77 media-present. A refused STOP is 00 -> 02 -> 00 within ~1 s with
   nothing else; a successful one is 02 (~1.5 s) -> 03 (~0.2 s) -> 04, and re-insert is 0x77 -> 1, then ~1 s later 0x6f -> 00, after which the remote
   deck sends a media query and opens TCP 12523/1051. There is no unsolicited media broadcast at removal or insertion.
2. **The physical decks do not display the stick name** (human, 2026-10-25). The name is therefore not a protocol concern: nothing needs to be found or mocked
   on the wire. `MediaInfo.name` in the media response may stay as it is (the real deck's was empty). Stick/volume names exist only as **vdj-ui UI labels**
   for the virtual-pendrive list and export selector.
3. **Deferred to Phase 8 (anomaly testing):** a stick physically pulled while in use on both decks. Human experience: the decks go into an "emergency loop" (loop 4 bars) - unknown whether anything about that is communicated over the network (status-packet flags, loop fields, TCP resets, NFS errors). Also browsing at the moment of STOP, and track-id reuse across sticks.

---

## Phase 1 - Summarising tools, first look at the existing capture, gap-filling runbook

Context to load: this plan's ground rules, `references/adj/tools/decode-tcp-frames.py`, `decode-tcp-pcap.sh`, `src/dbserver/rb_packets.h`.

Sonnet writes (nothing else):

1. `references/adj/doc/usb-switch-capture-runbook.md`: a step-by-step, copy-paste runbook for the human (below), including the exact
   `tcpdump` command (`sudo tcpdump -i <iface> -s 0 -w pcaps/<name>.pcap 'not port 22'`; on the port-mirror interface, or on the PC if the
   decks are on a hub/the mirrored port), how to mark moments in time (human notes wall-clock times of each button press into
   `pcaps/<name>.txt` - runbook provides a one-line `date +%T` habit), and a checklist to tick.
2. `references/adj/tools/usb-state-summary.py` (stdlib only): given a pcap/pcapng, print a time-ordered table of
   - CDJ status packets (UDP 50002, type 0x0a) per source device: the USB/SD state bytes, play state, loaded-player/slot/track fields,
     printing a row **only when one of those changes**;
   - media query (type 0x05) and media response (type 0x06) packets: src, dst, slot, decoded name/track count/dates/sizes;
   - TCP SYN/FIN/RST on 1051, 12523, 111, 2049 and mountd ports, per 4-tuple (shows sessions dropping when a stick is removed);
   - any UDP 50002 packet type not already known (hexdump first 64 bytes once per type).
   Reuse the pcap reader from the existing tools; do not duplicate it if it can be imported.
3. Run the script on `pcaps/prolink-usb-extracts.pcap` (and `prolink-deck-to-deck-transfer.pcap` as the "normal" baseline). Produce a first
   timeline in `references/adj/doc/usb-switching-protocol.md` (draft): the moments where a USB state byte changes, which devices, and a proposed split
   into "refused" vs "succeeded" extracts. Ask the human in one short list to **confirm that classification** and give any known times/decks
   (or say which events they remember) - do not guess silently.
4. From the timeline, decide which of the captures below are still needed. Mark the runbook table rows `NEEDED` / `COVERED by prolink-usb-extracts.pcap`.
   Typical gap: re-insert with a different stick (track ids repeating). The first capture already covers the status-byte sequence, refusal, and one remove/re-insert. Browsing-at-STOP and yanked-stick cases are NOT needed now: they belong to Phase 8.

### USER STEP - only the missing captures (the runbook must say this plainly)

Hardware: XDJ-700 and XDJ-1000 (the two live decks), two real USB sticks with different names (A "STICK-A", B "STICK-B"; rename one if needed),
port mirror or a PC on the same switch. Decks in LINK mode. Name each file below; start the capture before the first action and stop after the last.

| File | Scenario |
|------|----------|
| `pcaps/usb-stop-refused.pcap` | Stick A in deck 1. Deck 2 browses deck 1's USB and **loads and plays** a track from it. On deck 1 press USB STOP. (Expect refusal.) Wait 10 s. Stop deck 2, then try USB STOP again if it works, note what the deck did. |
| `pcaps/usb-stop-ok.pcap` | Stick A in deck 1, **idle**: nobody plays or browses it (deck 2 may show the browse list but loads nothing). Press USB STOP, wait for the "can remove" indication, pull stick A, wait 10 s, insert stick B, wait for it to mount, wait 10 s. |
| `pcaps/usb-swap-different-stick.pcap` | Stick A, then stick B (**different track counts and track ids**) in deck 1; on deck 2 open the deck-1 USB and browse before and after the swap. Note whether deck 2 shows stale content or reloads cleanly. (Decks do not show stick names - do not look for that.) |
| `pcaps/usb-stop-ok-empty.pcap` (optional) | Idle deck 1: USB STOP, pull stick, leave it empty 30 s, deck 2 looks at deck 1's USB, then re-insert the same stick. |

Notes the human also writes (`pcaps/usb-switch-notes.txt`): wall-clock time of every button press/insert/removal, what each deck's screen showed
(including any error text on refusal), deck firmware versions.

Phase done criteria: runbook and script exist, the existing capture has a first timeline and the human has confirmed the classification. **If any captures are marked NEEDED, STOP and ask the human to make them; resume at Phase 2 only when they say the pcaps are in `pcaps/`. If none are needed, go straight on.**

```sh
geany-progress done 1 -r references/adj/doc/usb-switch-capture-runbook.md -r references/adj/tools/usb-state-summary.py -w "Classification of refused/succeeded extracts in prolink-usb-extracts.pcap needs the human's confirmation" -w "Phase 2 needs any NEEDED extra captures in pcaps/ first"
```

---

## Phase 2 - Analyse the captures

Context to load: `pcaps/prolink-usb-extracts.pcap` and any new pcaps (via the summary script only), `usb-switch-notes.txt`, `rb_packets.h/.cpp` media parts, runbook.

1. Run `usb-state-summary.py` on each capture; extend it where output is unclear (diff mode between two status packets is likely useful).
2. Answer, with evidence (packet timestamps, hex of the changed bytes) in a new note `references/adj/doc/usb-switching-protocol.md`, each tagged
   `confirmed` / `likely` / `guess`:
   - Which status-packet bytes change on USB STOP, unmount-complete, empty, re-insert; the values and the timing relative to the button press.
   - What a deck does in the refusal case: no change in packets? a changed byte? a message to the asking deck? (compare to the `ok` case).
   - How the **remote deck decides the stick is gone**: what it sends/does (media re-query, TCP resets, NFS unmount, etc.) and whether our exporter has to
     tear down things (dbserver sessions, NFS export) or just answer differently.
   - What the remote does after re-insert: does it re-query media on its own, or does the exporter have to announce? Does the dbserver need a cache flush
     (track ids repeat across different sticks)?
   - Whether the player's own "what's loaded from player N slot 3" fields in the remote deck's status packets change.
3. Unknowns that remain get listed under "Open questions" with a proposed extra capture if needed (then ask the human).

```sh
geany-progress done 2 -r references/adj/doc/usb-switching-protocol.md -w "Confidence tags matter: anything 'guess' becomes // GUESS in Phase 3 code" -w "If a key behaviour is unknown, ask the human for an extra capture before Phase 3"
```

---

## Phase 3 - Protocol messages and runtime folder switching in LinkExport (no GUI)

Context to load: `usb-switching-protocol.md`, `src/network/link_export.h/.cpp`, `src/network/network_manager.h/.cpp` (link export start/stop and status
sending), `src/dbserver/rb_packets.*`, `src/dbserver/rb_library.h`, `src/dbserver/db_server.h`, `tests/test_link_export.cpp`.

1. Model the exporter's media state: `Mounted(folder, name)`, `Unmounting`, `Empty`. Implement the state byte(s) found in Phase 2 in the status packets
   the virtual player sends (find where NetworkManager/libcdj builds them; if libcdj must change, **stop and propose the design to the human first**).
2. Add to `LinkExport`:
   - `bool requestUnmount(QString* why)`: refuses (returns false, `why` = "in use by player N") when any remote is actively using the stick - define "in use" from
     the Phase 2 evidence (open NFS reads in the last N s, an active dbserver session with a loaded track, a remote status packet saying it loaded from us). The
     NFS server (`nfsd_*`) needs a "recently read" query: add the smallest accessor to our nfsd usage in `src/nfs`/`nfsd` wrappers (if `nfsd` is libcdj code, ask first).
   - `bool mount(const QString& folder, const QString& displayName, QString* error)`: loads the new `RbLibrary`, swaps the NFS export root and dbserver library
     atomically (no half-switched state), flushes caches, announces per Phase 2.
   - `void eject()`: go to `Empty` (answers media query as empty, sessions closed as the real deck does).
   - Display name: a vdj-ui label only (UI list, status bar, logs): explicit name if given, else the folder/volume name, else a default like `VDJ-USB`. Physical decks show no stick name, so it is never required on the wire; keep the media-response `name` as it is today.
   - Signals: `stateChanged(...)`, plus log lines in the existing `logLine` style.
3. `MediaInfo`/`mediaResponse` changes in `rb_packets` as needed (e.g. the empty-slot reply).
4. Tests (no hardware): extend `tests/test_link_export.cpp` / `test_rb_packets.cpp`: refusal when "in use"; mount A -> unmount -> mount B changes the
   library, media response name/count, status byte sequence; empty state; failed mount (no `PIONEER` dir) leaves the old stick mounted. Where possible assert against
   bytes taken from the captures (fixtures under `tests/fixtures`, small hex excerpts only).
5. Add a hidden debug trigger for hardware tests (command-line `--link-export-swap A B` or a signal on a debug action - pick the smaller one) so the human can try it
   before any GUI exists.

```sh
geany-progress done 3 -r src/network/link_export.h -r src/network/link_export.cpp -r tests/test_link_export.cpp -w "Status-byte sequence is from captures; check the // GUESS markers" -w "Not yet tried on real decks - Phase 4 does that"
```

---

## Phase 4 - Verify on real decks (USER STEP) and fix

Context to load: Phase 3 diff summary, `usb-switching-protocol.md`.

USER STEP: build (`cd build && make -j`), run vdj-ui exporting folder A with the debug trigger from Phase 3, with deck(s) in LINK mode, and
capture to `pcaps/vdj-ui-usb-swap.pcap` using the same method as Phase 1 with this sequence: deck 2 browses vdj-ui's USB (shows folder A's content) ->
trigger unmount while idle (expect deck 2 to drop it) -> mount folder B (deck 2 shows B's tracks, no stale A content) -> deck 2 loads a track from B and plays -> trigger
unmount (expect refusal). Human notes what the decks' screens showed.

Sonnet: compare `vdj-ui-usb-swap.pcap` against `usb-stop-ok.pcap` / `usb-stop-refused.pcap` with the summary script, list differences, fix the
implementation, add the corrected bytes to tests, and update the protocol note's "Verified on hardware" section. Iterate with the human until the decks behave as with a real stick
(if they cannot be satisfied, record the residual difference in the note and carry on; the UI phases do not depend on perfection).

```sh
geany-progress done 4 -r references/adj/doc/usb-switching-protocol.md -r src/network/link_export.cpp -w "Requires hardware session with the human; record any deck behaviour that still differs from a real stick"
```

---

## Phase 5 - Virtual pendrive registry and physical stick scanner (no GUI)

Context to load: `src/config/config_manager.h/.cpp` and `tests/test_config.cpp` (how config is stored), `src/ui/pendrive_clone_dialog.*` (existing
pendrive/UI patterns), `src/ui/track_browser.h`, `src/nfs/nfs_mount_manager.h`.

1. New `src/export/pendrive_registry.h/.cpp` (Qt Core only):
   - `struct VirtualPendrive { QString id; QString name; QString folder; }`; list persisted via `ConfigManager` (new section; follow existing style). Add / remove /
     rename / reorder; reject duplicates and folders without `PIONEER/rekordbox/export.pdb` (return a reason; still allow adding a folder that does not exist yet? **no** - validate).
   - `struct PhysicalPendrive { QString name; QString mountPoint; bool hasRekordbox; qint64 total, free; }` and `QList<PhysicalPendrive> scanPhysical()` iterating
     `/media/<username>/*` (and honouring `QStorageInfo::mountedVolumes()` removable filter as a fallback), with `scanPhysicalIn(root)` taking a directory so tests use temp dirs.
   - `QFileSystemWatcher` (or a 3 s timer) wrapper that emits `physicalChanged()` when sticks are plugged/removed.
   - Helper `describe(folder)` returning name, track count (cheap check, no full load), `hasRekordbox`.
2. Tests (`tests/test_pendrive_registry.cpp`, add to `tests/CMakeLists.txt`): add/remove/rename/persist round trip, duplicate and invalid-folder rejection, scan of a fake
   `/media/user` tree with and without rekordbox folders, default name rules.

```sh
geany-progress done 5 -r src/export/pendrive_registry.h -r tests/test_pendrive_registry.cpp -w "Config format is new - check key names before users start saving lists"
```

---

## Phase 6 - UI: manage the list, load into the viewer pane, choose what to export

Context to load: `src/ui/main_window.h/.cpp` (menus at ~370-440, `applyLinkExport` ~860, `openRekordboxFolder`), `src/ui/track_browser.h`,
`src/ui/rekordbox_mode_panel.*` (an existing dock for a similar feature), `src/ui/pendrive_clone_dialog.*`, `pendrive_registry.h`, `LinkExport` API from Phase 3.

1. New `src/ui/virtual_pendrives_dialog.h/.cpp`: lists **Virtual** (user list) and **Physical** (scan, live-updating) pendrives with columns name / path / tracks / status
   (exported now, mounted). Buttons: Add folder... (file dialog + name prompt), Remove, Rename, **Open in Browser** (loads it into the existing drive viewer pane via
   `TrackBrowser::openRekordboxFolder`), **Export** (mount it on the LinkExport), **Eject** (unmount; shows the refusal reason from `requestUnmount` in a message, "in use by player N").
   Double-click = open in browser.
2. Export selector: a combo/menu on the Network menu (and in the dialog) "Exported pendrive:" listing `None`, virtual ones, physical ones, `Browser's current
   database` and "Other folder..." (ad hoc export with default name `VDJ-USB`, or the folder name; no registry entry required). Selecting one while exporting calls
   `unmount` then `mount` (or refuses with the reason). If not yet exporting, it just sets the pending selection and enables export as `setLinkExportEnabled` does.
3. Keep the existing menu items: File > Open Rekordbox Folder, "Export the Loaded Database as a LINK Player", `--link-export DIR`. The checkbox keeps working as "export what
   the browser shows"; document in its tooltip how it relates to the new selector. Reflect the selector state in the status bar/`PlayerPanel` where the export status is already shown.
4. Remember the last exported pendrive in config; do **not** auto-export on startup unless the existing behaviour already does.
5. Tests: widget-free logic only (selector model: choices list, ad-hoc naming, refusal path) in `tests/test_pendrive_registry.cpp`/`test_integration.cpp`; the dialog gets a smoke test if `tests/test_rb_panel.cpp` shows a pattern for it (offscreen).

```sh
geany-progress done 6 -r src/ui/virtual_pendrives_dialog.cpp -r src/ui/main_window.cpp -w "Exported-pendrive switching must be re-checked on real decks (Phase 7, anomalies Phase 8)" -w "File menu items intentionally unchanged"
```

---

## Phase 7 - End-to-end hardware check (USER STEP), docs and cleanup

Context to load: `README.md` (feature section), `usb-switching-protocol.md`, Phase 4 pcap notes.

USER STEP: with the finished UI, human exports virtual pendrive 1 to deck 2, browses it, ejects (idle) and switches to a second virtual pendrive, then to a physical
stick plugged into the PC, then an ad-hoc folder, then tries Eject while deck 2 plays from it. Optional capture `pcaps/vdj-ui-usb-swap-ui.pcap` for regression.
Human reports anything odd; Sonnet fixes.

Sonnet:
1. Update `README.md` (virtual pendrives, switching, how to refuse/eject) and finalize `usb-switching-protocol.md`.
2. Remove the temporary debug trigger from Phase 3 if the GUI supersedes it (or keep as `--link-export-swap` documented, human's call).
3. Run the full test suite (`ctest`) and report results faithfully.

```sh
geany-progress done 7 -r README.md -r references/adj/doc/usb-switching-protocol.md -w "Report any failing tests honestly; hardware results come from the human"
```

---

## Phase 8 - Anomaly testing: stick pulled while in use, browsing at STOP (USER STEP, last)

Context to load: `usb-switching-protocol.md`, `references/adj/tools/usb-state-summary.py`, the final LinkExport (`src/network/link_export.*`), Phase 4/7 pcap notes.

Purpose: learn how real decks signal abnormal removal, and decide whether vdj-ui should emulate it (e.g. a "yank" button for testing remote decks) or at least survive it.
Nothing here blocks the earlier phases.

USER STEP - captures (human performs, Sonnet writes the checklist into the runbook first; each file follows the Phase 1 conventions and notes file):

| File | Scenario |
|------|----------|
| `pcaps/usb-yank-in-use-both.pcap` | Stick in deck 1, both decks have a track from it loaded and **playing**. Physically pull the stick without USB STOP. Note on both decks when the "emergency loop" (4-bar loop) starts, what the screens say, how they recover after re-insert / after pressing eject. |
| `pcaps/usb-yank-in-use-one.pcap` | Same, but only deck 2 plays from deck 1's stick (deck 1 plays something else/nothing). |
| `pcaps/usb-stop-while-browsing.pcap` | Deck 2 has deck 1's USB menu open (browsing, not loading) when USB STOP is pressed on deck 1 (accepted case), then re-opens it after a swap to a stick with different track ids. |
| `pcaps/usb-yank-then-reinsert.pcap` (optional) | After a yank, re-insert the same stick and then a different one; note whether the decks leave the emergency loop by themselves. |

Sonnet analysis (add a section "Abnormal removal" to `usb-switching-protocol.md`, confidence-tagged):
1. Per-packet differences from the clean STOP sequence: do 0x6f/0x77 skip the 02/03 stages (00 -> 04 directly?), do any other status bytes change (play state 0x7b, loop/emergency flags, `flags89`, loaded-track fields), on the owner and on the remote decks?
2. Is the "emergency loop" visible on the network at all (look for loop-related fields or flags in the remote deck's status packets; compare to the human's wall-clock notes)? State plainly if **nothing** is communicated - then it is purely local behaviour and vdj-ui cannot trigger it.
3. TCP/NFS side effects: RST/FIN on 1051/12523, NFS errors, mount drops, remote re-query behaviour after re-insert, stale browse content, and whether track-id reuse across sticks needs a flush on our side.

Then, depending on findings and with the human's agreement:
- Make `LinkExport` robust to the same events from our side (the folder disappears under us: unplugged physical pendrive in `/media/<user>`): detect it (file errors, scanner `physicalChanged()`), go to the Empty state per the abrupt-removal sequence, log it, never crash.
- Optionally add an "Abrupt eject (test)" action only if the sequence is fully understood from captures; otherwise record it as not implemented.
- Add tests with the captured byte sequences.

```sh
geany-progress done 8 -r references/adj/doc/usb-switching-protocol.md -r src/network/link_export.cpp -w "Abnormal removal sequence comes only from the human's hardware tests" -w "Anything about the emergency loop that is not seen on the wire stays unimplemented"
```
