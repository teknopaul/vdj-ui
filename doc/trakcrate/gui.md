# trakcrate UI pane (requirements)

A pane in vdj-ui for editing the trakcrate master database (`TRAKCRATE-FORMAT.md`, the contract). The pane is C++/Qt code in `src/trakcrate/` (Qt-free core, `src/ui/trakcrate_*` for the widgets) that reads and writes the library directly. The command line tools (`cli.md`, `src-cli/`) are a separate implementation of the same format for script writers: they share no code with the GUI and neither depends on the other. See "GUI and CLI are separate" below. Because the database is a plain directory tree, the pane must cope with files changing underneath it (the user editing `meta.txt` in an editor, a CLI run in a terminal).

## Placement

vdj-ui builds its panels as `QDockWidget`s in `MainWindow` (Debug at the bottom, Recording and the Rekordbox PC mode on the right, toggled by checkable actions). The trakcrate pane follows that pattern: a dock created lazily by a `trakcrateDock()` accessor, toggled from a View menu action, with its widget in its own `src/ui/trakcrate_panel.{h,cpp}` (as `rekordbox_mode_panel` does). It will want to be large, so it must also be floatable and openable as a separate window. State (selected library, columns, splitter positions) is saved with the other window settings.

## Layout

```
┌ trakcrate ─────────────────────────────────────────────────────────────┐
│ Library: [~/trakcrate ▾] [Open…] [Import folder…] [Import rekordbox…]  │
├───────────────┬────────────────────────────────────────┬──────────────┤
│ Folders       │ Tracks (filter: [__________])           │ Track        │
│  ▸ Acid Loops │  Title   Artist   BPM  Key  Wave  Orig  │ ┌──────────┐ │
│  ▸ Gig 2026   │  …                                      │ │ artwork  │ │
│ Playlists     │                                         │ └──────────┘ │
│  ▸ friday     │                                         │ title …      │
│  ▸ Gig 2026   │                                         │ artist …     │
│               │                                         │ [waveform]   │
│               │                                         │ cues, grid   │
├───────────────┴────────────────────────────────────────┴──────────────┤
│ Jobs: analysing 3/12 ████░░░░  [Cancel]   Messages                     │
└────────────────────────────────────────────────────────────────────────┘
```

Left: folders of `track-master-copy` and the playlists (`playlists/*`). Centre: track table. Right: detail/editor for the selection. Bottom: background jobs and a message log.

## Functions

### Library
- Open or create a library (`init`); remember recent ones.
- Import a folder of bare audio files (dialog for folder, playlist name defaulting to the directory name, recursive, analyse after; same options as `trakcrate import`).
- Import a real rekordbox pendrive (`import-rekordbox`) with a report dialog: counts, missing audio, conflicts.
- Rescan when files change on disk (file system watcher, plus a Refresh button; large libraries must not be rescanned on every change event).

### Track table
- **Comment (implemented):** a Comment column (after Album): double click it, or press F2, to edit the text; Enter saves it to the track's `meta.txt` (`comment`), which the exporter writes to the drive's database, where the decks show it when the track is selected. The track editor has the same field, labelled, for the loaded track.
- **Rating (implemented):** a Rating column and the details pane always show five stars, solid for the rating and hollow for the rest ("★★☆☆☆"; gold and grey, dark theme only). Clicking a star in the table sets it, the same star again clears it. The edit rewrites the track's `meta.txt` (`rating`, 0 to 5) and marks `meta` as `edited` in `source.txt`, in a folder or a playlist alike (a playlist row is a link to the same track folder). Sorting the column sorts by stars. These table edits are not on the editor's undo stack.
- Columns: title, artist, album, genre, BPM, key, duration, loudness, a mini waveform (reuse `waveform_delegate`), origin (generated / rekordbox / edited), status flags (missing audio, no beat grid, not analysed).
- Sort and filter by any column; quick filter box; saved filters for chores such as "no beat grid", "origin rekordbox".
- Multi-select; drag to reorder inside a playlist and drag tracks between folders and playlists; the drag changes the `000_` symlink prefixes.
- Context menu: Analyse, Re-analyse, Open folder in file manager, Remove from playlist, Remove from library (confirmation).

### Editing a track
- Metadata form (title, artist, album, genre, label, key, BPM, year, rating, colour, comment) saved to the readable `meta.txt`; fields changed by the user are marked `edited` in `source.txt`.
- **Bulk edit** for several selected tracks (set genre, album, strip " (Original Mix)" from titles, as noted in the earlier prompts).
- **Artwork**: show it, replace by file picker or drag and drop, remove, re-extract from the tag (`artwork.md`).
- **Beat grid**: show the grid on the waveform, change BPM, nudge the first beat, tap tempo, double/halve the tempo, set the downbeat, re-detect (`beat-grid.md`). Warn before overwriting a grid whose origin is `rekordbox`.
- **Cues and loops**: add, move, delete hot cues A-H, memory points and loops, with comments/colours once supported (`track_analysis` and `waveform_window` already draw cues, loops and the grid; here they become editable).
- **Waveform**: preview only, plus "regenerate". Heights are never normalised per track, so the DJ can see an unnormalised track (`waveforms.md`).
- Audition: play the track from the master (the app has an audio player), so the DJ can check a grid by ear.

### Playlists / pendrives
- Create, rename, delete; add tracks; reorder with drag and drop; show the resulting order numbers.
- Per-pendrive summary: track count, total size, estimated export size, and tracks that would fail (missing audio, not analysed).
- **Sizes in the tree (implemented):** the **Folders** heading shows the estimated disk usage of all the folders in decimal gigabytes to two places ("Folders (23.34Gb)"). Each virtual pendrive shows the size of the drive it needs, in whole gigabytes rounded UP ("House Set (2Gb)"; 1.000001Gb is 2Gb). Rounding up is deliberate: a drive's real free bytes are imprecise, one byte over and it does not fit, drives are sold in whole gigabytes, and the player writes to a drive as soon as it is inserted so it needs spare room. The estimate is each track's audio file plus 150 kB for analysis files and artwork, counted once per track however many lists it is in, plus 300 kB for the database and settings (`Library::estimateBytes`, `formatFolderSize`, `pendriveGigabytes`).
- **Export** to a folder or mounted drive (the staged tmpfs-then-single-write flow), with progress and a verify step; **Sync** an existing drive with a preview of what will be added and what orphans will be removed, with the mandatory warning before removing.

### User settings (implemented)
- The **User Settings** node of the tree lists the settings sets (the default in bold, "(default)"), each with its files. With no set it shows a hint; double-click creates one. A library needs none to export (built in files are used).
- Right-click: **New settings...** (set name and the name to show on the players), **Copy settings from a rekordbox pendrive...**, and on a set **View...**, **Compare with another...**, **Make default**, **Change the name on the players...**, **Rename...**, **Duplicate...**, **Delete...** (refused while a virtual pendrive uses it). Double-click a set or file opens the viewer.
- The **viewer** is read only: header strings, sizes, CRC check and each payload byte of the `*SETTING.DAT` files (option codes marked, not decoded), the profile's name and date, the player and firmware in `RBFLTR.DAT`, the page layout of `exportExt.pdb`, plus a hex dump. Grey rows are not understood. Comparing two sets highlights the fields that differ. The only editable value is the name in `djprofile.nxs`.
- **Virtual pendrives:** right-click > **Settings** links the pendrive to a set (or "Follow the default settings").
- **Export dialog:** a *Player settings* box with a drop down (Default, each set, Built in files) and a choice between keeping the settings files already on the drive (add the missing) and updating them. The result line reports how many were written and kept.

### trakcrate only window (implemented)
- `trakcrate-ui` (a symlink to `vdj-ui`, installed by the deb in `/usr/bin`) or `vdj-ui --trakcrate` opens just the library, in a window of its own: title "📦 trakcrate", the trakcrate icon in the taskbar (`trakcrate-ui.desktop`), no deck, network, MIDI or audio sequencer code running. It shares `~/.config/vdj-ui` with vdj-ui, so the same library is opened.
- Menus: **File** has Clone Pendrive and Quit; **Config** has Quantize and Quit. No Connect button, no Network or View menus. The menus are built only in `TrakcrateWindow::setupMenus()` (`src/ui/trakcrate_window.cpp`): that is the one place to change them.
- The folder and playlist tree starts at 340 pixels instead of 170.
- There is no player: the table's "Load into player" becomes **Edit track** (also the double click) and opens the track editor in a dock at the bottom.

### Deck limits (implemented)
- A pendrive export is refused, with the numbers, when it holds more than an older deck can take (10000 tracks, 10000 artists, 10000 albums); nothing is written. The library itself has no limit. The red message in the export dialog says what to shorten.
- Only then the dialog offers **Apply modern deck limits (...YMMV)**: ticking it allows 20000 tracks and 16000 artists and albums, for newer decks. Details in `rekordbox-db-output.md`.

### Provenance and unknown bytes
- Origin per field group is visible and filterable; a "Regenerate" action changes it to generated after a confirmation.
- A view of `UNKNOWN_BYTES.txt` entries and which are filled from real imports, so contributors see what is still invented.

## Behaviour requirements

- **Never block the UI thread.** Decoding, analysis, import, export and verify run as background jobs (thread pool, as in the CLI `--jobs`), with progress, cancel, and results back as signals. The panel remains usable while jobs run; track rows update as results arrive.
- **GUI and CLI are separate.** Both work on the same plain files. Concurrency: multi-file writes take the library lock (`TRAKCRATE-FORMAT.md` section 16); if it is held (the CLI in another terminal, a script) the GUI shows "library busy" instead of failing.
- **Undo.** Metadata and cue edits go on a `QUndoStack` for the session. Destructive actions (remove from library, delete orphans, replace playlist) are confirmed and, where possible, move files to a `.trash/` folder inside the library instead of deleting.
- **Atomic, small writes**: edits write the changed file via temp and rename; no bulk rewrite of the library.
- **External edits**: if a `meta.txt` changes on disk while the user has unsaved edits in the form, show a conflict prompt rather than overwriting.
- **Large libraries**: use a model/view with lazy loading (`sample_tree_model` is the existing pattern for trees); thousands of tracks, thumbnail and waveform images loaded on demand and cached.
- **Accessibility and keyboard**: all actions in menus with shortcuts; the table is fully keyboard driven. Dark theme like the rest of the app (`dark_theme`).
- **No hidden state**: anything the pane changes is visible as a file in the library, which is the point of the format.
- **Safety with real pendrives**: the pane writes to physical drives only through the export/sync flow, never during edits.

## GUI and CLI are separate

Decided (revised 2026-10-28): the GUI and the command line tools are two independent implementations of the documented library format; no shared code, no dependency either way. (An earlier decision had the GUI run `trakcrate` commands as child processes; the finished GUI links the Qt-free `src/trakcrate/` library instead, and the CLI became a toolkit for people scripting the format.)

- **The format is the contract** (`TRAKCRATE-FORMAT.md`): provenance (`origin: edited`), atomic writes, preserved unknown keys and the lock are rules of the format, so either side leaves the library valid for the other.
- **External changes**: the GUI watches the files and reloads, so a CLI run, a script or a text editor can change the library while the pane is open.
- **Undo** is the GUI's own stack of inverse commands, not a library feature; destructive actions use the `.trash/` folder and the script snapshots (`scripts.md`).
- **Extending the GUI**: scripts in a library's `scripts/` run from the GUI's script menus (`scripts.md`) and may call `trakcrate` commands when the CLI package is installed; the GUI needs no knowledge of them.
- **Packaging**: the vdj-ui package does not depend on `trakcrate-cli`, and the CLI never depends on the GUI.
- **Duplicated logic** is expected for now (analysis, PDB/ANLZ writers, import). Candidates for a common C library (libcdj) are logged in `ai-output/refactor-recommendations.md`.

## Build and code organisation

- New files under `src/ui/`: `trakcrate_panel`, `trakcrate_track_model`, `trakcrate_track_editor`, `trakcrate_import_dialog`, `trakcrate_export_dialog`, and a `trakcrate_jobs` helper. Business logic lives in the Qt-free `src/trakcrate/` library; the widgets only format and display.
- Tests: model tests with a temporary library built from fixtures; no GUI-driven tests needed for logic, since it lives in the library.

## Phasing

1. Read-only browser: open a library, folders/playlists/tracks, details pane, waveform and artwork preview.
2. Import dialogs (folder, rekordbox pendrive) with background jobs.
3. Metadata, artwork and playlist editing, undo.
4. Beat grid and cue editing, audition.
5. Export / sync with preview, verify and the orphan warning.

## Open questions

- A dock inside the main window, or a separate top-level "library" window? The deck UI is busy; a separate window may suit editing better and the dock can stay a small status/launcher.
- How much of the existing Rekordbox PC-mode and `track_browser` should be reused? The browser already shows folders and rekordbox DBs from pendrives; the trakcrate pane could be a mode of it so a user can drag from a real pendrive into the master.
- Undo scope across the CLI and GUI (the CLI has none; the GUI's stack covers only its own edits).
- Whether bulk text edits (regex rename) belong in the GUI or only in the CLI.

## Preview player (implemented)

A mini player sits under the track list (`src/ui/trakcrate_preview_player.*`). It plays the shown playlist, a list of a virtual pendrive, or (on the pendrive itself) all its lists one after the other, so a DJ can listen to a whole set at home.

- Play/Pause, Stop, previous and next. "Play the list from here" is in the track context menu. Folders cannot be played.
- **Natural BPM** plays each track at its own tempo. **All at BPM** plays every track at one BPM: the track's BPM comes from `beatgrid.txt` (else the BPM tag), the tempo ratio is clamped to 0.5 to 2, and a track without a BPM plays at its natural tempo. The stretching is done in memory by `AudioPlayer`; no file is changed.
- The playing track is selected in the list as it changes, so its artwork and details show. On a pendrive the tree moves from list to list. If you browse another list meanwhile, the view is left alone.
- One list at a time. Playing is refused while the app is connected to the Pro DJ Link network, Connect stops the player first, and loading a track into the deck stops it. Stop gives the audio output back to the DJ decks.
- The mode and BPM are remembered (`trakcrate/preview_mode`, `trakcrate/preview_bpm`).

## Ordering (implemented)

- A playlist (and a list of a virtual pendrive) is always shown in its own order, which is the DJ's work: the columns cannot be sorted there. Folders can be sorted by a column.
- **Drag and drop:** drag the selected rows to a new place in a playlist or pendrive list. A line shows where they will land. Several rows move as one block.
- **Right click:** Move to start, Move up, Move down, Move to end (on the selected rows).
- **Virtual pendrives:** drag a list within its pendrive in the tree to put the lists in order (dropping on the pendrive itself puts it last).
- The order is written at once (`NNN_` prefixes, `Library::moveEntries`); there is no "clear sorting" because the order is never lost.
