# TRAKCRATE-UI: a GUI to build rekordbox pendrives that real players accept

Goal: vdj-ui gets a **trakcrate** pane that imports a folder of bare audio files into the trakcrate master database, analyses them (beat grid,
waveforms), and **exports a rekordbox-format pendrive** (`PIONEER/` + `Contents/`). The first milestone is the *gate*: a real CDJ/XDJ accepts and plays
an export made by this GUI (Phase 9). Editing features (rating, comment, cuemarks, beat grid nudge, artwork editor, scripts) come after the gate.

Read first: `doc/trakcrate/` (all of it; the design is there). The ones that matter per phase are named in each phase. The CLI (`cli.md`) is **out of scope
for now** (user decision: GUI first).

## Decision to confirm (deviation from the docs)

`doc/trakcrate/gui.md` says the GUI calls the CLI and links no trakcrate library. The user has now said to build the GUI first and not worry about the CLI.
So for this plan the logic lives in a **GUI-independent module `src/trakcrate/`** (plain C++17, `std::filesystem`, no Qt types, no Qt includes), called directly by the
`src/ui/trakcrate_*` classes. Keeping it Qt-free makes it movable into the C helper / command line later without a rewrite. If the user prefers otherwise,
stop after Phase 1 and ask. Do not add a CLI in this plan.

## Ground rules (read before every phase)

- C++17 / Qt in `src/ui`, plain C++17 in `src/trakcrate`. Match surrounding style (4 spaces, short `//` comments, `QStringLiteral` in Qt code).
- Build **from `build/`**: `cd build && cmake . && make -j8`. **Never `cmake ..`** (stale in-source `CMakeCache.txt`). Tests: QtTest, one executable per file, registered
  in `tests/CMakeLists.txt` (pattern: `tests/test_beat_clock.cpp`); Qt widget tests run with `QT_QPA_PLATFORM=offscreen`. Pure-logic tests may use QtTest without a QApplication.
- New core module: a static library target `vdj-trakcrate` in `src/CMakeLists.txt`, mirror how `vdj-dbserver` is declared. libcdj headers (`<cdj/pdb.h>`, `<cdj/anlz.h>`) are installed;
  the libcdj source tree is `references/libcdj` (symlink) and is built into the app for NFS (`src/CMakeLists.txt`). **Never run git in libcdj; never commit in this repo.** Changes to libcdj
  need the user's agreement first: put new code in `src/trakcrate/` instead.
- Do not run the GUI. Verify with unit tests, a clean build, and (for exports) re-reading output with libcdj's `pdb_parse_file` / `anlz_open_file`.
- Never write to a real pendrive from code in this plan. Exports go to a directory the user picks; hardware tests are done by the human (Phase 9).
- Fixtures: real data is in `references/libcdj/rekordbox/` (7-track drive: `export.pdb`, `exportExt.pdb`, `USBANLZ/…`, settings files; **no audio**) and `references/libcdj/src/test/fixtures/`
  (large `export.pdb`, ANLZ pairs). Tests must use copies in `tests/fixtures/trakcrate/`, never `/media/...`. The big pendrive `/media/teknopaul/TEKNOPAUL64` may or may not be mounted; never depend on it.
- Unknown format bytes are **copied from real data (templates)**, never invented silently. Every byte we make up goes in `doc/trakcrate/UNKNOWN_BYTES.md` with the offset, the value used and the reason.
- Wording: the feature is **trakcrate**; "rekordbox" only names the third-party format. Keep UI strings neutral.

## Facts about the existing code (verified, do not re-derive)

- `src/audio/audio_decoder.{h,cpp}`: `vdj::decodeAudioFile(path, &err)` → `DecodedAudio` (16-bit interleaved stereo 44.1 kHz, `frames()`, `lengthMs()`), uses ffmpeg
  (`libavformat libavcodec libswresample libavutil` via `pkg_check_modules(AUDIO …)` in `src/CMakeLists.txt`). It uses Qt types (`QString`, `QVector`): the core module needs its own std-only probe/decoder wrapper over the same ffmpeg libs.
- libcdj `pdb.h`: row parsers (`pdb_parse_file`, `pdb_parse_artists/albums/…/playlists/artwork`, `pdb_foreach_row`), `pdb_track_t` (fields: id, sample_rate, file_size, artwork_id, key_id, label_id,
  remixer_id, bitrate, track_number, bpm_x100, genre_id, album_id, artist_id, disc_number, play_count, year, sample_depth, duration_secs, color_id, rating, title, filename, file_path, comment,
  date_added, release_date, mix_name, analyze_path). Table types: 0 tracks, 1 genres, 2 artists, 3 albums, 4 labels, 5 keys, 6 colors, 7 playlist tree, 8 playlist entries, 13 artwork. **Only reading exists.**
  PDB header: page size 0x1000, `num_tables` 20, table entry `{type, empty_candidate, first_page, last_page}` at offset 28 + 16·i, page 0 = header; data page header 40 bytes, row index at page end in groups of 16
  (`pdb.c:151-180`); strings: short ASCII, `0x40` long ASCII, `0x90` UTF-16LE (`pdb.c:26-50`); track row = 94 fixed bytes + 21 u16 string offsets (`pdb.c:79`).
  Page flag values seen in the 7-track drive: `0x00` (10 free pages), `0x24`, `0x34`, `0x64` (meaning undocumented). Format notes: `references/libcdj/doc-internal/rekordbox-db-format.text`.
- libcdj `anlz.h`: `anlz_open_file/mem`, `anlz_find_tag`, `anlz_beatgrid`, `anlz_cues`, `anlz_waveform`, `anlz_track_open(root, analyze_path)`. Writer for tests only: `references/libcdj/src/anlz/anlz_fake.c`
  (`anlz_fake_begin/add_ppth/add_pqtz/add_pqt2/add_pcob/add_pco2/add_wave/finish`, **not installed**, compile it into the test/core target from the libcdj source tree like `src/nfs` is).
  Format: `references/libcdj/doc-internal/rekordbox-anlz-format.text` (PQTZ 8 bytes/beat; PQT2 = µs fractions; PWAV 400 cols 5-bit height + 3-bit whiteness; PWV2 100 cols; PWV3 150/s;
  PWV5 150/s u16 BE: R3 G3 B3 height5 + 2; PWV4 1200 × 6 bytes only roughly understood; PVBR/PVB2 raw).
- Settings files and CRC: `doc/trakcrate/settings-files.md` (CRC-16/XMODEM over bytes `[0x68, len-4)` stored LE at `len-4`, verified on both files).
- `src/export/pendrive_registry.{h,cpp}`: `PendriveRegistry`, `VirtualPendrive{name, folder}`, `isRekordboxFolder(folder)` (checks `PIONEER/rekordbox/export.pdb`). vdj-ui already **exports a virtual pendrive's folder to the decks over NFS**
  (`src/nfs`, `src/network/link_export.*`, `src/ui/virtual_pendrives_dialog.cpp`): this is the safe way to test an export on real players without a USB stick (Phase 9, step 1).
- UI patterns: docks are `QDockWidget`s created lazily in `MainWindow` (`rekordboxDock()` ~ line 1996, `recordDock_`, `debugDock_`) with a checkable action; panel classes in `src/ui/` (e.g. `rekordbox_mode_panel.cpp`);
  waveform drawing in `src/ui/waveform_widget.*` (reads PWV2/PWV3/PWV5, beat overlay, markers), `track_analysis.*`; track list is `track_browser.*`.

## Layout of the new code

```
src/trakcrate/                 plain C++17, std only (+ ffmpeg in audio_probe.cpp, libcdj readers in verify code)
    library.{h,cpp}            the master database on disk (read/write, atomic)
    track.{h,cpp}              Track struct + meta.txt / source.txt / beatgrid.txt / cuemarks.txt formats
    sha256.{h,cpp}  natsort.{h,cpp}  fsutil.{h,cpp}  (atomic write, tmpdir)
    audio_probe.{h,cpp}        tags, duration, format, embedded picture via ffmpeg
    importer.{h,cpp}           import a folder: master + playlist
    analysis_beats.{h,cpp}  analysis_wave.{h,cpp}  loudness
    pdb_writer.{h,cpp}  anlz_writer.{h,cpp}  settings_files.{h,cpp}  artwork_export.{h,cpp}
    exporter.{h,cpp}           staged export (tmpfs build, verify, single write)
src/ui/trakcrate_*.{h,cpp}     panel, models, dialogs, job runner
tests/test_trakcrate_*.cpp     one per module
tests/fixtures/trakcrate/      copies of real data used by tests
```

---

## Phase 1: Library model and file formats (no UI)

Read: `doc/trakcrate/trakcrate-db-format.md`, `artwork.md`, `cuemarks.md`, `vocals.md` (only the format part), `import-from-rekordbox.md` (the `source.txt` part).

1. Add the `vdj-trakcrate` static library target and `tests/` target; helpers: `fsutil` (`atomicWrite(path, bytes)` temp+rename in the same dir, `makeTempDir`), `sha256`, `natsort` (case-insensitive natural compare, locale independent).
2. Define the on-disk formats with a writer and a parser each, tolerant of comments (`#`), unknown keys preserved:
   - `meta.txt`: `key: value` lines (title, artist, album, genre, label, key, bpm, year, track_number, rating 0-5, color, comment, remixer, mix_name, original_filename).
   - `source.txt`: `origin:` per field group (meta, beatgrid, cues, waveform, artwork: `generated|rekordbox|edited`), `imported_from`, `imported_at`.
   - `beatgrid.txt`: header (`bpm`, `first_beat_us`) + optional explicit beats; `cuemarks.txt` per `cuemarks.md`; `vocals.txt` parse-only (round trip, no analyser).
3. `Library`: `open/create(dir)` (creates `track-master-copy/`, `playlists/`, `user-settings/`), `listFolders()`, `listTracks(folder)`, `loadTrack(dir)`, `saveMeta/Source/…`, `createPlaylist(name)`, `playlistTracks(name)` (symlinks `000_`… sorted), `addToPlaylist(name, trackDir, position)`, `renumberPlaylist`. Prefix width from count (≥3 digits).
4. Tests (`tests/test_trakcrate_library.cpp`): round trip of every format incl. unknown keys and comments; atomic write leaves no temp on success; playlist ordering with 12 and 1200 entries; natural sort (`track2` < `track10`); path safety (no `..`, no separators in names, sanitise folder names).

Done when: `make` clean, test passes, no UI changes.

```sh
geany-progress done 1 -r src/trakcrate/library.h -r src/trakcrate/track.h -r tests/test_trakcrate_library.cpp -w "Qt-free module in src/trakcrate: confirm this is acceptable vs doc/trakcrate/gui.md (GUI calls CLI)"
```

## Phase 2: Import a folder of bare audio files

Read: `doc/trakcrate/cli.md` section "Importing a folder of bare audio files" (behaviour spec; implement as a function, not a command).

1. `audio_probe`: with ffmpeg (`avformat_open_input`): duration, sample rate, channels, bit depth (from codec/sample fmt), bitrate, container (`mp3`/`wav`), tags (title, artist, album, genre, date/year, track, comment), embedded picture stream (attached_pic) bytes. Fallback title = file name without extension. Must not crash on garbage files (return an error).
2. `importer`: `importFolder(Library&, dir, Options)` → report. Options: recursive, playlistName (default: base name of dir), noPlaylist, copy|move|link, dedupe (content SHA-256), append|replace, analyse flag (ignored until Phase 4), dryRun. For each `.mp3`/`.wav` (case-insensitive): create `track-master-copy/<folder>/<track>/` with audio, `meta.txt`, `source.txt` (`origin: generated`), `artwork.jpg` from the embedded picture (written once, never overwritten), name collisions get a numeric suffix, original file name kept in `meta.txt`. Playlist symlinks alphabetical (natural, case-insensitive) with `000_` prefixes. Skipped/failed files collected in the report (path + reason). Idempotent re-run.
3. Progress callback + cancel flag (atomic bool) so the UI can run it on a worker thread.
4. Tests (`tests/test_trakcrate_import.cpp`): generate tiny WAV files in a temp dir (write a minimal PCM WAV writer in the test; an mp3 case only if `ffmpeg` can encode one in-process, otherwise use a small committed fixture `tests/fixtures/trakcrate/tiny.mp3`); check master layout, alphabetical playlist, dedupe, collisions, garbage file skipped, dry run writes nothing, re-run changes nothing, cancel stops cleanly.

```sh
geany-progress done 2 -r src/trakcrate/importer.cpp -r src/trakcrate/audio_probe.cpp -r tests/test_trakcrate_import.cpp -w "audio_probe duplicates some of src/audio/audio_decoder.cpp's ffmpeg use on purpose (Qt-free)"
```

## Phase 3: The trakcrate pane (read-only browser + import dialog)

Read: `doc/trakcrate/gui.md` (layout, behaviour requirements), the `rekordboxDock()` code in `src/ui/main_window.cpp`.

1. `src/ui/trakcrate_job.{h,cpp}`: a small runner that executes a `std::function` on a `QThread`/`QtConcurrent`, with progress/cancel signals and one-job-at-a-time per library. Never touch widgets from the worker.
2. `trakcrate_panel`: dock "trakcrate" via `trakcrateDock()` + checkable View action (same pattern as `rbDock_`); left folders/playlists tree, centre track table (title, artist, album, BPM, duration, origin, flags), right details (artwork preview, meta fields read-only). Library chooser (open / create, remembered in the config). Model/view (`QAbstractItemModel`/`QStandardItemModel` subclass with lazy loading), filter box, sort.
3. `trakcrate_import_dialog`: folder, playlist name (default folder name), recursive, copy/move/link, dry run preview; runs the import job with a progress bar and a result report.
4. File watching (`QFileSystemWatcher` on the library dirs, debounced) and a Refresh button.
5. Tests: model test with a temp library from Phase 2 helpers (rows, sort, filter); an offscreen widget test creating the panel and importing a temp folder.

```sh
geany-progress done 3 -r src/ui/trakcrate_panel.cpp -r src/ui/trakcrate_import_dialog.cpp -r src/ui/main_window.cpp -w "dock follows the rbDock_ pattern; no editing yet"
```

## Phase 4: Analysis: beat grid, waveforms, loudness

Read: `doc/trakcrate/beat-grid.md`, `waveforms.md`.

1. Std-only decode wrapper in `src/trakcrate/audio_decode` (ffmpeg → float/int16 mono+stereo 44.1 kHz).
2. `analysis_beats`: onset envelope (~200 Hz), autocorrelation over 70-180 BPM, octave resolution with ID3 BPM prior if present, least squares grid fit, phase search, confidence; low confidence → empty grid. Output `beatgrid.txt` (`origin: generated`, bpm 2 decimals, `first_beat_us`).
3. `analysis_wave`: base series at 150 columns/s from the audio; **height = fixed mapping, 0 dBFS = 31, never per-track normalisation** (`waveforms.md`); 3-band colour/whiteness; derive PWV3, PWV5, PWAV (400 cols), PWV2 (100 cols), PWV4 (1200 cols; leave as template-based placeholder for now, see Phase 7). Store as binary `waveform.bin` (or one file per tag) in the track folder + a PNG preview (`waveform.png`, QImage is allowed in the UI layer only; the core writes raw data).
4. `loudness`: peak and RMS in dBFS, in `meta.txt` (to show which tracks are not normalised).
5. Hook into the importer (`analyse` option) and the panel (context menu "Analyse", job with progress). Skip fields whose origin is `rekordbox`/`edited` unless forced.
6. Tests (`tests/test_trakcrate_analysis.cpp`): synthetic click tracks at 90/120/128/140 BPM with known phase → BPM within 0.05, phase within 10 ms; silence/noise → empty grid; waveform height of a −20 dBFS sine is much lower than a −1 dBFS sine (proves no normalisation); deterministic output (run twice, identical bytes).

```sh
geany-progress done 4 -r src/trakcrate/analysis_beats.cpp -r src/trakcrate/analysis_wave.cpp -r tests/test_trakcrate_analysis.cpp -w "height curve is uncalibrated against rekordbox: needs the user's Beatport vs unnormalised sample data" -w "PWV4 layout only roughly understood"
```

## Phase 5: PDB writer, part 1: structure and byte-identical re-emit

Read: `references/libcdj/doc-internal/rekordbox-db-format.text`, `references/libcdj/src/nfs/pdb.c` (all of it), `doc/trakcrate/rekordbox-db-output.md`, `UNKNOWN_BYTES` rules above. This is the riskiest phase: go slowly, keep notes in `doc/trakcrate/pdb-write-notes.md`.

1. Fixture: copy `references/libcdj/rekordbox/PIONEER/rekordbox/export.pdb` (167,936 bytes) to `tests/fixtures/trakcrate/pdb/export7.pdb` (also `exportExt.pdb` if useful).
2. `pdb_writer` data model: `PdbFile{header, tables[20]{type, empty_candidate, first, last, pages[]}}`, `Page{index, type, next, flags, header bytes, rows[] (raw bytes + presence flags), raw tail}`. A **parser into this model that keeps every byte** (unknown header fields, free pages, row index groups, padding) and a serialiser.
3. Goal of the phase: `serialise(parse(file)) == file` byte for byte for the 7-track `export.pdb`. Work out and document in `pdb-write-notes.md` (and UNKNOWN_BYTES where unresolved): page header fields (flags `0x24/0x34/0x64`, free-space counters, row count packing 13 bits + 3 bits as in `pdb.c:159`), row index group layout (16 offsets + presence bitmap, 0x24 bytes), the "next page" chain and the header's `next_unused_page` and sequence fields.
4. Then the **semantic re-emit**: rebuild each page from decoded rows (`pdb_foreach_row`) instead of raw copies, and compare. Differences are listed per page; goal byte-identical or, if impossible, identical after normalising named unknown fields (document which).
5. Tests: raw round trip; semantic re-emit; every output page re-parses with `pdb_parse_file` to the same track/artist/album counts as the original (7 tracks).

```sh
geany-progress done 5 -r src/trakcrate/pdb_writer.cpp -r doc/trakcrate/pdb-write-notes.md -r tests/test_trakcrate_pdb_roundtrip.cpp -w "if byte-identical re-emit is not reached, STOP and report the differing pages to the user before Phase 6"
```

## Phase 6: PDB writer, part 2: build a PDB from the library

Read: `pdb-write-notes.md` from Phase 5, `doc/trakcrate/rekordbox-db-output.md` (tables), `import-from-rekordbox.md`.

1. Row encoders for tracks, genres, artists, albums, labels, keys, colors, playlist tree, playlist entries, artwork (table 13), inverse of the readers in `pdb.c` (use the field offsets there; unknown fixed bytes copied from a **template track row** taken from the 7-track drive). String encoder picks short ASCII / long ASCII / UTF-16LE.
2. `buildPdb(const ExportPlan&)` → bytes: assigns ids (artists, albums, genres, labels, artwork de-duplicated by content hash; stable first-seen order), lays out pages per table (multi-page when a table overflows), fills the header (20 tables including the empty ones copied from the template), sets `file_path` (`/Contents/<…>`), `analyze_path` (`/PIONEER/USBANLZ/Pxxx/yyyyyyyy/ANLZ0000.DAT`; rule unknown: document the scheme used and mark it in UNKNOWN_BYTES), playlists from the pendrive symlink folders.
3. Verification function `verifyPdb(bytes)`: parse with libcdj (`pdb_parse`, playlists, artwork) and compare against the plan (counts, titles, ids, paths, string round trip including non-ASCII).
4. Tests: 0, 1, 7 and 2000 track plans (multi-page); unicode titles; very long strings; empty playlist; verify passes; the 7 track plan built from the fixture's own metadata re-parses equal to the original rows.

```sh
geany-progress done 6 -r src/trakcrate/pdb_writer.cpp -r src/trakcrate/pdb_build.cpp -r doc/trakcrate/UNKNOWN_BYTES.md -w "ANLZ directory naming and several track-row fields are invented from templates: see UNKNOWN_BYTES.md"
```

## Phase 7: ANLZ writer, settings files, artwork export

Read: `references/libcdj/doc-internal/rekordbox-anlz-format.text`, `references/libcdj/src/anlz/anlz_fake.{h,c}`, `doc/trakcrate/settings-files.md`, `artwork.md`.

1. `anlz_writer`: build `ANLZ0000.DAT` (PMAI, PPTH, PVBR, PQTZ, PWAV, PWV2, PCOB×2 hot+memory) and `ANLZ0000.EXT` (PMAI, PPTH, PWV3, PCOB×2, PCO2×2, PQT2, PWV5, PWV4) from the track's analysis. Compile `anlz_fake.c` from the libcdj tree into the core target and wrap it, or reimplement the same layout; unknown tags (PVBR body, PQT2 `0x2c` word, PWV4) from templates taken from the real 7-track files (`tests/fixtures/trakcrate/anlz/`). Empty hot cue lists when none; the first hot cue convention (cue A at the first beat, marker 150) is the cue module's job later.
2. Round trip tests: take each of the 7 real ANLZ files, decode with libcdj, re-write through our writer using the same data, compare **tag by tag** (and whole-file if achievable); `anlz_open_mem` accepts all outputs; `anlz_track_beatgrid`/waveform return equal data to the input.
3. `settings_files`: ship `DEVSETTING.DAT` and `MYSETTING.DAT` as templates (`resources/trakcrate/` embedded in `resources.qrc` or installed data dir) copied verbatim from the 7-track drive; verify CRC on load (XMODEM rule) and reject damaged templates; do **not** ship `djprofile.nxs` (contains the user's name) or `RBFLTR.DAT` (device specific) in this phase; an option list "optional files" is added in Phase 8.
4. `artwork_export`: for each distinct artwork (content hash) re-encode to baseline sRGB JPEG at the size measured in `artwork.md` if known, otherwise 240×240 and 80×80 `_m` (flag as unverified in UNKNOWN_BYTES), path `PIONEER/Artwork/NNNNN/aID.jpg` with the 20-ids-per-dir rule from `artwork.md` (unverified). Uses `QImage` therefore lives in `src/ui/` or a Qt-using helper file; keep the interface plain (bytes in, bytes out).

```sh
geany-progress done 7 -r src/trakcrate/anlz_writer.cpp -r src/trakcrate/settings_files.cpp -r tests/test_trakcrate_anlz_roundtrip.cpp -w "artwork sizes and Artwork directory numbering are unverified: need samples from the big pendrive"
```

## Phase 8: Staged export and the export dialog

Read: `doc/trakcrate/rekordbox-db-output.md` ("Design: tmpfs build, single write"), `src/export/pendrive_registry.*`, `src/ui/virtual_pendrives_dialog.cpp`.

1. `exporter`: input = library + playlist(s) (a trakcrate "pendrive") + destination dir. Steps: (a) refuse a non-empty destination; (b) check destination free space and tmpfs free space; (c) build the tree in a tmpfs staging dir (`/dev/shm/trakcrate-export-XXXXXX`, fall back to `$XDG_RUNTIME_DIR`, else a temp dir with a warning): `PIONEER/rekordbox/export.pdb`, `USBANLZ/…`, `Artwork/…`, settings files, `Contents/<…>` audio (copy; large audio files stream, they do not need to be in RAM: stage metadata in tmpfs, stream audio at write time); (d) verify the staged tree with libcdj parsers; (e) write to the destination **sequentially, each file once, large buffers, audio first, `export.pdb` last**, `fsync` each file and the directories, `syncfs` once; (f) re-read the destination and compare SHA-256 with the staged copy; (g) write `SHA256SUMS` + a manifest of what we invented. Cleanup of the staging dir always. Never writes outside the destination.
2. **Variant switches** for hardware bisecting (Phase 9): include/exclude `exportExt.pdb`, `RBFLTR.DAT`, `USBMNG.DAT`, `djprofile.nxs`, `*SETTING.DAT`, `PVBR`, `PWV4`/`PWV5`; copy-from-template options for the optional files (taken from a real drive the user points at). Recorded in the manifest.
3. `trakcrate_export_dialog`: choose pendrive (playlist), destination folder (pre-fill removable media under `/media/<user>`, show free space), variant checkboxes under an "Advanced / compatibility testing" expander, progress, verify result, and a button **"Register as virtual pendrive"** (adds the folder to `PendriveRegistry` so the existing NFS export can serve it to the decks: the safest first test).
4. Tests: export a 3-track plan to a temp dir, check layout, `SHA256SUMS`, libcdj parse of the output (`PendriveRegistry::isRekordboxFolder`), refusal of a non-empty dir, failure injection (destination full: use a tiny tmpfs or a size-limit hook) leaves no partial `export.pdb`; variants honoured.

```sh
geany-progress done 8 -r src/trakcrate/exporter.cpp -r src/ui/trakcrate_export_dialog.cpp -r tests/test_trakcrate_export.cpp -w "single write means sequential, each file once; not literally one write(2)" -w "audio is streamed, not staged in RAM"
```

## Phase 9: Hardware gate (human-run) and recording the results

This phase is mostly the user's. The agent prepares the protocol and records what the user reports; it must not claim success without the user's report.

1. Write `doc/trakcrate/hardware-test.md`: a checklist with tables to fill in. Steps, in order:
   1. Import a few of the user's own tracks (include one non-normalised and one Beatport-normalised), analyse, export a 3-track pendrive to a local folder.
   2. **Register it as a virtual pendrive** and export it to the decks over the network (existing feature): do the decks list it, show titles/artists/BPM/artwork, show waveforms, load and play, show the beat grid?
   3. Export to a real FAT32/exFAT stick (the user's spare) with all files; insert in each player model; same questions.
   4. Bisect: repeat with variants (without `exportExt.pdb`, `USBMNG.DAT`, `RBFLTR.DAT`, settings files, `PVBR`, `PWV4`, `PWV5`) to learn which are required; record per model.
   5. Sanity: the same stick still works when also read by the user's real rekordbox software, if available.
2. A one page "what to send back" section: for failures, the player's message, model and firmware, and the export folder (without audio) zipped for diffing against a real drive.
3. After the user returns results, update `doc/trakcrate/rekordbox-db-output.md` and `UNKNOWN_BYTES.md` (what is required, what is optional, what was wrong) and fix writer bugs found. This phase ends when the user confirms the **gate: a real player accepts and plays a track from a trakcrate export**.

```sh
geany-progress done 9 -r doc/trakcrate/hardware-test.md -r doc/trakcrate/UNKNOWN_BYTES.md -w "GATE: do not start Phase 10 until the user confirms a real player accepts the export"
```

---

## After the gate

## Phase 10: Load a trakcrate track into the player; rating and comment

Read: `doc/trakcrate/track-editor.md`, `src/ui/player_panel.h` (`loadTrack(const TrackInfo&)`), `src/ui/track_details.*`, `waveform_widget.*`.

1. Context menu / drag from the trakcrate table: "Load into deck N" calls `PlayerPanel::loadTrack` with the master audio, and hands the track folder to a `TrackEditor` panel (new dock "Track" that follows the selected deck).
2. The player shows the master's grid, waveform and artwork (reuse `WaveformWidget`'s data path by converting the trakcrate waveform/beat data to the structures `track_analysis` produces).
3. `StarRatingWidget` (0-5, click again to clear, keys 0-5) and a comment field: written atomically to `meta.txt` (`origin: edited` for meta), undoable (`QUndoStack`).
4. Tests: rating/comment write + undo; loading sets the editor state; offscreen.

```sh
geany-progress done 10 -r src/ui/trakcrate_track_editor.cpp -r src/ui/star_rating_widget.cpp -r tests/test_trakcrate_editor.cpp
```

## Phase 11: Cuemarks and hot cues (editing and export rendering)

Read: `doc/trakcrate/cuemarks.md`, `track-editor.md` (cuemarks with the mouse).

1. `WaveformWidget` edit mode (toolbar toggle): insert at click/`M` (snap to beat by default, Alt = free), drag to move, delete; signals `cuemarkAdded/Moved/Removed`; hit testing; editor writes `cuemarks.txt`; assign A/B/C from the context menu.
2. Export: `cuemark_render` writes the **white spike** (height 31 / whiteness 7; PWV5 7,7,7; 2 columns wide on detail tags, 1 on the previews; PWV4 untouched) into the exported copies of PWAV/PWV2/PWV3/PWV5, and hot cues A-C into PCOB/PCO2. The master waveform is never modified. Option for green on PWV5 (`--cuemark-colour green` equivalent), off by default.
3. Tests: rendering puts spikes at the right columns, clamps at the ends, is deterministic; the exported ANLZ re-parses with the expected cues.
4. Hardware: extend `hardware-test.md` with the cuemark checks (spike visible, hot cues A-C, quantize set-cue offset, memory cue limit).

```sh
geany-progress done 11 -r src/ui/waveform_widget.cpp -r src/trakcrate/cuemark_render.cpp -r tests/test_trakcrate_cuemarks.cpp -w "needs a deck test for how the spike looks"
```

## Phase 12: Beat grid nudging

Read: `track-editor.md` (Beat grid nudging), `beat-grid.md`.

1. Nudge buttons/keys with 10 / 1 / 0.1 ms steps, grid-drag mode, "set beat 1 here", numeric offset readout, revert, `QUndoStack` merging; writes `beatgrid.txt` (`origin: edited`); confirm the first time on a grid whose origin is `rekordbox`; cuemark and vocal beat columns recomputed from their ms values.
2. The deck uses the new grid immediately (check `beat_source` / `deck_phase` handling; ask the user if the hand-over to sync is unclear).
3. Tests: nudge arithmetic, undo merge, file content, dependent files updated.

```sh
geany-progress done 12 -r src/ui/trakcrate_track_editor.cpp -r src/ui/waveform_widget.cpp -r tests/test_trakcrate_nudge.cpp
```

## Phase 13: Artwork view and "open in editor"

Read: `track-editor.md` (Artwork), `artwork.md`.

1. `ArtworkView` (shows `artwork.jpg`, click to enlarge, replace by picker/drag and drop, remove, re-extract), one level of backup in `.trakcrate-history/`.
2. `artwork_launcher`: list installed apps for `image/jpeg`/`image/png` by scanning `.desktop` files (`/usr/share/applications`, `~/.local/share/applications`), remembered choice, custom command run without a shell (`QProcess::startDetached(program, args)`); `QFileSystemWatcher` + mtime poll to reload after the editor saves (re-add the watch after rename saves).
3. Tests: `.desktop` parsing on fixture files, command building without a shell, backup behaviour. No real editor launched.

```sh
geany-progress done 13 -r src/ui/artwork_launcher.cpp -r src/ui/artwork_view.cpp -r tests/test_trakcrate_artwork.cpp
```

## Phase 14: Scripts menu

Read: `doc/trakcrate/scripts.md` (all of it). Manual execution only.

1. `ScriptRunner`: discover `scripts/track-script-*` and `crate-script-*` (regular, executable, owner-checked, not world-writable), run with `QProcess` (no shell, argv list, env `TRAKCRATE_*` per the doc, stdin closed, own process group, timeout, nice 10, output cap), snapshot (non-audio files) to `.trakcrate-history/` before, "undo last script", library lock, review state by SHA-256 in user settings (new/changed scripts need confirmation).
2. UI: Scripts menus in the track editor and the pane, a Scripts log, "New script" template, "Open with editor" via the `.desktop` mechanism (never `xdg-open` on executables).
3. Tests with throw-away scripts in a temp library: ordering, exit status, timeout kill of child processes, output cap, snapshot/restore, refusal of world-writable files and of unreviewed scripts.

```sh
geany-progress done 14 -r src/ui/trakcrate_scripts.cpp -r tests/test_trakcrate_scripts.cpp
```

## Not in this plan

CLI (`doc/trakcrate/cli.md`), vocal analysis, rekordbox pendrive **import** (`import-from-rekordbox.md`: next plan; the readers exist), `rsync` sync/orphan removal, hot cue / loop editing, PSSI phrase data, `.2EX`/PWV6/PWV7, packaging as `trakcrate-cli`.
