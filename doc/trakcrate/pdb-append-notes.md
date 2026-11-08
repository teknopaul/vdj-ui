# Adding to an existing rekordbox `export.pdb` (study)

Plan: `.planning/plans/add-to-rekordbox_PLAN.md`. Model and page layout: `pdb-write-notes.md`. Row layouts: `rekordbox-db-output.md`, `references/libcdj/doc-internal/rekordbox-db-format.text`.

Status: written from reading real files (2026-11-07). **Everything marked GUESS is unproven on a deck**; the plan's Phase 7 is the hardware test.

## Decisions (made by the user, 2026-11-07)

1. A playlist track that is already on the stick is **referenced, not copied**: no new track row, audio copy or ANLZ file. Match on the rekordbox path (case-insensitive: FAT), then size and audio hash.
2. A playlist whose name already exists on the stick: **refuse**, before anything is written.
3. `PIONEER/rekordbox/exportLibrary.db` present: **refuse**.
4. The stick's `export.pdb` is copied to `/var/tmp/rekordbox-edit-backup-export.<isodate>.pdb` first, **never deleted**.
5. Switch: write `export.pdb.new`, rename `export.pdb` to `export.pdb.bak` (rotating `.bak.2`, `.bak.3` by rename), rename `.new` to `export.pdb`. A pull between the renames leaves `.bak` + `.new`: repair by renaming `.new` to `export.pdb`.
6. Never changed: user settings files, `USBMNG.DAT`, `exportExt.pdb`, `RBFLTR.DAT`, the trakcrate library.

## What the real files look like

Surveyed: `TEKNOPAUL64` (rekordbox 6 stick, 423 pages, updated many times), `/transfer/cdj64` (465 pages), `rekordbox-clone-large`, `rekordbox-mini`, `RB_EXPORT_LOADS_FINE` (the last two are the committed fixtures `tests/fixtures/trakcrate/pdb-drive/`).

### Page chains and `emptyCandidate`

- Every table: `first` = index page (flag 0x64), then data pages along `next`, ending at the table's `emptyCandidate` (**ec**) page, which is a reserved empty page. `last` = last *data* page (`ec` is not part of it). In every file `chain_end == ec`.
- The ec page is either an all-zero blank page inside the file, or **beyond the end of the file** (page number >= file pages) with the file simply shorter: `TEKNOPAUL64` has 423 pages (0..422), tracks `last 422 ec 423`, playlist tree `last 407 ec 424`, header `next_unused = 425`. So `next_unused` is greater than every ec; the pages between the end of the file and `next_unused` are reserved.
- Chain pages are in ascending page-number order in every table of every file (new pages are always taken from the end).
- All 384 data pages of `TEKNOPAUL64` and the other files decode as "structured" with `pdb_writer.cpp` (rows back to back from heap offset 0, ascending offsets), so no real data page is kept raw by our model; raw pages are only blank pages and index pages.

### Data page header words

- Flag byte 0x1b: **0x34 exactly when some slot is deleted** (`present < slots`), 0x24 when every slot is present (verified for all pages of 5 files).
- `u10` (0x10): a write counter. Highest data-page `u10` is just below the header `sequence` (`TEKNOPAUL64`: 13736 vs sequence 13742; cdj64 18419 vs 18423). Each page written gets a new, larger value.
- `u20`/`numLarge` (0x20, 0x22): a fresh page has `u20 = 1`, `numLarge = slots - 1`; this holds for only about a third of the all-present pages of an updated stick (others have e.g. `u20 = 1, numLarge = 3` with 9 slots; pages with every row deleted have `u20 = slots, numLarge = 0`). Meaning unknown.
- Row group flag words: equal to the present mask on fresh pages, otherwise unknown.
- Rows are **not** in id order along a chain and ids have gaps (tracks: 2198 present rows, max id 2245). The playlist tree is rewritten by rekordbox on every export (444 deleted vs 120 present rows in `TEKNOPAUL64`); wholly deleted pages stay in the chain.
- Row subtypes seen: tracks 0x24, artists 0x60, albums 0x80 (never 0x64/0x84). No playlist folders (`is_folder` always 0, `parent 0`) in these sticks.
- A UTF-16 string starts on a 4 byte boundary of its row (`rekordbox-db-output.md`).

## Append algorithm (what `appendPlaylist` does)

For each table that gets rows (tracks 0, genres 1, artists 2, albums 3, labels 4, playlist tree 7, playlist entries 8, artwork 13), new rows are appended to the table; **existing rows are never rewritten or moved, index pages are never touched**.

1. Candidate page: the table's last data page (`lastPage`). It takes the row if `4096 - 40 - used - indexSpan(slots + 1) >= row size` and `slots + 1 <= 0x1FFF`. The row goes at the end of the heap (a new slot at the end; deleted slots are not reused: GUESS that this is fine).
2. Otherwise a new page: its number is the table's `emptyCandidate`. The old last page's `next` already points to it. The new page becomes the table's `lastPage`; the new `emptyCandidate` is `nextUnusedPage`, and `nextUnusedPage` grows by one. If the file is shorter than the new page number, blank pages are appended so that the file ends at the new data page (the other reserved ec pages below stay physical blanks: GUESS that decks accept a longer file; the real files are shorter than `next_unused` by 1 to 2 pages, so they cope with beyond-the-end ec pages).
3. Touched pages: `flags` stays 0x24 / 0x34 by the rule above (a page with deleted rows stays 0x34); the new slot's present bit is set and the group's row-flag word becomes just the new row's bit (the real sticks mark only the newest row of a page: `0x2` on a 2 row page, `0x200` on a 10 row page; the rest of the pages' flag words are left alone); `u10` = the next sequence number; header `sequence` = the last sequence number used. `u20`/`numLarge`: new pages `1` / `slots - 1`; an existing page that followed the fresh pattern (`u20 == 1 && numLarge == slots - 1`) keeps following it, any other page is left unchanged (GUESS).
4. Header: `nextUnusedPage` only grows; `unknown10` unchanged; table entries updated for `lastPage` and `emptyCandidate`.
5. Index pages stay as they are (checked 2026-11-07 against the 2195 track export and rekordcrate's index page: the entries list exactly the pages with flag 0x34, see pdb-small-tables.md; GUESS for the rest: strongest assumption: our own 2000 track exports have index pages that name only the first data page and load on decks, which suggests the deck follows the `next` chain. If a deck shows the old library only, the index pages are the first suspect).

Bytes 2 and 3 of a track, artist or album row are `slot * 0x20` (true for every row of every real page checked: 5,000+ rows), so the appender writes them for the slot the row lands in. A table that was empty (`last == first`) gets its first data page: its index page word at 0x2c, which holds `0x03ffffff` while the table is empty, becomes that page number (`rb-loads-fine` shows rekordbox doing exactly this for the genre table: `last 4`, new empty candidate 52, index body `[3, 4, ...]`).

Ids: new id = **max over present and deleted rows** + 1 (a deleted id may be referenced by history). Existing artists/genres/labels/artwork are reused by exact name (album also by artist id; names compared as stored: UTF-16 decoded). Playlist tree: `parent 0`, `is_folder 0`, `sort_order` = max root sort_order + 1, id = max + 1. Entries: `(entry index 1.., track id, playlist id)`. Artwork: the next `a<N>.jpg` number after the highest existing path in table 13; the folder (`/PIONEER/Artwork/00001/`) of the highest existing path while it holds fewer files than the stick's busiest folder, else the next folder (rule to confirm; the first version continues in the highest folder).

ANLZ folders: `analyzePathFor(filePath, used)` with `used` = analyze paths of every track row (present and deleted) and the ANLZ files found in the folder. The deck recomputes the folder from the path, so the rule must be exactly the hash of `usbanalz-hashing.md`.

## Sanity rules (`loadDriveDb` refuses when one fails)

- File size is a multiple of 4096, at most 256 MB, page size 4096, 20 tables in the header.
- Every table: `first` is an index page (flag 0x64), the chain from `first` terminates, stays in range (a page number at or past the end of the file is allowed only as the chain's end, i.e. the ec), is acyclic, and ends at `emptyCandidate`; `lastPage` is on the chain; `nextUnusedPage` is greater than every ec and than the file's page count minus one.
- Data pages decode as structured rows (not raw) and have the table's type in the page header; slot offsets lie inside the heap; `u10` of a page greater than the header `sequence` is only a warning (our own builder keeps the template pages' counters, which can exceed its sequence); the appender numbers new writes from the larger of the two.
- Ids are unique among present rows of a table; every playlist entry names a present track and a present playlist; every present track has readable strings and a `/PIONEER/USBANLZ/P…/…/ANLZ….DAT` analyze path whose slot digits match the hash of its file path (warning only: decks' own sticks are authoritative).
- Subtypes: tracks 0x24, artists 0x60, albums 0x80; anything else: refuse with the page and slot.
- Not an error: deleted rows, 0x34 pages, wholly deleted pages, history rows, ec pages beyond the file.

Refusals before reading the database: no `export.pdb`; `exportLibrary.db` present; the stick is read only; not enough free space.

## Implementation (Phases 3 and 4)

`src/trakcrate/pdb_append.{h,cpp}`: `loadDriveDb` (checks and indexes), `appendPlaylist` (edits a copy of the `PdbFile`, serialises, reloads the result through `loadDriveDb`, so an invalid result is never returned), `verifyAppended` (old rows and pages unchanged, new rows read back through libcdj, entries in order, flags match deleted rows). Track rows use the same template as full exports (`pdb_rows.h`), never a row copied from the stick. Tested on the committed fixtures, on libcdj's 2195 track export and, when present, on the real `TEKNOPAUL64` and `cdj64` sticks (three additions of 40, 100 and 160 tracks).

## The engine (Phase 5): `addToDrive` in `exporter.cpp`

Order: refuse (no `export.pdb`, `exportLibrary.db`, read only, playlist name already there) before any write; old database into memory and checked; copy to `/var/tmp/rekordbox-edit-backup-export.<UTC stamp>.pdb` and compare; gather the playlist against the stick (a track whose path has a row and the same audio, same size and first/last megabyte, is referenced; a file with the same audio but no row is reused; another file at the path gives the next name `... (2).ext`; folder spellings and artwork numbers continue the stick's); deck limits counted on the whole database; `appendPlaylist` and `verifyAppended` in memory; space check; ANLZ and artwork staged for new tracks only; audio, artwork, analysis files copied with `copyOne` (temporary name, fsync, rename); `export.pdb.new` written; **the switch**: rotate `.bak` files by rename, `export.pdb` to `export.pdb.bak`, `export.pdb.new` to `export.pdb`, sync, read back and compare. Cancel until the switch; before it an error or cancel removes what was added (`deleteOrphans`). Test hooks stop the run at each step to simulate an eject: at every step except between the two renames the stick still has the old, loadable database; between the renames `export.pdb.bak` and `export.pdb.new` are both there and one rename repairs it.

## Evidence from rekordbox itself (2026-11-07, `/var/tmp/rekordbox-added-to-trakcrate`)

A database written by trakcrate (4 tracks, `export.pdb.bak`) to which real rekordbox then added three tracks and a playlist (7 tracks, 2 playlists). Compared page by page:

- New pages take the old empty candidate numbers (3, 6, 9, 12, 22, 25, 36, 52: blank pages inside the file); the new empty candidates 53 to 60 are beyond the end of the file (still 53 pages); `next_unused_page` 53 to 61. Same as our rule. Rekordbox put 2 of the 3 new rows of each table into the existing last page and the third into a new page (even where the old page had room); we fill the last page first. Both are valid chains.
- A page that only gains rows keeps its `u10`; new pages get new counters (14 to 21); the header sequence ended at 23 (past the highest counter, 21). We now do the same (`sequence = last counter + 1`).
- `u20 = 1`, `numLarge = slots - 1` followed on every page that had it; the row-flag word covers every slot (`0x3f` on a 6 row page, `0x7` on a 3 slot page with a deleted slot): we OR in the new row's bit (the older real stick with a flag word marking only the newest row is the other convention).
- **Index pages**: only the tracks and playlist-tree index pages changed, and only because those tables gained pages with deleted rows (flag 0x34). The index page body is a count (`0x1fff0000 | count` at 0x38) followed by `(page << 3) | 3` entries, one per 0x34 page of the table. Checked on two big sticks: the count equals the number of 0x34 pages in every table (tracks 44 and 253, playlist tree 6, ...). A table that only gained 0x24 pages (genres, artists, albums, entries, artwork) kept an **identical** index page. So leaving index pages alone is right while we never create a page with deleted rows, which `appendPlaylist` never does.
- Rekordbox rewrote the playlist tree (old playlist row deleted and written again on the new page, flag 0x34): not needed for an addition.
- Track rows written by rekordbox have the same layout as ours (same string offsets, the `2406 + id` unknown field continuing, a UTF-16 path at row offset 252 for `Palo.mp3`). The ANLZ folder it chose for all 7 tracks equals our hash rule (including `P02B/0002BDAF` for `Miniteca Selvática/Palo.mp3` and `P049/00010C89` for the Afrovox track). Artwork continued as `a4.jpg`, `a5.jpg`, `a6.jpg` in `Artwork/00001`.

## Open questions only a deck can answer

1. Index pages untouched (above).
2. Appended rows vs `exportExt.pdb`: does a new track need rows there?
3. Do decks need the physical blank pages between the old end of the file and a new ec page?
4. The row group flag words and `u20`/`numLarge` of appended pages.
5. Does rekordbox desktop accept the edited stick (Windows/Mac), and does it rewrite it cleanly?

## The command line port (Phase 8, 2026-11-07)

`trakcrate add-to-drive PLAYLIST TARGET` (`doc/trakcrate/cli.md`, "Add to rekordbox") does the same as the GUI: `src-cli/c/lib/rbappend.c` is `pdb_append.cpp` (same checks, messages, row placement and counters) and `src-cli/c/lib/rbadd.c` is `addToDrive` (same steps and refusals). The C editor gives byte identical databases to the C++ one on the committed fixtures, libcdj's 2195 track export and the real `TEKNOPAUL64` and `cdj64` sticks, for one plan and for plans added one after the other (`src-cli/test/test_append_oracle.sh`), and a whole stick after an add is byte identical to the GUI's apart from the artwork JPEGs (`test_add_oracle.sh`). Differences that do not change the output: the C loader keeps its name lists sorted instead of in maps, and reports duplicate ids after the pass in a different order of messages.
Not covered by a test: cancel by signal (the C++ engine's cancel is tested in `test_trakcrate_drive_add.cpp`; the C engine shares the structure) and the real-deck behaviour of the C output, which is the same bytes as the C++ output the hardware checklist tested.
