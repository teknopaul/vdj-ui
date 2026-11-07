# Add to rekordbox: append a playlist to an existing pendrive

Source brief: `ai-prompts/add-to-rekordbox.prompt.md`. Related: `doc/trakcrate/rekordbox-db-output.md`, `doc/trakcrate/pdb-write-notes.md`, `doc/trakcrate/usbanalz-hashing.md`, `.planning/plans/concurrent-export_PLAN.md` (the multi-export UI this reuses).

## Goal

Right click a playlist, "Add to rekordbox >" lists the mounted sticks that hold `/PIONEER/rekordbox/export.pdb`; choosing one adds that playlist (tracks, metadata, ANLZ, artwork, audio) to the stick's **existing** database without rebuilding it, never damaging the stick, surviving an early eject. No trakcrate library change, no user-settings change on the stick. A `trakcrate` CLI equivalent.

## What exists (so the gap is small except the database editor)

| Need                                                                                                                     | Already there                                                                                                                                       |
|--------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------|
| Whole-file pdb model that round-trips any file and rebuilds data pages from rows                                         | `PdbFile`, `parsePdb`, `serialisePdb`, `tableRows` (`src/trakcrate/pdb_writer.{h,cpp}`)                                                             |
| Row encoders for tracks/artists/albums/genres/labels/playlists/entries/artwork, string encoding, UTF-16 alignment        | `src/trakcrate/pdb_build.cpp` (anonymous namespace: `trackRow`, `artistRow`, `albumRow`, `nameRow`, `playlistRow`, `entryRow`, `loadTrackTemplate`) |
| ANLZ folder naming (slot hash, partition, collision counter)                                                             | `pdb_build.cpp` inside `buildPdb` (inline, not callable on its own)                                                                                 |
| Gathering a playlist into tracks with paths, artwork, waveforms, grids, cues                                             | `gather` in `exporter.cpp` (`GTrack`, `Gathered`)                                                                                                   |
| Staging, ANLZ/artwork build, free-space reservation, copy with temp name + rename + fsync, `.bak/.bak.2/.bak.3` rotation | `exportDrive`, `copyOne`, `backupPdb` in `exporter.cpp`                                                                                             |
| Read back and compare against a plan                                                                                     | `verifyPdb` (libcdj `pdb_parse` based)                                                                                                              |
| Several exports at once, jobs strip, cancel, notifications                                                               | `TrakcratePanel::exportDrive`, `ExportJob`, `TrakcrateJobsStrip`                                                                                    |
| Mounted media listing                                                                                                    | `QStorageInfo` in `trakcrate_export_dialog.cpp`                                                                                                     |

**The big gap: nothing edits an existing database.** `buildPdb` always lays a whole new file out from the template, and `exportDrive` with `update` replaces the stick's database with that (it never reads the old one). Editing one is new code: id allocation against existing rows, page allocation in the middle of real rekordbox page chains, header counters. That is Phases 3 to 5 (hardware proof in Phase 7). Everything else is reuse plus a small amount of extraction.

## Gaps in the design brief (decide before or during Phase 1)

1. **Index pages are not understood.** Each table starts with an "index page" (flag 0x64). In our own exports it only names the first data page; in a real rekordbox export of a big table it holds a few hundred words (offsets like `0xa98`, `0xb00`) that nobody has decoded (`pdb-write-notes.md`). Plan: do not touch index pages, add data in the table's last page when it fits, else new pages linked through `next` (our own 2000 track exports work on decks with trivial index pages, which suggests decks follow the chain). Must be proven on hardware with a real rekordbox stick; Phase 1 makes this explicit and Phase 8 ships the hardware checklist.
2. **Tracks already on the stick (DECIDED).** Re-use audio and ANLZ wherever possible: a playlist track that is already on the stick gets no new database row, no audio copy and no new ANLZ; the new playlist entry **references the existing track id**. Matching: the track's rekordbox path (`/Contents/...`, the path the exporter would create for it, compared case-insensitively because the stick is FAT) against the paths in the stick's track rows, then file size + audio hash against files found at that path. A path that is taken by a *different* file gets a new unique name (see item 9). The plan must prove the path creation is right: unit tests feed the exporter's path rules (`rekordboxFolderName`, `fatSafeName`, the 48 code point cut, the `Artist/Album/File` layout) with paths read from the real fixture databases and require identical results for tracks that rekordbox itself wrote with plain names.
3. **Playlist name already exists on the stick (DECIDED).** Refuse before anything is written, with a message naming the playlist. Comparison is exact, then case-insensitive; no suffixing, no merging.
4. **Next-generation sticks (DECIDED).** The menu lists a stick only if `export.pdb` exists; the engine **refuses** (before touching the stick) if `PIONEER/rekordbox/exportLibrary.db` is present.
5. **Stick state after use on a deck.** A deck appends play history rows and bumps counters (`UNKNOWN_BYTES.md`, history tables 11 to 12); deleted rows and `0x34` flag pages occur. Sanity check must accept those, and the editor must keep the sequence/`u10` counters monotonic.
6. **Playlist folders.** The playlist tree may have folders (`is_folder`) and nesting; new playlist goes at the root, `sort_order = max(root sort_order) + 1`. Existing folders are left alone.
7. **Id allocation.** Reuse existing artist/album/genre/label/artwork ids by exact name (album also by artist id); new ids = max existing + 1 (not row count: ids can have gaps). Track ids likewise.
8. **Artwork numbering.** New `a<N>.jpg` must continue the stick's numbering and folder grouping (`/PIONEER/Artwork/00001/...`); read it from the artwork table, not from the folder listing alone.
9. **Contents collisions.** A target path that exists with different content must get a different name (and FAT is case insensitive). Existing identical file (size + hash) is reused. Also the 48 character cut rekordbox applies and 255 byte FAT names.
10. **Backup in `/var/tmp` (DECIDED).** The stick's `export.pdb` is copied to `/var/tmp/rekordbox-edit-backup-export.<isodate>.pdb` (ISO 8601 date and time, e.g. `2026-11-07T10:31:05`, colons kept out if they upset the file system: use `20261107T103105`; if the name exists add `-2`). Never deleted by trakcrate (the files are small). The result message states the path. The copy is made before anything on the stick changes and is checked against the original bytes.
11. **The swap order (DECIDED: the brief's).** Write the new database next to the old one as `export.pdb.new` (full copy with `copyOne`, fsync), then two renames: `export.pdb` to `export.pdb.bak` (rotating `.bak`, `.bak.2`, `.bak.3` by renames, the same names as the virtual pendrive update), then `export.pdb.new` to `export.pdb`. Renames are metadata only, so the gap between them is microseconds and no 1 to 2 MB copy of the old database is made. A stick pulled inside that gap has `export.pdb.bak` and `export.pdb.new` (and the `/var/tmp` copy) and is repaired by renaming one file; the result message and the notes document say so. If the second rename fails, rename the `.bak` back at once. Progress announces "Switching database..." before the first rename.
12. **Early removal cleanup.** New audio/ANLZ/artwork written before the switch are harmless orphans if the stick is pulled; on an error the engine removes what it added when the stick is still there, and reports what it could not.
13. **`exportExt.pdb`, `USBMNG.DAT`, `RBFLTR.DAT`** are left untouched (no settings changes). `USBMNG.DAT` slots written by decks are not read or changed. Whether `exportExt.pdb` needs rows for new tracks is unknown; hardware checklist item.
14. **Warn about unanalysed tracks** without another dialog normally: one question box only when some tracks are unanalysed, with "Add anyway" (flat waveform, no grid, as `allowUnanalysed` does today) and "Cancel".
15. **Concurrency.** Two additions to the same stick must exclude each other (reuse `destinationsOverlap`), and an addition must not run while a normal export targets that stick.
16. **Library read only.** Nothing may call library write functions (no `order` renumbering, no meta writes) from the add path; test with a read only library copy.

## Architecture decisions

- New module `src/trakcrate/pdb_append.{h,cpp}`: `loadDriveDb` (sanity + indexes of an existing db) and `appendPlaylist` (edit the in-memory `PdbFile`). Pure functions on bytes, fully unit testable, no UI.
- New engine entry `addToDrive(lib, AddOptions, progress, cancel)` in `src/trakcrate/exporter.{h,cpp}` (or `drive_add.cpp` if exporter.cpp gets too large), sharing helpers extracted in Phase 2.
- **Refactoring needed first (Phase 2):** `exportDrive` is one 330 line function; the add path needs its `gather`, per-track ANLZ/artwork staging, copy pass and backup/replace, but not its database build or settings. Extract those into named internal functions with no behaviour change (the existing 35 exporter tests are the safety net), and extract the ANLZ path computation out of `buildPdb` into `analyzePathFor(filePath, used)` so the append code uses the very same rule. Nothing in `libcdj` is *necessary*.
- **C++ first (DECIDED), real hardware before the CLI.** The GUI/C++ engine is built and tested on decks (Phase 7) before anything is ported. The CLI helper is C (`src-cli/c/lib/`), a separate implementation of the exporter checked against the C++ one by an oracle test; its port (Phase 8) happens only after the hardware results, because those may change the page-placement rules. If the port is judged too large, the CLI is documented as GUI only.
- libcdj: no change is required. Optional refactor recommendations (move the `PdbFile` model and row encoders into libcdj, shared by GUI and CLI; expose the ANLZ path hash from libcdj; add a pdb writer test fixture there) go to `ai-output/refactor-recommendations.md` in Phase 8.

## Phase 1: Study and rules (no production code)

Files: new `doc/trakcrate/pdb-append-notes.md`; new test fixture folder `tests/fixtures/trakcrate/pdb-drive/` (a small real rekordbox export with several pages per table; `/var/tmp/rekordbox-mini/PIONEER/rekordbox/export.pdb` is 53 pages, check licence/personal data before copying; plus `PIONEER/USBANLZ` entries or only the db if sizes are large).

- Read, with the existing `trakcrate-helper pdb --pages` and a throwaway script (scratchpad, not committed), the real files: `/transfer/cdj64`, `/var/tmp/rekordbox-mini`, `/var/tmp/RB_EXPORT_LOADS_FINE`: how new rows are placed in a page that has free space, how `emptyCandidate`, `lastPage`, `nextUnusedPage`, page `u10`, `numLarge`, `u20` change, how a deck-touched db (history rows, `0x34` pages) looks.
- Write down the append algorithm: choose page (last page of the table with room, else allocate at `nextUnusedPage`, link `next`, update `lastPage`/`emptyCandidate`, header `nextUnusedPage`, `sequence`), what to do with row group flags and the page counters, which fields of the touched pages to bump. State clearly what is guesswork and must be checked on hardware (index pages left alone).
- Write the sanity rules the loader enforces (table count 20, chain walks terminate and stay in range, row offsets inside heap, ids unique per table, track strings readable, every entry refers to existing track and playlist, analyze/artwork paths well formed) and the refusal cases (no db, unparsable, `exportLibrary.db` present, file bigger than a sane limit, free space too small).
- Record the decisions from "Gaps" items 2, 3, 4, 10, 11 in the notes (all decided by the user except 11, which follows this plan unless the user objects).
- Create the fixture (the user confirmed the rekordbox databases hold nothing personal; metadata may be committed, **no audio files**) and confirm `parsePdb`/`serialisePdb` reproduce it byte for byte (a test goes in Phase 3).

Validation: the notes doc exists and lists every decision; the fixture parses and round-trips with a scratch test run.

```sh
geany-progress done 1 -r doc/trakcrate/pdb-append-notes.md -r tests/fixtures/trakcrate/pdb-drive -w "Index pages of real tables are left untouched: only hardware can prove this" -w "Decisions recorded: refuse on playlist name clash and exportLibrary.db, reference existing tracks, never-deleted /var/tmp backups"
```

## Phase 2: Extract shared pieces from the exporter (no behaviour change)

Files: `src/trakcrate/exporter.cpp`, `exporter.h`, `pdb_build.cpp`, `pdb_build.h`, tests `test_trakcrate_export.cpp`, `test_trakcrate_pdb_build.cpp`.

- Extract from `exportDrive`: `stageTrackFiles(g, o, stage, built.analyzePaths, ...)` (the ANLZ + artwork loop at about lines 590 to 645), `copyPass(files, ...)` (the `step` lambda and loop), `replaceDatabase(dest, newPdb, ...)`, and split `backupPdb`'s rotation of `.bak/.bak.2/.bak.3` from its copy so the add engine can rotate by rename (the virtual pendrive update keeps copying: its behaviour is unchanged). Keep every message, order and result field identical.
- In `pdb_build.cpp` move the inline hash into `std::string analyzePathFor(const std::string& filePath, std::set<std::string>& used)` (declared in `pdb_build.h`); `buildPdb` calls it. Expose row encoders needed by the appender (`trackRow` needs the template: expose `TrackTemplate`, `loadTrackTemplate`, `trackRow`, `artistRow`, `albumRow`, `nameRow`, `playlistRow`, `entryRow` in a small `pdb_rows.h`; implementation stays in `pdb_build.cpp`).
- **As done (2026-11-07):** `stageTrackFiles` (with an `only` filter) and `rotateBackups` were extracted; `copyPass`/`replaceDatabase` were not needed (the add engine calls `copyOne` and `rename` directly). `pdb_rows.h` exposes the encoders. **Phase 4 must not copy a real drive's first track row blindly:** `loadTrackTemplate` takes the string slots (ISRC, etc.) of whatever row is first, which is harmless for the 7 track template but would copy one real track's extra strings to every new track; the appender needs a neutral template (fixed bytes of an existing row, the other string slots empty).
- Tests: all existing tests unchanged and green; add a test that `analyzePathFor` gives the known pairs (use the `usbanalz-data` verified pairs, e.g. the Afrovox `P049/00010C89` case if the path is available in the test data, else three pairs from `horner_test.py`) and increments the counter on a clash.

Validation: `make` and `ctest` are green, `src-cli` tests unchanged (the CLI is not touched yet).

```sh
geany-progress done 2 -r src/trakcrate/exporter.cpp -r src/trakcrate/pdb_build.h -w "Pure refactor: any behaviour change here is a bug"
```

## Phase 3: Load and sanity check an existing database

Files: new `src/trakcrate/pdb_append.h`, `pdb_append.cpp`; `src/CMakeLists.txt`; new `tests/test_trakcrate_pdb_append.cpp` (+ `tests/CMakeLists.txt`).

- `struct DriveDb { PdbFile file; maps name->id for artists/genres/labels, (artist,name)->album id, artwork path->id; max ids per table; set of analyze paths used; track path -> track id; playlist names; max playlist sort order; max artwork number; counters }`.
- `bool loadDriveDb(const std::string& bytes, DriveDb* out, std::vector<std::string>* problems)` implementing the Phase 1 sanity rules. Decode ids and names from rows with the existing string decoder in `pdb_build.cpp` (expose a `decodeDeviceSqlString`) rather than a second parser.
- Tests: the Phase 1 fixture and `export7.pdb` load with the expected counts and maxima; round-trip through `serialisePdb` is byte exact; corrupted copies (truncated, bad page size, chain loop, row offset past the heap, entry to a missing track, duplicate ids) each give a specific problem and a false return; a deck-touched sample (deleted rows, `0x34` pages) loads.

Validation: new test target green; reading never writes.

```sh
geany-progress done 3 -r src/trakcrate/pdb_append.h -r tests/test_trakcrate_pdb_append.cpp -w "Sanity rules are my reading of real files: tighten after hardware tests"
```

## Phase 4: Edit the in-memory database

Files: `src/trakcrate/pdb_append.{h,cpp}`, `tests/test_trakcrate_pdb_append.cpp`, `pdb_build.cpp` (only if `verifyPdb` needs a generalised form).

- `struct AppendResult { std::vector<std::string> analyzePaths; std::vector<std::string> artworkPaths; std::vector<bool> trackIsNew; uint32_t playlistId; ... }`
- `bool appendPlaylist(DriveDb& db, const ExportPlan& plan, const AppendOptions& o, AppendResult* out, std::string* err)`: reuse tracks already present (Gaps item 2), allocate ids, build rows with the Phase 2 encoders (analyze paths via `analyzePathFor` seeded with `db.analyzePaths`; artwork continues numbering), insert rows with the Phase 1 page algorithm (new rows only, existing rows and pages byte-identical), add the playlist tree row and entries, bump counters, return a `PdbFile` that serialises.
- `std::vector<std::string> verifyAppended(const std::string& before, const std::string& after, const ExportPlan&, const AppendResult&)`: every row of `before` still present and byte-identical; new rows decode (libcdj `pdb_parse`) to what the plan says; playlist entries in order; counters consistent with the page contents.
- Tests: add 1 track, 30 tracks (forces new pages in tracks/entries), 1000 tracks into the fixture; UTF-16 names land aligned (4 byte rule); repeat adds (second playlist into the result of the first); reuse of an existing artist/album/track; name clash handling; a table with no free room; result passes `verifyAppended` and libcdj `pdb_parse`; the old pages are byte-identical.

Validation: tests green; `parsePdb` of the output re-serialises exactly.

```sh
geany-progress done 4 -r src/trakcrate/pdb_append.cpp -r tests/test_trakcrate_pdb_append.cpp -w "Row/page placement rules are guesses until a deck loads the result" -w "Unknown header words of touched pages are copied or bumped, see pdb-append-notes.md"
```

## Phase 5: The `addToDrive` engine (files, backup, safe switch)

Files: `src/trakcrate/exporter.{h,cpp}` (or new `drive_add.cpp`), `tests/test_trakcrate_export.cpp` or new `tests/test_trakcrate_drive_add.cpp`.

- `struct AddOptions { fs::path drive; std::string playlist; bool allowUnanalysed; fs::path backupDir (default /var/tmp; files `rekordbox-edit-backup-export.<isodate>.pdb`, never deleted); stagingBase; ... }` (no settings, no template: the stick's own db is the base). `struct AddResult` like `ExportResult` plus `added`, `reused`, `backupVar`, `backupOnDrive`, `orphansCreated`, `cancelled`, `warnings`.
- Order exactly as the brief: (1) reject if `exportLibrary.db` exists, drive read only, `export.pdb` missing, **the playlist name already exists on the stick** (after `loadDriveDb`, still before any write); (2) copy `export.pdb` to staging (tmpfs) and `loadDriveDb` it, report each problem; (3) copy it to `/var/tmp/rekordbox-edit-backup-export.<isodate>.pdb` (never rotated or deleted) and compare it with the original; (4) gather the playlist, classify every track as existing (referenced, nothing copied) or new, free-space check on the stick for the new files only, stage ANLZ + artwork for the new tracks only; (5) copy new audio, then artwork, then ANLZ to the stick through `copyOne` (temp name, fsync, rename; nothing existing changed); record every path created; (6) `appendPlaylist`, `verifyAppended`; (7) write `export.pdb.new` on the stick with `copyOne`, `syncfs`; (8) point of no return: announce via progress, rotate the `.bak` files by rename, `rename(export.pdb, export.pdb.bak)`, `rename(export.pdb.new, export.pdb)` (undo the first rename if the second fails), `syncfs`; (9) re-read the stick's `export.pdb`, compare with the staged bytes; (10) result. Cancel allowed until (8). On error before (8) remove the files created in (5) when possible and the `.new` file; after (8) never roll back.
- Never touches: library files (open the library read only in the test and assert unchanged tree), user settings files on the stick, `USBMNG.DAT`, `exportExt.pdb`, `RBFLTR.DAT`.
- Tests with temp directories as fake sticks (the fixture db plus a few files): normal add; a playlist whose tracks are all already on the stick (only a playlist row and entries are added, no file is written); a mix of existing and new; a track whose path is taken by a different file (gets a new name); cancel at each stage; injected failures (`copyOne` failure through a read only directory, a full destination, a missing audio file); eject simulation (stop after each numbered step by a test hook, then check the stick still has a loadable db and `verify` passes except for orphans); concurrent adds to the same stick are refused; unanalysed track refused without `allowUnanalysed`, accepted with it; refuses a playlist name that already exists (exact and case-insensitive), refuses `exportLibrary.db`, an unreadable db and a db in a format the loader rejects without writing anything.

Validation: tests green, a stick tree diff after any failure shows only added files that are listed in the result.

```sh
geany-progress done 5 -r src/trakcrate/exporter.cpp -r tests/test_trakcrate_drive_add.cpp -w "Two renames as in the brief: a pull between them leaves export.pdb.bak + export.pdb.new, no export.pdb (repair by renaming)" -w "Orphan cleanup after an early eject is best effort"
```

## Phase 6: GUI menu and job integration

Files: `src/ui/trakcrate_panel.{h,cpp}`, `src/ui/trakcrate_jobs_strip.{h,cpp}` if the message needs a new state, new `src/ui/mounted_sticks.{h,cpp}` (the listing), `tests/test_trakcrate_panel.cpp`, `tests/CMakeLists.txt`.

- `QStringList/struct listRekordboxSticks()` using `QStorageInfo::mountedVolumes()`, `isReady()`, not read only, and existence of `PIONEER/rekordbox/export.pdb`; label = volume name + free space. Keep it in its own tiny class so it is testable with a fake mount list.
- Playlist context menu (near line 1635 of `trakcrate_panel.cpp`): add "Add to rekordbox >" with an `aboutToShow` handler that rebuilds the submenu each time (entry per stick, or a disabled "(no rekordbox pendrive mounted)"); only for `kind == "playlist"` and disabled when the playlist is empty.
- Choosing a stick starts `addToDrive` through the same `ExportJob`/`TrakcrateJobRunner`/jobs strip path as `exportDrive` (add a small `ExportJob::kind` so the strip row says "Add to <label>" and the finish message says "Added N tracks, M already on the stick; backup ..."). Overlap check with `destinationsOverlap`; no library writes, no `rebuildTree`.
- Unanalysed check first: count tracks without `anlz`; if any, a single `QMessageBox` "N of M tracks are not analysed (no beat grid or waveform). Add anyway?"; yes sets `allowUnanalysed`.
- Progress text announces the switch step ("Switching database...") before the point of no return. Errors go to the same failure notification as exports.
- Tests (panel test with a fake stick directory and a stub volume lister): menu contents with 0, 1, 2 sticks; selecting starts a job; unanalysed prompt appears and both answers work (use the existing dialog hooks of the panel tests); the library tree is unchanged after a run; two concurrent adds to different sticks both finish.

Validation: panel tests green; run the GUI by hand with the `run` skill against a fake stick folder.

```sh
geany-progress done 6 -r src/ui/trakcrate_panel.cpp -r src/ui/mounted_sticks.cpp -r tests/test_trakcrate_panel.cpp -w "Menu lists only mounted volumes with export.pdb; a stick mounted after the menu opened needs the menu reopened"
```

## Phase 7: Hardware test on real decks (gate before the CLI port)

Files: new `doc/trakcrate/hardware-test-add-to-rekordbox.md` (the checklist and a results table), fixes in `pdb_append.cpp` / `exporter.cpp` as the results demand. **Done by the user with the GUI feature of Phase 6; the agent prepares the checklist and reacts to the results.**

- Prepare before touching a deck: a **copy** of a real rekordbox stick (the user's `TEKNOPAUL64` or a smaller one) and one trakcrate-made stick; keep `rekordbox-edit-backup-export.*.pdb` and `export.pdb.bak` handy to restore.
- Checklist (record pass/fail and the deck model for each): (1) add a 1 track playlist; (2) add a 30 track playlist (forces new pages in tracks/entries); (3) add a playlist whose tracks are all already on the stick (no files written); (4) a mix of existing and new tracks; (5) refuses a playlist name that exists; (6) add to a stick the deck has played from (history rows present); (7) a track with non-ASCII artist and album (UTF-16 alignment); (8) eject during the audio copy, then plug into the deck (old library must be intact) and run the add again; (9) open the edited stick in rekordbox desktop if available; (10) settings files, `USBMNG.DAT`, `exportExt.pdb` byte identical before and after (`cmp`); (11) does the deck need `exportExt.pdb` rows for new tracks?
- Where a deck crashes or shows the wrong library, bisect as was done for the `Selvatica` case (smaller playlist, one table at a time) and fix the append rules; update `pdb-append-notes.md` with what the decks require.
- **Gate:** Phase 8 does not start until checks 1 to 4 and 8 pass on at least one deck.

Validation: results table filled in; `make` and `ctest` green after any fix.

```sh
geany-progress done 7 -r doc/trakcrate/hardware-test-add-to-rekordbox.md -r src/trakcrate/pdb_append.cpp -w "Index pages and page counters are only proven by decks: record every deck model tried"
```

## Phase 8: CLI (after the hardware gate)

**Hardware gate passed (2026-11-07).** What a fresh session needs to know, measured on the code as it is:

- The C side already has the page model: `tc_pdb_parse`, `tc_pdb_serialise`, `tc_pdb_table_rows`, `struct tc_pdb_page` (`src-cli/c/lib/rbpdb.{h,c}`), the row encoders (already UTF-16 aligned) and, since 2026-11-07, `tc_analyze_slot` / `tc_analyze_partition` (the export oracle test passes). Missing: the checker (`loadDriveDb`), the editor (`appendPlaylist`, `verifyAppended`) and the engine.
- `rbexport.c` (1508 lines) has `gather`, staging, `copy_one` and the ANLZ/artwork building as **static** functions with their own `struct gathered`; the engine needs them. That is the real cost of the port.
- Do it in three steps, each green on its own:
  1. **8a refactor**: split `rbexport.c` into an internal header `rbexport_internal.h` that exports `struct gathered`, `gather` (with an optional seed: the drive's database and root, as `GatherSeed` in `exporter.cpp`), `stage_track_files` (with an `only` mask), `copy_one`/`write_staged`, `pick_staging_base`. No behaviour change: `bash src-cli/test/run.sh` stays green, in particular `test_export_oracle.sh`.
  2. **8b editor**: `src-cli/c/lib/rbappend.{c,h}`: `tc_drive_db_load` (the rules of `pdb-append-notes.md`, same messages), `tc_append_playlist`, `tc_verify_appended`. Test: `test_append_oracle.sh` runs the C++ `appendPlaylist` (small program in `src-cli/test/oracle/cxx_append.cpp`, built from the GUI library like `cxx_export.cpp`) and the C one on `tests/fixtures/trakcrate/pdb-drive/*.pdb` and on libcdj's big `export.pdb`, same plan, and requires identical bytes.
  3. **8c engine and command**: `tc_add_to_drive` (the order of Phase 5, same refusals and messages, backup in `/var/tmp`, the two renames), `trakcrate-helper add-to-drive`, `src-cli/script/usr/lib/trakcrate/trakcrate-add` (options `--allow-unanalysed`, `-n/--dry-run`, `--backup-dir`, `--progress-fd`, `--yes`, formats like `export`), `test_add.sh` (fake stick made by `trakcrate export`: add, reuse, refusals, cancel, `verify` passes, library untouched) and an oracle comparison of whole sticks after an add (C helper vs `addToDrive` through a small `cxx_add.cpp`).
- Then update `doc/trakcrate/cli.md` ("Add to rekordbox (GUI only so far)") and mark this phase.

Files: `src-cli/script/usr/lib/trakcrate/trakcrate-add` (new), `src-cli/script/usr/bin/trakcrate` (dispatch), `src-cli/c/helper/cmd_add.c`, `src-cli/c/lib/rbappend.{c,h}` (port of `pdb_append`), `src-cli/c/Makefile`, `src-cli/test/test_add.sh`, `src-cli/test/oracle/cxx_add.cpp`, `doc/trakcrate/cli.md`.

- Command: `trakcrate add-to-drive [options] PLAYLIST TARGET` (`--allow-unanalysed`, `-n/--dry-run`, `--backup-dir DIR`, `--progress-fd N`, `--yes`), same output formats and exit codes as `export`/`sync`.
- Port `loadDriveDb`/`appendPlaylist` to C (the CLI cannot link the C++ library). Reuse `rbpdb.c` row encoders (already aligned for UTF-16) and its page code; keep the engine sequence of Phase 5. Make `rbappend` byte-identical to the C++ appender on the fixture: oracle test `test_add_oracle.sh` in the style of `test_export_oracle.sh` (the C helper's ANLZ folder rule was fixed on 2026-11-07: `tc_analyze_slot` / `tc_analyze_partition` in `rbpdb.c`, so the export oracle test passes again).
- Tests: `test_add.sh` (fixture library, fake stick from the Phase 1 fixture): adds a playlist, `trakcrate verify TARGET` passes, backup files exist, library untouched, refusals, dry run changes nothing.
- If the port proves too large, stop after the hash port, document the command as not yet available and tell the user.

Validation: `bash src-cli/test/run.sh` green including the new oracle.

```sh
geany-progress done 8 -r src-cli/c/lib/rbappend.c -r src-cli/script/usr/lib/trakcrate/trakcrate-add -r src-cli/test/test_add.sh -w "The ANLZ slot hash is already in the C helper (done 2026-11-07)"
```

## Phase 9: Documentation and recommendations

Files: `doc/trakcrate/rekordbox-db-output.md`, `doc/trakcrate/cli.md`, `doc/trakcrate/pdb-append-notes.md`, `ai-output/refactor-recommendations.md`, `TRAKCRATE-FORMAT.md` (only if a format fact changed), `doc/trakcrate/hardware-test-add-to-rekordbox.md` (final results).

- Document the feature, the switch order, the refusals, the `/var/tmp/rekordbox-edit-backup-export.<isodate>.pdb` backup rule (never deleted), the refusals (existing playlist name, `exportLibrary.db`), the decks it was tested on.
- Append to `ai-output/refactor-recommendations.md`: share the pdb model/row encoders/ANLZ path hash through libcdj (so GUI, CLI and any other tool use one implementation), one row encoder implementation instead of C and C++ copies, split `exporter.cpp` into gather/stage/copy/replace files, decode the index pages.

Validation: docs build nothing; `make` and `ctest` and `src-cli/test/run.sh` green.

```sh
geany-progress done 9 -r doc/trakcrate/rekordbox-db-output.md -r ai-output/refactor-recommendations.md -w "List only the decks that were actually tested"
```
