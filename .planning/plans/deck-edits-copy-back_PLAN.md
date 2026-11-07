# Copy beat grid and cue point edits from the decks back into trakcrate

## The workflow this serves

1. Buy music; keep it in trakcrate as **folders**. Make playlists, export them (beat grid, no cue points) to a pendrive.
2. Play the pendrive on real decks (Pro DJ Link). Adjust beat grids and set cue points on the decks.
3. Connect vdj-ui, find the playlist that is playing on the deck in the **rekordbox viewer**, right click a playlist or a track, **copy back**: only the beat grid and/or the cue points go into the trakcrate library, only for the tracks chosen. Nothing else (metadata, artwork, audio, waveforms) is touched.

## What is not known (so the first phases are human tests, no trakcrate writes)

- **Where does a deck write its edits?** Hot cues, memory cues and (on some models) beat grid changes are saved by the deck onto the stick it loaded the track from. Candidates: `PIONEER/USBANLZ/Pxxx/<id>/ANLZ0000.DAT` / `.EXT` (PQTZ, PCOB, PCP2...), `export.pdb` rows, `PIONEER/rekordbox/exportExt.pdb`, newer decks' `exportLibrary.db` (not supported by us at all). Which file(s) change, per deck model, is unproven. Our own notes (`doc/trakcrate/import-from-rekordbox.md`) only cover reading a drive that rekordbox itself wrote.
- **Is the viewer's data fresh?** `RemoteDeck` (`src/nfs/remote_deck.{h,cpp}`) mirrors the deck's files into `~/.cache/vdj-ui/p<N>_...` and "skips the transfer when the local copy has the size the player reports". An edited ANLZ file keeps its size often, so the viewer may show **stale** grids and cues. This is the main suspected hazard. A viewer opened on the *pendrive plugged into the PC* reads the files directly and is fresh by construction, but only after the deck has flushed and the stick was ejected properly.
- **Does the deck keep edits in RAM until eject / next load?** Timing of the write is unknown.
- **Which deck is playing which row?** Status packets carry the track id and the slot it was loaded from (`NetworkManager::noteRemoteLoad`, `playingFrom/slot/trackType`); the rekordbox id in them is the `export.pdb` track id of the stick in that slot. Unproven that it can be turned into a highlighted row in the viewer.
- **Identity:** how to find the library track for an exported track. trakcrate does not currently remember `pdb_track_id` for tracks it exported (only for tracks imported from rekordbox, in `source.txt`). Candidates: the audio file name inside `Contents/`, SHA-256 of the audio, title+artist+duration.

Everything the plan builds before Phase 8 is **read only** towards the library.

## Existing pieces to reuse

- `src/trakcrate/rb_import.{h,cpp}`: already turns a rekordbox drive's ANLZ (PQTZ/PQT2 grid, PCOB/PCP2 cues) into `beatgrid.txt` / `cues.txt` with `source.txt` provenance. The decode half is the reader for edits.
- `src/trakcrate/track.h` (`loadTrack`, `saveBeatGrid`, `saveCuemarks`), `doc/trakcrate/beat-grid.md`, `doc/trakcrate/cuemarks.md`, `doc/trakcrate/trakcrate-db-format.md`.
- `src/trakcrate/scripts.h`: snapshots into `<track>/.trakcrate-history/<time>-<name>/`.
- `src/ui/track_browser.{h,cpp}` (the viewer, local folder or `openRemoteDeck`), `src/nfs/remote_deck.*`, `src/network/network_manager.*`.
- `libcdj` ANLZ parser (`references/libcdj/src/anlz`); memory `anlz-parser-status`.

---

## Phase 1: Snapshot tool (read only) and a baseline

Goal: a way to record exactly what the analysis files of a drive or a deck look like, so before/after comparisons are objective. No trakcrate library access.

Work:
1. New small command line tool `vdj-rb-snapshot` (or a `trakcrate` sub command if the CLI has a natural place; look at `src/tools/`) taking a rekordbox export root (a mounted pendrive or a directory) and writing a snapshot folder:
   - `manifest.txt`: for every file under `PIONEER/rekordbox/` and `PIONEER/USBANLZ/` (and `PIONEER/` top level files): path, size, mtime, SHA-256.
   - `tracks.txt`: for each `export.pdb` track row: id, title, artist, `Contents/` path, analyze path, file size, and a decoded summary of its ANLZ: beat count, first beat time, BPM first/last, number of cue entries by kind (hot A-H, memory, loops) with times in ms. Reuse the libcdj anlz reader and the decode already written for `rb_import.cpp`.
   - Copies of the `.DAT/.EXT/.2EX` files themselves (`files/...`) so they can be diffed later.
2. `snapshot-diff` mode: two snapshots in, a plain text report out: files added/removed/changed (hash), then per track the changed grid (beat count, first beat shift in ms, BPM change) and cue lists (added / moved / removed).
3. Unit tests with the fixtures that already exist (`tests/fixtures`, the usbanalz data) including a synthetic pair of ANLZ files that differ by one beat shift and one added hot cue.
4. Document the tool in `doc/deck-edits.md` (new): how to run it, and a table to fill in during the human tests.

**Human test 1 (baseline):** export a short playlist (4-6 tracks, grid and no cues) to the pendrive with trakcrate. Run the snapshot on the stick: `baseline`. Also make a second copy of the stick's folder tree on the PC. Record which deck models (and firmware) will be used.

Review: tool code, the diff report format.

```sh
geany-progress done 1 -r src/tools -r doc/deck-edits.md -w "read only; human test 1 (baseline snapshot of the exported stick) must be done before Phase 2"
```

## Phase 2: Human test 2 - do the decks write edits to the stick? (pendrive route)

Goal: answer "where do edits go" using the physical stick, which has no caching problem. No new code unless the diff report needs improving.

Procedure for the human (write it as a checklist in `doc/deck-edits.md`; fill in results):
1. Put the stick in a deck. Load track A. **Do nothing**, eject properly (USB STOP). Snapshot `S0`. Diff against baseline: do the decks write anything just from loading/playing (history, play count, `export.pdb`)? Expect noise; note it so it is not mistaken for edits later.
2. Load track B. Set 2 hot cues and 1 memory cue (remember times). Eject properly. Snapshot `S1-cues`. Diff vs `S0`.
3. Load track C. Adjust the beat grid (move/shift the grid, whichever the deck model offers; record what the deck offers). Save, eject. Snapshot `S2-grid`. Diff.
4. Repeat step 2 **without ejecting properly** (just wait 1 minute, pull the stick after the track is unloaded). Note whether the edit survived. Then repeat but leave the track loaded while the stick is read on the PC over the network (see Phase 3).
5. Record per deck model: which files changed, whether PDB rows changed (cue counts, `exportExt.pdb`), whether new files appeared (for instance an `exportLibrary.db` or `DEVICE` folder), and how long after the edit the files changed.

Decision outputs (write into the doc, section "Findings"):
- Cue edits: file and tag where they land (PCOB list types 0/1, PCP2, ...) per model.
- Grid edits: whether and where they land (PQTZ/PQT2), or "decks cannot save grid edits" (then the feature is cue-only for that model).
- If edits are in `exportLibrary.db` or another format we cannot read, **stop and report**; the plan then needs a new reader phase before anything else.

Review: the findings section.

```sh
geany-progress done 2 -r doc/deck-edits.md -w "human test; if the edits are not in ANLZ/PDB files we can read, STOP and revisit the plan"
```

## Phase 3: Human test 3 - freshness over the network (the viewer's own route)

Goal: find out whether the rekordbox viewer, opened on a connected deck, shows the edited data, and make it fresh if it is not.

Code (small, contained in `src/nfs` and the viewer; read only towards the deck):
1. Make staleness visible: `RemoteDeck` logs, per ANLZ/PDB fetch, whether it was served from the cache or transferred, the size reported and the size cached, and the file's mtime if the NFS attributes give it (`XdjNfs` attributes). Add a status line in the viewer: "analysis files fetched at hh:mm:ss (cached / from the deck)".
2. A forced mode: `RemoteDeck::ensureAnalysis(path, bool refresh)` that always re-transfers (or compares mtime plus size plus a content hash) and replaces the cache copy atomically. A viewer action **"Refresh from the deck"** (right click on a track or playlist, and on the tree root) that drops the cached `export.pdb` and the analysis files of the selected tracks and reloads. No automatic behaviour change yet.
3. Same decode path as Phase 1 so the snapshot tool can also be pointed at a RemoteDeck cache folder (`vdj-rb-snapshot --deck N`), giving objective before/after for the network route.

**Human test 3:**
1. Deck on the network, vdj-ui connected, deck (with the stick) shows in the viewer. Open it; snapshot `N0` of the cache.
2. Edit cues on track B on the deck (while it stays loaded, then after unloading it, then after USB STOP). Each time: use "Refresh from the deck", snapshot, diff against `N0`.
3. Compare to the pendrive route results from Phase 2: same files, same timing? Record: does the deck serve the edited bytes while the track is still loaded, only after unload, or only after the stick is ejected (the stick then is no longer reachable - then network route is useless for this feature)?
4. Check the plain (non refreshed) viewer: does it show stale cues? (Confirms the suspected size-only cache check.)
5. Does the dbserver route differ? The deck answers beat grid and cue requests from its own memory (`doc/rekordbox-mode.md` and `src/dbserver` describe the messages). Optional: query the loaded track's beat grid / cue list message from vdj-ui and compare to the file on the stick. If the dbserver answer already contains the edits while the file does not, note it as a possible source (the playing deck's current state).

Decision outputs in `doc/deck-edits.md`: for each source (pendrive plugged into the PC, deck over NFS, deck over dbserver) - fresh? when? needs which user action? Pick the **primary** source(s) for the feature.

Review: `remote_deck.*` changes, the refresh action, findings.

```sh
geany-progress done 3 -r src/nfs/remote_deck.cpp -r src/ui/track_browser.cpp -r doc/deck-edits.md -w "human test; fixes only staleness reporting and an explicit refresh, no automatic behaviour change"
```

## Phase 4: Show what is playing on the deck in the viewer

Goal: with the deck's database open in the viewer (or the pendrive folder), highlight the playlist and the track that are playing on the connected deck. Read only.

Work:
1. From the status packets that `NetworkManager` already tracks (`RemoteLoad`: player, slot, track type, rekordbox track id), expose "deck N plays track id T from slot S (USB/SD) of player P". Check `noteRemoteLoad` and the status packet decode for the track id field; add a signal `deckTrackChanged(playerId, slot, trackId)` if missing.
2. Viewer: when the shown database belongs to that deck/slot (remote) or its `export.pdb` matches (local folder; compare the stick's database identity, for example the media-query id/label and track count), find the track row by id and playlists containing it. Mark it in the tree (bold + "playing on deck N") and scroll to it; if the playing track is in several playlists, mark all, and prefer the playlist that is selected. Playlist context is not sent by the deck, so "the playing playlist" is **the playlist the user selected that contains the playing track**; say so in the doc.
3. Test with injected status data (no hardware): a fake deck load for track id X must mark exactly the rows with id X in a fixture `export.pdb`.

**Human test 4:** load tracks on the deck from the playlist, confirm the viewer follows within ~2 s, also after switching tracks and decks; note mismatches (wrong slot, two decks).

Review: viewer marker code, the test.

```sh
geany-progress done 4 -r src/ui/track_browser.cpp -r src/network/network_manager.cpp -w "playlist context is inferred from the selection, the deck only reports the track"
```

## Phase 5: Findings review and decision gate

No code. Update `doc/deck-edits.md` with the final matrix (model x edit kind x source x freshness), and write the **decision**:
- Which source the copy-back reads (candidate defaults: the stick's ANLZ files read via the viewer after "Refresh from the deck", or the plugged-in stick).
- Whether grid copy-back is supported (depends on Phase 2).
- Whether the deck's cue kinds map onto trakcrate's cuemarks (hot A-H, memory, loop; colours, comments - see `doc/trakcrate/cuemarks.md`). List the kinds that cannot be represented and say what happens to them (skipped with a warning).
- Update `.planning/plans/deck-edits-copy-back_PLAN.md` phases 6-10 if the findings change them (this phase may rewrite them).

Stop here and get the human's go-ahead before Phase 6.

```sh
geany-progress done 5 -r doc/deck-edits.md -w "decision gate: nothing after this phase touches the trakcrate library; confirm the findings with the user before starting Phase 6"
```

## Phase 6: Matching viewer tracks to library tracks (no writes)

Goal: `matchTracks()` answers "which library track folder is this deck track?" with a confidence and a reason.

Work:
1. New `src/trakcrate/deck_match.{h,cpp}`:
   - Input: a viewer track (`export.pdb` row: id, title, artist, `Contents/` path, file size, optional audio file read from the stick) and a `Library`.
   - Strategies in order, first unambiguous hit wins: (a) a mapping written by our own exporter (see 2 below); (b) `source.txt` `pdb_track_id` + `imported_from` for tracks that came from rekordbox; (c) audio SHA-256 equal to the master's (cheap pre-filter by size and duration); (d) title + artist + duration within 1 s as a weak match flagged "needs confirmation".
   - Result: `{trackDir, how, confidence}` or ambiguous list or none.
2. Make our exporter remember the mapping so (a) works for the user's main workflow: when exporting, write `export-map.txt` in the drive's library area (or in the virtual pendrive folder `drives/<name>/`) listing `pdb_track_id <TAB> master track folder <TAB> audio sha256`. Check `doc/trakcrate/rekordbox-db-output.md` for where track ids are assigned; ids change per export, so the file is per export. Importantly this adds a file in trakcrate's own library only, not on the stick. Tracks exported before this feature fall back to (c).
3. Tests: fixture library + fixture `export.pdb`; each strategy, ambiguity (same audio in two folders - report both), and "not in the library".

Review: `deck_match.cpp`, the exporter map change.

```sh
geany-progress done 6 -r src/trakcrate/deck_match.cpp -r src/trakcrate/export.cpp -w "adds export-map.txt to the library side of exports; old exports match by audio hash"
```

## Phase 7: Read the deck's edits and compute a preview (no writes)

Goal: a pure function that compares a deck track's grid and cues with the library track and lists the differences.

Work:
1. `src/trakcrate/deck_edits.{h,cpp}`:
   - `readDeckAnalysis(anlzFiles)` -> `{BeatGrid grid (our type), vector<Cuemark> cues}` using the same decode as `rb_import.cpp` (extract the shared function rather than copying it; both paths must agree).
   - `diffAgainstLibrary(Track, DeckAnalysis)` -> `GridDiff {same, beats changed, bpm old/new, first beat shift ms}` and `CueDiff {added, moved, removed, unrepresentable}`; ms tolerance 1 ms.
   - Tracks whose grid is identical report "no change", and are skipped in a copy-back.
2. Tests: round trip (library track -> exported by our writer -> read back = no diff); shifted grid; moved/added hot cue; deck-only cue kinds that the library cannot hold.

Review: the shared decode refactor in `rb_import.cpp` (must keep its tests green).

```sh
geany-progress done 7 -r src/trakcrate/deck_edits.cpp -r src/trakcrate/rb_import.cpp -w "decode is shared with the drive import: run the rbimport tests"
```

## Phase 8: The copy-back writer (first phase that writes the library)

Goal: apply chosen differences to library tracks, grid and/or cues, safely.

Work:
1. `applyDeckEdits(lib, plan, options)` where options are `grid` and `cues` (independent checkboxes) and the plan lists tracks:
   - Before any write, snapshot the files that will change into `<track>/.trakcrate-history/<time>-deck-copy-back/` (same mechanism as scripts, `scripts.h`).
   - Writes only `beatgrid.txt` and/or `cues.txt` (`saveBeatGrid`, `saveCuemarks`), atomically. `meta.txt`, artwork, audio, waveforms are never opened for writing. A test asserts byte equality of every other file in the folder.
   - `source.txt`: set the per field origin of `beatgrid` / `cues` to `edited` (the rule in `import-from-rekordbox.md`) with the source recorded (`deck_copy_back_from: <pendrive/deck, date>`).
   - Verbatim ANLZ copies kept in `<track>/anlz/` from an earlier rekordbox import would be exported instead of the new grid: when grid or cues are copied, drop or mark those stale (decide from `beat-grid.md`; the exporter rule "write imported ANLZ files verbatim when all parts are rekordbox" is the place that must see `edited`). Test that a re-export contains the new grid.
   - Cue merging policy for the user's workflow (library has cues from a previous copy back, deck has the full set): default **replace** the library cue list by the deck's list for the chosen tracks; option "add only" keeps existing ones. Choose in Phase 5 and test both.
   - Running analysis skip rule: copied grids are "edited", so re-analysis does not overwrite them.
2. Dry run returns the same report without writing. Report: per track what changed, errors, skipped (no match, ambiguous, unchanged).
3. Tests: grid only; cues only; both; unchanged track untouched (mtime stays); failure halfway leaves earlier tracks done and a clear report; history snapshot exists and restores the old state.

Review: writer, the history snapshot, the exporter interaction.

```sh
geany-progress done 8 -r src/trakcrate/deck_edits.cpp -r src/trakcrate/export.cpp -w "first phase that writes the library: only beatgrid.txt, cues.txt and source.txt, history snapshot taken first; check the ANLZ verbatim interaction"
```

## Phase 9: UI in the rekordbox viewer

Goal: right click copy back, with a preview and a confirmation.

Work:
1. `TrackBrowser` context menu on a track, a selection of tracks, or a playlist: **"Copy beat grid and cues back to trakcrate..."** Enabled only when a trakcrate library is open (the main window passes the `TrakcratePanel` library; the viewer emits `copyBackRequested(QList<TrackInfo>, source)` and the main window owns the dialog so the viewer keeps no dependency on trakcrate).
2. Dialog `TrakcrateCopyBackDialog`: table of tracks (deck title, matched library track + how, grid change summary, cue change summary, status), checkboxes **Beat grid** and **Cue points**, per row tick, a "freshness" line from Phase 3 ("analysis fetched 2 min ago from the deck / read from the pendrive") with a **Refresh from the deck** button, and **Copy** / **Cancel**. Unmatched or ambiguous rows are shown but unticked; ambiguous ones let the user pick the library track from a list. Nothing is written until Copy is pressed; Copy shows a summary (tracks changed) and tells where the history snapshot is.
3. The playing track/playlist highlighted in Phase 4 is the default selection: with a playlist selected, "copy back" takes the playlist's tracks.
4. Hide the library tree refresh problem: after Copy the trakcrate panel `refresh()`es and the track editor reloads if it shows one of the tracks.
5. Tests with Qt Test: dialog with a fixture library and injected deck analyses; checking boxes changes what the writer is asked to do; Cancel writes nothing.

Review: dialog, menu wiring in `main_window.cpp`.

```sh
geany-progress done 9 -r src/ui/trakcrate_copyback_dialog.cpp -r src/ui/track_browser.cpp -r src/ui/main_window.cpp -w "viewer must stay independent of trakcrate; main window owns the dialog"
```

## Phase 10: Documentation and the acceptance test with real decks

1. Docs: `doc/deck-edits.md` (final form: how it works, the freshness rules, which decks, limits), a section in `doc/trakcrate/gui.md`, `doc/trakcrate/import-from-rekordbox.md` cross reference, `UNKNOWN_BYTES.md` for any new unknown bytes seen in deck-written files, `ai-output/refactor-recommendations.md` for general code notes.
2. `ctest` full run; fix flakes only if caused by this work.

**Human test 5 (acceptance, the real workflow):** export a playlist with grids and no cues -> play on the decks, set cues and adjust a grid -> in vdj-ui open the deck's (or the stick's) database in the viewer, see the playing track highlighted -> right click the playlist -> Copy back (preview: only the edited tracks show changes) -> check in trakcrate that the grids and cues are there, that the folder's other files are byte identical (compare with the history snapshot), export the playlist again and confirm the new cues appear on the deck. Record what went wrong and any model specific limits in the doc.

```sh
geany-progress done 10 -r doc/deck-edits.md -r doc/trakcrate/gui.md -w "needs the real-deck acceptance test (human test 5) before the feature is called working"
```

---

## Constraints for the implementer

- Never use git commands, never submit code, never change `.perf.targets`.
- Phases 1-7 never write into a trakcrate library or onto a stick/deck; Phase 3 only writes the local cache folder.
- Keep every phase to the files it names; shared decode (Phase 7) is a refactor, keep the existing tests green.
- If a human test shows the deck does not store the data (or stores it somewhere unreadable), stop at the gate (Phase 5), report, and do not start Phase 6.


---

## REVISION (user, 2026-11-07): UI first, human testing in vdj-ui, no CLI snapshot tool

Finding from the user's first test: the decks DO update the database that the rekordbox viewer shows, but the viewer
has to reload the data over the network to see it. So the original Phases 1-2 (snapshot tool, pendrive tests) are dropped
and the work is re-ordered. Human testing happens in vdj-ui itself. Do not use git.

Revised phases (these are the ones registered with geany-progress):

1. **Refresh from deck**: right click on the top (root) tree item of the rekordbox viewer: "Refresh from deck". Only present when the
   viewer shows a database fetched over the network from a deck (not a local folder/pendrive). Drops the `RemoteDeck` cache of
   `export.pdb` and the analysis files and reloads the tree. (Original Phase 3 item 2.)
2. **Copy back menu items**: in the playlist context menu "Copy back beatgrid" and "Copy back cue points"; in the track context menu the same
   pair for that track (or the track selection). The viewer only emits a signal with the tracks and the kind; the main window handles it.
3. **Match + read + diff**: original Phases 6-7: map viewer tracks to library tracks, decode the deck's grid/cues (shared with rb_import), diff.
4. **Writer + confirmation**: original Phase 8 (history snapshot, only beatgrid.txt/cues.txt/source.txt) and a confirmation/preview dialog
   (original Phase 9), wired to the menu items of phase 2.
5. **Docs and human acceptance test in vdj-ui** (original Phase 10).
